// podcast-sanity - autonomous restoration policy
#include "sanity/auto_master.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/clean.hpp"
#include "sanity/dsp.hpp"
#include "sanity/dynamics.hpp"
#include "sanity/json.hpp"
#include "sanity/loudness.hpp"
#include "sanity/metrics.hpp"

#ifndef SANITY_VERSION
#define SANITY_VERSION "0.0.0"
#endif

namespace sanity {

namespace {

constexpr double kExcerptSeconds = 8.0;
constexpr double kExcerptContextSeconds = 0.5;
constexpr std::size_t kMaxExcerpts = 8;
constexpr double kQuietTieDb = 1.0;
// Total frame energy includes the noise that shared the speech frames, so
// removing noise legitimately lowers it. A larger loss than this means the
// candidate suppressed the voice itself.
constexpr double kSpeechEnvelopeGateDb = -12.0;
constexpr double kSpeechBandGateDb = -1.5;
// SI-SDR against the preservation render, measured on speech samples only.
// 0 dB means the change is as large as the voice, which is already too much.
constexpr double kSpeechSiSdrGateDb = 0.0;
constexpr double kStereoImbalanceGateDb = 0.5;
constexpr double kFullFileEnvelopeGateDb = -12.0;
constexpr double kFullFileBandGateDb = -2.0;

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t i = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1.0 - frac) + v[i + 1] * frac : v[i];
}

// Same detector rule as the analysis: the loudest channel, never an L+R sum.
std::vector<float> detector_of(const AudioBuffer& b, std::vector<double>* rms_out = nullptr) {
    const int ch = b.channels > 0 ? b.channels : 1;
    const std::size_t n = b.frames();
    std::vector<double> energy(static_cast<std::size_t>(ch), 0.0);
    for (int c = 0; c < ch; c++) {
        double e = 0.0;
        for (std::size_t i = 0; i < n; i++) {
            const double v = b.data[i * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)];
            e += v * v;
        }
        energy[static_cast<std::size_t>(c)] = n ? e / static_cast<double>(n) : 0.0;
    }
    int best = 0;
    for (int c = 1; c < ch; c++)
        if (energy[static_cast<std::size_t>(c)] > energy[static_cast<std::size_t>(best)]) best = c;
    if (rms_out) {
        rms_out->assign(static_cast<std::size_t>(ch), -200.0);
        for (int c = 0; c < ch; c++)
            (*rms_out)[static_cast<std::size_t>(c)] =
                energy[static_cast<std::size_t>(c)] > 0
                    ? 10.0 * std::log10(energy[static_cast<std::size_t>(c)])
                    : -200.0;
    }
    std::vector<float> d(n, 0.0f);
    for (std::size_t i = 0; i < n; i++)
        d[i] = b.data[i * static_cast<std::size_t>(ch) + static_cast<std::size_t>(best)];
    return d;
}

std::vector<std::pair<std::size_t, std::size_t>> frame_ranges(
    const std::vector<FrameFeat>& feats, bool want_speech) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t i = 0;
    while (i < feats.size()) {
        const bool hit = want_speech ? feats[i].speech
                                     : (!feats[i].speech && feats[i].rms_db > -70.0);
        if (hit) {
            std::size_t j = i;
            while (j < feats.size()) {
                const bool keep = want_speech ? feats[j].speech
                                              : (!feats[j].speech && feats[j].rms_db > -70.0);
                if (!keep) break;
                j++;
            }
            out.emplace_back(i, j);
            i = j;
        } else {
            i++;
        }
    }
    return out;
}

// Mean energy over a set of frame ranges, summed across channels, in dB.
double range_energy_db(const AudioBuffer& b,
                       const std::vector<std::pair<std::size_t, std::size_t>>& ranges, int hop) {
    const std::size_t ch = static_cast<std::size_t>(b.channels > 0 ? b.channels : 1);
    const std::size_t n = b.frames();
    const std::size_t h = static_cast<std::size_t>(hop > 0 ? hop : 1);
    double e = 0.0;
    std::size_t count = 0;
    for (const auto& r : ranges) {
        const std::size_t a = r.first * h;
        const std::size_t z = std::min(n, r.second * h);
        for (std::size_t i = a; i < z; i++)
            for (std::size_t c = 0; c < ch; c++) {
                const double v = b.data[i * ch + c];
                e += v * v;
                count++;
            }
    }
    if (count == 0) return -200.0;
    const double ms = e / static_cast<double>(count);
    return ms > 0 ? 10.0 * std::log10(ms) : -200.0;
}

