// podcast-sanity - background music bed with speech-driven ducking
#include "sanity/music_bed.hpp"

#include <algorithm>
#include <cmath>

#include "sanity/dsp.hpp"
#include "sanity/metrics.hpp"

namespace sanity {

namespace {

double rms_db(const std::vector<float>& x) {
    if (x.empty()) return -1e30;
    double s = 0.0;
    for (float v : x) s += static_cast<double>(v) * v;
    return 10.0 * std::log10(s / static_cast<double>(x.size()) + 1e-30);
}

// Repeats src with an equal-power crossfade at every seam.
std::vector<float> loop_to(const std::vector<float>& src, std::size_t len, int sample_rate,
                           std::size_t& loops) {
    std::vector<float> out;
    out.reserve(len);
    loops = 0;
    if (src.empty()) return out;
    const std::size_t xf = std::min<std::size_t>(src.size() / 4, static_cast<std::size_t>(0.05 * sample_rate));
    while (out.size() < len) {
        const std::size_t avail = std::min(xf, out.size());
        const std::size_t base = out.size() - avail;
        for (std::size_t k = 0; k < avail; k++) {
            const double t = (static_cast<double>(k) + 0.5) / static_cast<double>(avail);
            out[base + k] = static_cast<float>(out[base + k] * std::cos(t * kHalfPi) +
                                               src[k] * std::sin(t * kHalfPi));
        }
        out.insert(out.end(), src.begin() + static_cast<long>(avail), src.end());
        loops++;
    }
    out.resize(len);
    return out;
}

}  // namespace

std::vector<float> render_music_bed(const std::vector<float>& speech, int sample_rate,
                                    const std::vector<FrameFeat>& feats, const AudioBuffer& music,
                                    const MusicBedOptions& o, MusicBedInfo* info) {
    MusicBedInfo local;
    MusicBedInfo& inf = info ? *info : local;
    inf = MusicBedInfo{};
    if (!o.enabled || music.data.empty() || music.sample_rate <= 0 || speech.empty() || sample_rate <= 0)
        return {};

    // 1. Music to the speech rate, mono.
    std::vector<float> m = to_mono(music);
    if (music.sample_rate != sample_rate) m = resample(m, music.sample_rate, sample_rate);
    if (m.empty()) return {};

    // 2. Reference levels: the bed is set relative to how loud the speaker is.
    inf.speech_level_db = active_speech_level_db(speech, sample_rate);
    if (inf.speech_level_db < -100.0) inf.speech_level_db = rms_db(speech);
    const double music_db = rms_db(m);
    inf.bed_level_db = inf.speech_level_db + o.bed_level_db;
    const double gain = db_to_gain(inf.bed_level_db - music_db);

    // 3. Timeline: offset, loop or trim, tail.
    const std::size_t tail = static_cast<std::size_t>(std::max(0.0, o.tail_s) * sample_rate);
    const std::size_t offset = std::min(speech.size(), static_cast<std::size_t>(std::max(0.0, o.offset_s) * sample_rate));
    const std::size_t total = speech.size() + tail;
    const std::size_t need = total - offset;
    std::vector<float> body;
    if (o.loop) {
        body = loop_to(m, need, sample_rate, inf.loops);
    } else {
        body.assign(m.begin(), m.begin() + static_cast<long>(std::min(need, m.size())));
        body.resize(need, 0.0f);
        inf.loops = 1;
    }
    std::vector<float> bed(total, 0.0f);
    for (std::size_t i = 0; i < need; i++) bed[offset + i] = static_cast<float>(body[i] * gain);

    // 4. Fades (raised cosine) at the start of the bed and at the very end.
    const std::size_t fi = std::min(need, static_cast<std::size_t>(std::max(0.0, o.fade_in_s) * sample_rate));
    for (std::size_t i = 0; i < fi; i++)
        bed[offset + i] *= static_cast<float>(0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / static_cast<double>(fi)));
    const std::size_t fo = std::min(total, static_cast<std::size_t>(std::max(0.0, o.fade_out_s) * sample_rate));
    for (std::size_t i = 0; i < fo; i++)
        bed[total - 1 - i] *= static_cast<float>(0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / static_cast<double>(fo)));

    // 5. Ducking: per 10 ms frame, target gain is duck_db while speech is on
    //    (plus a hold), 0 dB otherwise; asymmetric smoothing; linear per sample.
    const int hop = segmenter_hop(sample_rate);
    const std::size_t nf = total / static_cast<std::size_t>(hop) + 1;
    std::vector<double> target(nf, 0.0);
    const int hold_frames = static_cast<int>(std::max(0.0, o.hold_ms) / 10.0);
    int hold = 0;
    std::size_t ducked = 0;
    for (std::size_t f = 0; f < nf; f++) {
        const bool talking = f < feats.size() && feats[f].speech;
        if (talking) hold = hold_frames;
        else if (hold > 0) hold--;
        target[f] = (talking || hold > 0) ? o.duck_db : 0.0;
        if (target[f] < 0.0) ducked++;
    }
    inf.duck_ratio = nf ? static_cast<double>(ducked) / static_cast<double>(nf) : 0.0;
    const double atk = 1.0 - std::exp(-10.0 / std::max(1.0, o.attack_ms));
    const double rel = 1.0 - std::exp(-10.0 / std::max(1.0, o.release_ms));
    std::vector<double> env(nf, 0.0);
    double cur = 0.0;
    for (std::size_t f = 0; f < nf; f++) {
        cur += (target[f] < cur ? atk : rel) * (target[f] - cur);
        env[f] = cur;
    }
    for (std::size_t i = 0; i < total; i++) {
        const std::size_t f = i / static_cast<std::size_t>(hop);
        const std::size_t g = std::min(nf - 1, f + 1);
        const double t = static_cast<double>(i % static_cast<std::size_t>(hop)) / hop;
        bed[i] = static_cast<float>(bed[i] * db_to_gain(env[f] * (1.0 - t) + env[g] * t));
    }

    inf.frames_added = tail;
    inf.applied = true;
    return bed;
}

MusicBedInfo mix_music_bed(std::vector<float>& speech, int sample_rate,
                           const std::vector<FrameFeat>& feats, const AudioBuffer& music,
                           const MusicBedOptions& o) {
    MusicBedInfo info;
    const std::vector<float> bed = render_music_bed(speech, sample_rate, feats, music, o, &info);
    if (!info.applied) return info;
    speech.resize(bed.size(), 0.0f);
    for (std::size_t i = 0; i < bed.size(); i++) speech[i] += bed[i];
    return info;
}

}  // namespace sanity
