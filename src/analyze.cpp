#include "sanity/clean.hpp"
#include "sanity/dsp.hpp"
#include "sanity/loudness.hpp"
#include "sanity/master.hpp"
#include "sanity/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

#include "sanity/audio.hpp"

namespace sanity {

namespace {

double percentile_of(std::vector<double> v, double p) {
    if (v.empty()) return -200.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t i = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1.0 - frac) + v[i + 1] * frac : v[i];
}

// Energy decay based RT60 estimate: after a speech offset the level decays
// exponentially, and measuring the first ~50 ms of that decay gives the slope
// before the noise floor dominates (measuring longer windows underestimates the
// decay and therefore overestimates the reverb time).
double estimate_rt60(const std::vector<float>& x, int sample_rate) {
    const int hop = segmenter_hop(sample_rate);  // 10 ms
    const int nf = static_cast<int>(x.size() / static_cast<std::size_t>(hop));
    if (nf < 40) return 0.0;
    std::vector<double> e(static_cast<std::size_t>(nf), -200.0);
    for (int t = 0; t < nf; t++) {
        double s = 0.0;
        for (int i = 0; i < hop; i++) {
            const double v = x[static_cast<std::size_t>(t) * hop + i];
            s += v * v;
        }
        const double ms = s / hop;
        e[static_cast<std::size_t>(t)] = ms > 0 ? 10.0 * std::log10(ms) : -200.0;
    }
    const double loud = percentile_of(e, 0.92);
    const double floor_db = percentile_of(e, 0.05);
    std::vector<double> decays;
    const int look = 5;  // 50 ms
    for (int t = 1; t + look + 1 < nf; t++) {
        const double a = e[static_cast<std::size_t>(t)];
        if (a < floor_db + 12.0 || a > loud - 2.0) continue;
        // A local maximum followed by a monotone-ish decay.
        if (a <= e[static_cast<std::size_t>(t - 1)]) continue;
        if (a <= e[static_cast<std::size_t>(t + 1)]) continue;
        const double b = e[static_cast<std::size_t>(t + look)];
        const double drop = a - b;
        if (drop < 3.0) continue;
        const double seconds = look * hop / static_cast<double>(sample_rate);
        double rt = 60.0 * seconds / drop;
        if (rt < 0.05) rt = 0.05;
        if (rt > 3.0) rt = 3.0;
        decays.push_back(rt);
    }
    if (decays.size() < 8) return 0.0;
    return percentile_of(decays, 0.40);
}

double band_ratio_db(const std::vector<float>& x, int sample_rate, double lo, double hi) {
    if (x.empty()) return -200.0;
    std::vector<float> band = x;
    Biquad hp = Biquad::highpass(sample_rate, lo, 0.707);
    biquad_apply_zero_phase(hp, band);
    Biquad lp = Biquad::lowpass(sample_rate, std::min(hi, sample_rate * 0.45), 0.707);
    biquad_apply_zero_phase(lp, band);
    const double be = rms(band), te = rms(x);
    if (te <= 0) return -200.0;
    return 20.0 * std::log10((be + 1e-12) / (te + 1e-12));
}

Analysis analyze_impl(const std::vector<float>& mono, int sample_rate,
                      const std::vector<FrameFeat>& feats) {
    Analysis a;
    a.sample_rate = sample_rate;
    a.channels = 1;
    a.duration = sample_rate > 0 ? static_cast<double>(mono.size()) / sample_rate : 0.0;
    if (mono.empty() || sample_rate <= 0) return a;

    const LoudnessStats m = measure_loudness(mono, sample_rate);
    a.integrated_lufs = m.integrated_lufs;
    a.lra = m.lra;
    a.true_peak_db = m.true_peak_db;
    a.sample_peak_db = m.sample_peak_db;
    a.max_momentary = m.max_momentary;
    a.max_short_term = m.max_short_term;
    a.dc_offset = m.dc_offset;
    a.clipped_samples = m.clipped_samples;

    if (!feats.empty()) {
        // Quiet regions and their levels.
        std::size_t i = 0;
        std::vector<double> region_levels;
        long long speech = 0, quiet = 0;
        while (i < feats.size()) {
            const bool is_quiet = !feats[i].speech;
            std::size_t j = i;
            double acc = 0.0;
            while (j < feats.size() && (!feats[j].speech) == is_quiet) {
                acc += feats[j].rms_db;
                j++;
            }
            const double lvl = acc / static_cast<double>(j - i);
            if (is_quiet) {
                region_levels.push_back(lvl);
                quiet += static_cast<long long>(j - i);
            } else {
                speech += static_cast<long long>(j - i);
            }
            i = j;
        }
        a.region_noise_db = region_levels;
        a.noise_regions = static_cast<int>(region_levels.size());
        if (!region_levels.empty()) {
            a.noise_floor_db = percentile_of(region_levels, 0.0);
            a.noise_avg_db = percentile_of(region_levels, 0.5);
        }
        // True silence: no speech and at or below the background level.
        long long silent = 0;
        const double sil_thr = a.noise_floor_db + 6.0;
        for (const auto& f : feats)
            if (!f.speech && f.rms_db <= sil_thr) silent++;
        const long long total = static_cast<long long>(feats.size());
        a.speech_ratio = total ? static_cast<double>(speech) / total : 0.0;
        a.silence_ratio = total ? static_cast<double>(silent) / total : 0.0;
        a.music_ratio = 0.0;  // the Talk chain does not need a music/vocal split
        (void)quiet;
    }

    // Hum detection (analysis only).
    {
        HumInfo h = dehum(const_cast<std::vector<float>&>(mono), sample_rate, feats,
                          std::max(-90.0, a.noise_floor_db), false);
        a.hum_freq = h.freq;
        a.hum_db = h.level_db;
        a.hum_present = h.freq > 0.0;
    }

    a.rt60 = estimate_rt60(mono, sample_rate);
    a.sibilance_db = band_ratio_db(mono, sample_rate, 5000.0, 8000.0);
    a.speech_activity_db = active_speech_level_db(mono, sample_rate);
    return a;
}

}  // namespace