// Full-scale overflow only. The working representation is float, so a sample
// slightly above 0.9999 is headroom, not damage; reaching 1.0 is what the
// delivery limiter would have to repair.
long long count_clipped(const AudioBuffer& b) {
    long long n = 0;
    for (float v : b.data)
        if (std::fabs(static_cast<double>(v)) >= 1.0) n++;
    return n;
}

// Narrow-band energy around a mains component, used to prove the notch worked.
double tone_db(const std::vector<float>& x, int rate, double freq) {
    if (freq <= 0.0 || x.empty()) return -200.0;
    std::vector<float> y = x;
    Biquad hp = Biquad::highpass(rate, freq * 0.96, 2.0);
    biquad_apply_zero_phase(hp, y);
    Biquad lp = Biquad::lowpass(rate, std::min(freq * 1.04, rate * 0.45), 2.0);
    biquad_apply_zero_phase(lp, y);
    const double r = rms(y);
    return r > 0 ? 20.0 * std::log10(r) : -200.0;
}

// Energy in the speech band over the given ranges, measured on a mono signal.
double speech_band_db(const std::vector<float>& mono, int rate,
                      const std::vector<std::pair<std::size_t, std::size_t>>& ranges, int hop) {
    if (mono.empty()) return -200.0;
    std::vector<float> band = mono;
    Biquad hp = Biquad::highpass(rate, 300.0, 0.707);
    biquad_apply_zero_phase(hp, band);
    Biquad lp = Biquad::lowpass(rate, std::min(3400.0, rate * 0.45), 0.707);
    biquad_apply_zero_phase(lp, band);
    AudioBuffer b;
    b.sample_rate = rate;
    b.channels = 1;
    b.data = std::move(band);
    return range_energy_db(b, ranges, hop);
}

double stereo_imbalance_db(const AudioBuffer& b) {
    if (b.channels < 2) return 0.0;
    const std::size_t n = b.frames();
    double l = 0.0, r = 0.0;
    for (std::size_t i = 0; i < n; i++) {
        const double lv = b.data[i * 2];
        const double rv = b.data[i * 2 + 1];
        l += lv * lv;
        r += rv * rv;
    }
    if (l <= 1e-20 || r <= 1e-20) return 0.0;
    return 10.0 * std::log10(l / r);
}

struct ExcerptMetrics {
    double quiet_reduction_db = 0.0;
    double hum_reduction_db = 0.0;
    double speech_envelope_db = 0.0;
    double speech_band_db = 0.0;
    double si_sdr_db = 0.0;
    double stereo_imbalance_change_db = 0.0;
    long long new_clipped = 0;
    std::size_t quiet_frames = 0;
    std::size_t speech_frames = 0;
};

// SI-SDR between a candidate and the preservation render, restricted to the
// samples the shared speech mask calls speech. On those samples the voice
// dominates, so removing noise should not lower this value; a drop means the
// candidate ate part of the voice. `si_sdr_db` fits the gain for us.
double speech_si_sdr_db(const std::vector<float>& candidate, const std::vector<float>& reference,
                        const std::vector<FrameFeat>& feats, int hop) {
    std::vector<float> c;
    std::vector<float> r;
    const std::size_t h = static_cast<std::size_t>(hop > 0 ? hop : 1);
    for (std::size_t f = 0; f < feats.size(); f++) {
        if (!feats[f].speech) continue;
        const std::size_t a = f * h;
        const std::size_t z = std::min(std::min(candidate.size(), reference.size()), a + h);
        for (std::size_t i = a; i < z; i++) {
            c.push_back(candidate[i]);
            r.push_back(reference[i]);
        }
    }
    if (c.size() < 32) return 0.0;
    return si_sdr_db(c, r);
}

ExcerptMetrics measure_excerpt(const AudioBuffer& before, const AudioBuffer& after,
                               const std::vector<FrameFeat>& feats, double hum_freq,
                               const std::vector<float>& preservation_det) {
    ExcerptMetrics m;
    const int rate = before.sample_rate;
    const int hop = segmenter_hop(rate);

    const std::vector<std::pair<std::size_t, std::size_t>> quiet = frame_ranges(feats, false);
    const std::vector<std::pair<std::size_t, std::size_t>> speech = frame_ranges(feats, true);
    for (const auto& r : quiet) m.quiet_frames += r.second - r.first;
    for (const auto& r : speech) m.speech_frames += r.second - r.first;

    const std::vector<float> before_det = detector_of(before);
    const std::vector<float> after_det = detector_of(after);

    // Matched active-speech gain: every comparison below is made after the
    // candidate's intentional level change is removed.
    const double gain_db = active_speech_level_db(after_det, rate) -
                           active_speech_level_db(before_det, rate);

    if (!quiet.empty()) {
        const double qb = range_energy_db(before, quiet, hop);
        const double qa = range_energy_db(after, quiet, hop);
        m.quiet_reduction_db = qb - (qa - gain_db);
    }
    if (hum_freq > 0.0) {
        const double hb = tone_db(before_det, rate, hum_freq);
        const double ha = tone_db(after_det, rate, hum_freq);
        m.hum_reduction_db = hb - (ha - gain_db);
    }
    if (!speech.empty()) {
        // Per-frame envelope ratios, not per-range: a single quiet range must
        // not decide the fate of the whole candidate. The 10th percentile is
        // the "worst speech that survived" proxy.
        std::vector<double> ratios;
        for (std::size_t f = 0; f < feats.size(); f++) {
            if (!feats[f].speech) continue;
            const std::size_t a = f * static_cast<std::size_t>(hop);
            const std::size_t z = std::min(before.frames(), a + static_cast<std::size_t>(hop));
            double eb = 0.0, ea = 0.0;
            std::size_t count = 0;
            for (std::size_t i = a; i < z; i++) {
                eb += static_cast<double>(before_det[i]) * before_det[i];
                ea += static_cast<double>(after_det[i]) * after_det[i];
                count++;
            }
            if (count == 0 || eb <= 1e-20) continue;
            ratios.push_back(10.0 * std::log10((ea + 1e-20) / eb) - gain_db);
        }
        m.speech_envelope_db = percentile(ratios, 0.10);
        m.speech_band_db =
            speech_band_db(after_det, rate, speech, hop) - gain_db -
            speech_band_db(before_det, rate, speech, hop);
    }
    m.si_sdr_db = speech_si_sdr_db(after_det, preservation_det, feats, hop);
    if (before.channels >= 2) {
        m.stereo_imbalance_change_db =
            std::fabs(stereo_imbalance_db(after) - stereo_imbalance_db(before));
    }
    m.new_clipped = std::max<long long>(0, count_clipped(after) - count_clipped(before));
    return m;
}

std::vector<AutoSettings> base_candidates() {
    std::vector<AutoSettings> out;
    auto add = [&](const char* name, double denoise, double dereverb) {
        AutoSettings s;
        s.profile = name;
        s.denoise_strength = denoise;
        s.dereverb_strength = dereverb;
        s.dereverb = dereverb > 0.0;
        out.push_back(s);
    };
    add("preservation", 0.0, 0.0);
    add("gentle", 0.4, 0.2);
    add("balanced", 0.8, 0.4);
    add("strong", 1.2, 0.6);
    return out;
}

void configure_from_evidence(AutoSettings& s, const Analysis& a, bool reverb_reliable) {
    s.dehum = a.hum_present;
    s.adaptive_highpass = true;
    s.declick = true;
    s.dereverb = s.dereverb && reverb_reliable;

    // De-essing only when the 5-8 kHz band actually stands out.
    s.deess = a.sibilance_db > -16.0;
    s.deess_amount = s.deess ? std::min(1.0, std::max(0.2, (a.sibilance_db + 16.0) / 6.0)) : 0.0;

    // Leveling from the measured loudness range: a file that already sits in a
    // narrow range needs only a light touch.
    const double spread = a.lra > 0.1 ? a.lra
                                     : std::max(0.0, a.max_short_term - a.speech_activity_db);
    if (spread >= 10.0) s.leveler_strength = 1.0;
    else if (spread >= 6.0) s.leveler_strength = 0.8;
    else if (spread >= 3.0) s.leveler_strength = 0.5;
    else s.leveler_strength = 0.2;
    s.leveler = true;

    s.compress = true;
    s.compressor_threshold_db = a.speech_activity_db > -60.0 ? a.speech_activity_db - 6.0 : -18.0;
    s.compressor_ratio = 3.0;

    if (a.music_ratio > 0.25) {
        // Continuous background: keep restoration conservative.
        s.denoise_strength = std::min(s.denoise_strength, 0.4);
        s.dereverb_strength = std::min(s.dereverb_strength, 0.2);
        s.dereverb = s.dereverb && reverb_reliable;
    }
}