std::string params_hash(const Params& p) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "v1|%d|%d|%.4f|%.4f|%.4f|%.4f|%.4f|%d|%.4f|%d|%d|%d|%d|%d|%d|%.4f|%d|%d|%d|%d",
                  static_cast<int>(p.preset), p.normalize ? 1 : 0, p.target_lufs,
                  p.target_true_peak_db, p.max_lra, p.max_short_term, p.max_momentary,
                  p.adaptive_leveler ? 1 : 0, p.leveler_strength, p.compress ? 1 : 0,
                  p.adaptive_highpass ? 1 : 0, p.dehum ? 1 : 0, p.declick ? 1 : 0,
                  p.deess ? 1 : 0, p.power_clean ? 1 : 0, p.denoise_strength, p.dereverb ? 1 : 0,
                  static_cast<int>(p.filler), p.out_sample_rate, p.out_channels);
    // FNV-1a 64
    unsigned long long h = 1469598103934665603ULL;
    for (const char* c = buf; *c; c++) {
        h ^= static_cast<unsigned char>(*c);
        h *= 1099511628211ULL;
    }
    std::snprintf(buf, sizeof(buf), "%016llx", h);
    return std::string(buf);
}

Analysis analyze(const std::vector<float>& mono, int sample_rate) {
    std::vector<FrameFeat> feats;
    Segmenter seg(SegmenterConfig{sample_rate, 10.0, 35.0});
    seg.features(mono, feats);
    return analyze_impl(mono, sample_rate, feats);
}

Analysis analyze(const std::vector<float>& mono, int sample_rate,
                 const std::vector<FrameFeat>& feats) {
    return analyze_impl(mono, sample_rate, feats);
}

void apply_multichannel_loudness(Analysis& a, const AudioBuffer& b) {
    a.channels = b.channels;
    if (b.channels <= 1) return;
    const LoudnessStats m = measure_loudness(b);
    a.integrated_lufs = m.integrated_lufs;
    a.lra = m.lra;
    a.true_peak_db = m.true_peak_db;
    a.sample_peak_db = m.sample_peak_db;
    a.max_momentary = m.max_momentary;
    a.max_short_term = m.max_short_term;
    a.dc_offset = m.dc_offset;
    a.clipped_samples = m.clipped_samples;
}

Analysis analyze(const AudioBuffer& b) {
    Analysis a = analyze(to_mono(b), b.sample_rate);
    apply_multichannel_loudness(a, b);
    return a;
}

}  // namespace sanity