// Copies frames [start, end) into a standalone buffer.
AudioBuffer slice(const AudioBuffer& b, std::size_t start, std::size_t end) {
    AudioBuffer out;
    out.sample_rate = b.sample_rate;
    out.channels = b.channels;
    end = std::min(end, b.frames());
    start = std::min(start, end);
    const std::size_t ch = static_cast<std::size_t>(b.channels);
    out.data.assign(b.data.begin() + static_cast<std::ptrdiff_t>(start * ch),
                    b.data.begin() + static_cast<std::ptrdiff_t>(end * ch));
    return out;
}

std::vector<AutoExcerpt> build_excerpts(const AudioBuffer& in, const std::vector<FrameFeat>& feats,
                                        const Analysis& a) {
    std::vector<AutoExcerpt> out;
    const int rate = in.sample_rate;
    const std::size_t n = in.frames();
    const std::size_t hop = static_cast<std::size_t>(segmenter_hop(rate));
    if (n == 0 || rate <= 0) return out;
    // Shorter than the whole excerpt budget: the full source is the most
    // representative material available, so use it.
    if (n <= static_cast<std::size_t>(kExcerptSeconds * kMaxExcerpts * rate)) {
        AutoExcerpt e;
        e.start_s = 0.0;
        e.end_s = in.duration();
        e.reason = "whole file (shorter than the excerpt budget)";
        out.push_back(e);
        return out;
    }
    const std::size_t win = std::min(n, static_cast<std::size_t>(kExcerptSeconds * rate));
    const std::size_t ctx = static_cast<std::size_t>(kExcerptContextSeconds * rate);

    std::vector<std::size_t> anchors;
    auto push = [&](std::size_t frame, const char* reason) {
        const std::size_t anchor = std::min(n - 1, frame * hop);
        const std::size_t start = anchor > win / 2 ? anchor - win / 2 : 0;
        const std::size_t end = std::min(n, start + win);
        AutoExcerpt e;
        e.start_s = static_cast<double>(start > ctx ? start - ctx : 0) / rate;
        e.end_s = static_cast<double>(std::min(n, end + ctx)) / rate;
        e.reason = reason;
        // Reject overlaps with an already chosen excerpt.
        for (const auto& other : out) {
            const double oa = other.start_s * rate, ob = other.end_s * rate;
            const double overlap = std::min(e.end_s * rate, ob) - std::max(e.start_s * rate, oa);
            const double shortest = std::min(e.end_s * rate - e.start_s * rate, ob - oa);
            if (shortest > 0.0 && overlap / shortest > 0.5) return;
        }
        if (out.size() < kMaxExcerpts) out.push_back(e);
    };

    push(0, "start");
    if (n > win) push((n / hop) / 2, "middle");
    if (n > win) push((n - win) / hop, "end");

    // Quietest, loudest and lowest-level speech frames.
    std::size_t quietest = 0, loudest = 0, lowest_speech = 0;
    double qmin = 1e30, lmax = -1e30, smin = 1e30;
    for (std::size_t f = 0; f < feats.size(); f++) {
        if (!feats[f].speech && feats[f].rms_db > -70.0 && feats[f].rms_db < qmin) {
            qmin = feats[f].rms_db;
            quietest = f;
        }
        if (feats[f].speech) {
            if (feats[f].rms_db > lmax) {
                lmax = feats[f].rms_db;
                loudest = f;
            }
            if (feats[f].rms_db > a.noise_floor_db + 3.0 && feats[f].rms_db < smin) {
                smin = feats[f].rms_db;
                lowest_speech = f;
            }
        }
    }
    if (qmin < 1e29) push(quietest, "quietest region");
    if (lmax > -1e29) push(loudest, "loudest speech");
    if (smin < 1e29) push(lowest_speech, "lowest-level speech");

    // Loudest transient.
    std::size_t peak_at = 0;
    double peak = 0.0;
    for (std::size_t i = 0; i < n; i++) {
        const double v = std::fabs(static_cast<double>(in.data[i * static_cast<std::size_t>(in.channels)]));
        if (v > peak) {
            peak = v;
            peak_at = i;
        }
    }
    push(peak_at / hop, "transient");

    if (a.rt60 > 0.30) push(feats.size() / 4, "reverb evidence");
    return out;
}

// Restoration only, shared by the candidate comparison and the full render.
void apply_restoration(AudioBuffer& buffer, const AutoSettings& s, const Analysis& base,
                       double floor_hz) {
    const int rate = buffer.sample_rate;
    std::vector<float> detector = detector_of(buffer);
    Segmenter seg(SegmenterConfig{rate, 10.0, 35.0});
    std::vector<FrameFeat> feats;
    seg.features(detector, feats);

    const std::size_t ch = static_cast<std::size_t>(buffer.channels);
    const std::size_t n = buffer.frames();
    auto per_channel = [&](auto&& fn) {
        std::vector<float> channel(n);
        for (std::size_t c = 0; c < ch; c++) {
            for (std::size_t i = 0; i < n; i++) channel[i] = buffer.data[i * ch + c];
            fn(channel);
            for (std::size_t i = 0; i < n; i++) buffer.data[i * ch + c] = channel[i];
        }
    };

    // Repair.
    per_channel([&](std::vector<float>& x) { remove_dc(x); });
    if (s.declick) per_channel([&](std::vector<float>& x) { declick(x, rate); });
    if (s.dehum)
        per_channel([&](std::vector<float>& x) {
            dehum(x, rate, feats, base.noise_floor_db, true);
        });
    if (s.adaptive_highpass)
        per_channel([&](std::vector<float>& x) {
            adaptive_highpass(x, rate, feats, floor_hz, true);
        });
    if (s.denoise_strength > 0.0)
        per_channel([&](std::vector<float>& x) {
            denoise(x, rate, feats, s.denoise_strength);
        });
    if (s.dereverb && s.dereverb_strength > 0.0)
        per_channel([&](std::vector<float>& x) {
            dereverb(x, rate, feats, s.dereverb_strength);
        });
    if (s.deess && s.deess_amount > 0.0)
        per_channel([&](std::vector<float>& x) { deess(x, rate, s.deess_amount); });
}

void apply_dynamics(AudioBuffer& buffer, const AutoSettings& s, const Analysis& base) {
    if (buffer.frames() == 0) return;
    std::vector<float> detector = detector_of(buffer);
    Segmenter seg(SegmenterConfig{buffer.sample_rate, 10.0, 35.0});
    std::vector<FrameFeat> feats;
    seg.features(detector, feats);
    if (s.leveler && s.leveler_strength > 0.0)
        adaptive_leveler_channels(buffer, feats, s.leveler_strength);
    if (s.compress)
        compressor_channels(buffer, s.compressor_threshold_db, s.compressor_ratio, 10.0, 200.0);
    (void)base;
}

struct RenderResult {
    AudioBuffer audio;
    ExcerptMetrics restoration_metrics;
};

RenderResult render_with_settings(const AudioBuffer& in, const AutoSettings& settings) {
    RenderResult r;
    const Analysis base = analyze(in);
    const double floor_hz = 55.0;

    r.audio = in;
    apply_restoration(r.audio, settings, base, floor_hz);

    // Preservation is judged on the restoration output, before the intentional
    // level change of the leveler and compressor can hide a loss.
    {
        std::vector<float> detector = detector_of(in);
        Segmenter seg(SegmenterConfig{in.sample_rate, 10.0, 35.0});
        std::vector<FrameFeat> feats;
        seg.features(detector, feats);
        r.restoration_metrics = measure_excerpt(in, r.audio, feats, base.hum_freq, detector);
    }

    apply_dynamics(r.audio, settings, base);
    return r;
}

}  // namespace

void apply_auto_settings(const AudioBuffer& in, const AutoSettings& settings, AudioBuffer& out) {
    out = render_with_settings(in, settings).audio;
}

AutoReport choose_auto_settings(const AudioBuffer& in) {
    AutoReport report;
    report.version = SANITY_VERSION;
    report.before = analyze(in);
    const Analysis& base = report.before;

    if (in.frames() == 0 || in.sample_rate <= 0) {
        report.warnings.push_back("empty input: no settings to choose");
        return report;
    }

    std::vector<float> detector = detector_of(in);
    Segmenter seg(SegmenterConfig{in.sample_rate, 10.0, 35.0});
    std::vector<FrameFeat> feats;
    seg.features(detector, feats);

    report.excerpts = build_excerpts(in, feats, base);
    if (report.excerpts.empty()) {
        AutoExcerpt e;
        e.start_s = 0.0;
        e.end_s = in.duration();
        e.reason = "whole file";
        report.excerpts.push_back(e);
    }

    // Evidence confidence: speech present, quiet regions, and enough quiet
    // coverage to trust a noise reduction measurement.
    std::size_t quiet_frames = 0, speech_frames = 0;
    for (const auto& f : feats) {
        if (f.speech) speech_frames++;
        else if (f.rms_db > -70.0) quiet_frames++;
    }
    const double total = static_cast<double>(std::max<std::size_t>(1, feats.size()));
    double confidence = 0.0;
    if (speech_frames / total > 0.05) confidence += 0.3;
    if (base.noise_regions >= 2) confidence += 0.3;
    confidence += 0.4 * std::min(1.0, (quiet_frames / total) / 0.02);
    confidence = std::min(1.0, confidence);

    const bool reverb_reliable = base.rt60 >= 0.30 && base.speech_ratio > 0.05;
    const bool enough_quiet = quiet_frames >= 20;

    std::vector<AutoSettings> candidates = base_candidates();
    for (auto& c : candidates) configure_from_evidence(c, base, reverb_reliable);
    if (!enough_quiet) {
        // Without quiet evidence, never let the search reach for strong
        // suppression. This is the rule that stops a failed noise estimate
        // from becoming maximum denoise.
        for (auto& c : candidates) {
            if (c.denoise_strength > 0.4) {
                c.denoise_strength = 0.4;
                c.dereverb_strength = std::min(c.dereverb_strength, 0.2);
                c.dereverb = c.dereverb && reverb_reliable;
            }
        }
        report.warnings.push_back("insufficient quiet evidence: restoration kept conservative");
    }

    // Score each candidate on the excerpts. The preservation render is the
    // reference for the SI-SDR diagnostic.
    std::vector<ExcerptMetrics> totals(candidates.size());
    std::vector<double> weight(candidates.size(), 0.0);

    for (std::size_t x = 0; x < report.excerpts.size(); x++) {
        const std::size_t a = static_cast<std::size_t>(report.excerpts[x].start_s * in.sample_rate);
        const std::size_t b = static_cast<std::size_t>(report.excerpts[x].end_s * in.sample_rate);
        AudioBuffer excerpt = slice(in, a, b);
        if (excerpt.frames() < static_cast<std::size_t>(in.sample_rate) / 2) continue;
        const std::size_t xf = static_cast<std::size_t>(excerpt.frames());
        const double w = static_cast<double>(xf);

        // The preservation candidate is the reference every other candidate is
        // judged against, so it is rendered first.
        AudioBuffer preservation = excerpt;
        apply_restoration(preservation, candidates[0], base, 55.0);
        const std::vector<float> preservation_det = detector_of(preservation);

        std::vector<float> edet = detector_of(excerpt);
        Segmenter eseg(SegmenterConfig{excerpt.sample_rate, 10.0, 35.0});
        std::vector<FrameFeat> efeats;
        eseg.features(edet, efeats);

        for (std::size_t c = 0; c < candidates.size(); c++) {
            AudioBuffer processed = excerpt;
            apply_restoration(processed, candidates[c], base, 55.0);
            const ExcerptMetrics m =
                measure_excerpt(excerpt, processed, efeats, base.hum_freq, preservation_det);
            totals[c].quiet_reduction_db += m.quiet_reduction_db * w;
            totals[c].hum_reduction_db += m.hum_reduction_db * w;
            totals[c].speech_envelope_db += m.speech_envelope_db * w;
            totals[c].speech_band_db += m.speech_band_db * w;
            totals[c].si_sdr_db += m.si_sdr_db * w;
            totals[c].stereo_imbalance_change_db += m.stereo_imbalance_change_db * w;
            totals[c].new_clipped += m.new_clipped;
            totals[c].quiet_frames += m.quiet_frames;
            totals[c].speech_frames += m.speech_frames;
            weight[c] += w;
        }
    }

    report.evidence_confidence = confidence;
    report.candidates.reserve(candidates.size());
    for (std::size_t c = 0; c < candidates.size(); c++) {
        CandidateResult r;
        r.profile = candidates[c].profile;
        r.settings = candidates[c];
        const double w = weight[c] > 0.0 ? weight[c] : 1.0;
        r.quiet_reduction_db = totals[c].quiet_reduction_db / w;
        r.hum_reduction_db = totals[c].hum_reduction_db / w;
        r.speech_envelope_db = totals[c].speech_envelope_db / w;
        r.speech_band_db = totals[c].speech_band_db / w;
        r.speech_si_sdr_db = totals[c].si_sdr_db / w;
        r.stereo_imbalance_change_db = totals[c].stereo_imbalance_change_db / w;
        r.new_clipped = totals[c].new_clipped;

        if (r.speech_si_sdr_db < kSpeechSiSdrGateDb) {
            r.rejected = true;
            r.reject_reason = "speech damaged against the preservation render";
        } else if (r.speech_envelope_db < kSpeechEnvelopeGateDb) {
            r.rejected = true;
            r.reject_reason = "speech envelope loss";
        } else if (r.speech_band_db < kSpeechBandGateDb) {
            r.rejected = true;
            r.reject_reason = "speech band loss";
        } else if (r.stereo_imbalance_change_db > kStereoImbalanceGateDb) {
            r.rejected = true;
            r.reject_reason = "stereo imbalance change";
        } else if (r.new_clipped > 0) {
            r.rejected = true;
            r.reject_reason = "new clipped samples";
        }
        r.score_db = r.quiet_reduction_db;
        if (base.hum_present) r.score_db += 0.5 * r.hum_reduction_db;
        report.candidates.push_back(r);
    }

    // Select the greatest useful reduction that passes the gates. An
    // improvement below kQuietTieDb is a tie, and a tie goes to the weaker
    // candidate.
    std::size_t selected = 0;
    double best = report.candidates.empty() ? 0.0 : report.candidates[0].score_db;
    for (std::size_t c = 1; c < report.candidates.size(); c++) {
        const CandidateResult& r = report.candidates[c];
        if (r.rejected) continue;
        if (r.score_db - best >= kQuietTieDb) {
            selected = c;
            best = r.score_db;
        }
    }
    report.candidates[selected].selected = true;
    report.chosen = report.candidates[selected].settings;
    report.chosen.confidence = confidence;

    if (!enough_quiet) report.chosen.confidence = std::min(confidence, 0.35);
    return report;
}

AutoReport auto_clean(const AudioBuffer& in, AudioBuffer& out) {
    AutoReport report = choose_auto_settings(in);

    if (in.frames() == 0 || in.sample_rate <= 0) {
        out = in;
        return report;
    }

    RenderResult rendered = render_with_settings(in, report.chosen);
    report.full_file_speech_envelope_db = rendered.restoration_metrics.speech_envelope_db;
    report.full_file_speech_band_db = rendered.restoration_metrics.speech_band_db;
    report.full_file_stereo_imbalance_change_db =
        rendered.restoration_metrics.stereo_imbalance_change_db;
    report.full_file_new_clipped = rendered.restoration_metrics.new_clipped;

    // One weaker full-length fallback, from the original PCM. Never cascade
    // renders and never denoise already processed audio.
    const bool gate_failed = report.full_file_speech_envelope_db < kFullFileEnvelopeGateDb ||
                             report.full_file_speech_band_db < kFullFileBandGateDb ||
                             report.full_file_stereo_imbalance_change_db > kStereoImbalanceGateDb ||
                             report.full_file_new_clipped > 0;
    if (gate_failed) {
        // Find the next weaker non-rejected candidate.
        int current = -1;
        for (std::size_t c = 0; c < report.candidates.size(); c++)
            if (report.candidates[c].selected) current = static_cast<int>(c);
        for (int c = current - 1; c >= 0; c--) {
            if (report.candidates[static_cast<std::size_t>(c)].rejected) continue;
            report.chosen = report.candidates[static_cast<std::size_t>(c)].settings;
            report.chosen.confidence = report.evidence_confidence;
            report.full_file_fallback = true;
            report.fallback_reason = "full-file preservation gate failed; fell back to " +
                                     report.chosen.profile;
            report.candidates[static_cast<std::size_t>(current)].selected = false;
            report.candidates[static_cast<std::size_t>(c)].selected = true;
            rendered = render_with_settings(in, report.chosen);
            report.full_file_speech_envelope_db = rendered.restoration_metrics.speech_envelope_db;
            report.full_file_speech_band_db = rendered.restoration_metrics.speech_band_db;
            report.full_file_stereo_imbalance_change_db =
                rendered.restoration_metrics.stereo_imbalance_change_db;
            report.full_file_new_clipped = rendered.restoration_metrics.new_clipped;
            break;
        }
    }

    out = std::move(rendered.audio);
    return report;
}

namespace {

void write_analysis_json(JsonWriter& j, const Analysis& a) {
    j.begin_object();
    j.member("sample_rate", a.sample_rate);
    j.member("channels", a.channels);
    j.member("duration_s", a.duration);
    j.member("integrated_lufs", a.integrated_lufs);
    j.member("loudness_range_lu", a.lra);
    j.member("true_peak_dbtp", a.true_peak_db);
    j.member("sample_peak_dbfs", a.sample_peak_db);
    j.member("noise_floor_db", a.noise_floor_db);
    j.member("noise_avg_db", a.noise_avg_db);
    j.member("noise_regions", a.noise_regions);
    j.member("hum_freq_hz", a.hum_freq);
    j.member("hum_level_db", a.hum_db);
    j.member("hum_present", a.hum_present);
    j.member("dc_offset", a.dc_offset);
    j.member("clipped_samples", static_cast<long long>(a.clipped_samples));
    j.member("rt60_s", a.rt60);
    j.member("speech_ratio", a.speech_ratio);
    j.member("music_ratio", a.music_ratio);
    j.member("silence_ratio", a.silence_ratio);
    j.member("sibilance_db", a.sibilance_db);
    j.member("active_speech_db", a.speech_activity_db);
    j.member("channel_imbalance_db", a.channel_imbalance_db);
    j.member("stereo_correlation", a.stereo_correlation);
    j.key("channel_rms_db");
    j.begin_array();
    for (double v : a.channel_rms_db) j.value_double_inline(v);
    j.end_array();
    j.end_object();
}

void write_settings_json(JsonWriter& j, const AutoSettings& s) {
    j.begin_object();
    j.member("profile", s.profile);
    j.member("denoise_strength", s.denoise_strength);
    j.member("dereverb", s.dereverb);
    j.member("dereverb_strength", s.dereverb_strength);
    j.member("deess", s.deess);
    j.member("deess_amount", s.deess_amount);
    j.member("dehum", s.dehum);
    j.member("adaptive_highpass", s.adaptive_highpass);
    j.member("declick", s.declick);
    j.member("leveler", s.leveler);
    j.member("leveler_strength", s.leveler_strength);
    j.member("compress", s.compress);
    j.member("compressor_threshold_db", s.compressor_threshold_db);
    j.member("compressor_ratio", s.compressor_ratio);
    j.member("confidence", s.confidence);
    j.end_object();
}

}  // namespace

std::string AutoReport::to_json() const {
    JsonWriter j(2);
    j.begin_object();
    j.member("tool", std::string("podcast-sanity"));
    j.member("version", version);
    j.member("policy_version", policy_version);
    j.member("evidence_confidence", evidence_confidence);
    j.member("full_file_fallback", full_file_fallback);
    if (!fallback_reason.empty()) j.member("fallback_reason", fallback_reason);
    j.key("analysis");
    write_analysis_json(j, before);
    j.key("excerpts");
    j.begin_array();
    for (const auto& e : excerpts) {
        j.begin_object();
        j.member("start_s", e.start_s);
        j.member("end_s", e.end_s);
        j.member("reason", e.reason);
        j.end_object();
    }
    j.end_array();
    j.key("chosen");
    write_settings_json(j, chosen);
    j.key("candidates");
    j.begin_array();
    for (const auto& c : candidates) {
        j.begin_object();
        j.member("profile", c.profile);
        j.member("rejected", c.rejected);
        if (!c.reject_reason.empty()) j.member("reject_reason", c.reject_reason);
        j.member("quiet_reduction_db", c.quiet_reduction_db);
        j.member("hum_reduction_db", c.hum_reduction_db);
        j.member("speech_envelope_db", c.speech_envelope_db);
        j.member("speech_band_db", c.speech_band_db);
        j.member("speech_si_sdr_db", c.speech_si_sdr_db);
        j.member("stereo_imbalance_change_db", c.stereo_imbalance_change_db);
        j.member("new_clipped", static_cast<long long>(c.new_clipped));
        j.member("score_db", c.score_db);
        j.member("selected", c.selected);
        j.key("settings");
        write_settings_json(j, c.settings);
        j.end_object();
    }
    j.end_array();
    j.key("full_file_check");
    j.begin_object();
    j.member("speech_envelope_db", full_file_speech_envelope_db);
    j.member("speech_band_db", full_file_speech_band_db);
    j.member("stereo_imbalance_change_db", full_file_stereo_imbalance_change_db);
    j.member("new_clipped", static_cast<long long>(full_file_new_clipped));
    j.end_object();
    j.key("warnings");
    j.begin_array();
    for (const auto& w : warnings) j.value_string(w);
    j.end_array();
    j.end_object();
    return j.str();
}

}  // namespace sanity
