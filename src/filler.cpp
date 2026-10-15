#include "sanity/dsp.hpp"
#include "sanity/master.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "sanity/audio.hpp"

namespace sanity {

namespace {

double mean_of(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}

double stdev_of(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double m = mean_of(v);
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

double percentile_of(std::vector<double> v, double p) {
    if (v.empty()) return -200.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t i = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1.0 - frac) + v[i + 1] * frac : v[i];
}

// Thresholds calibrated on the synthetic corpus (tests/e2e_main.cpp): the
// cautious threshold keeps every hesitation in all three degradation profiles
// while rejecting at least 80 % of the look-alikes. See docs/TESTING.md.
double threshold_for(FillerMode mode) {
    switch (mode) {
        case FillerMode::Cautious: return 0.64;
        case FillerMode::Balanced: return 0.54;
        case FillerMode::Aggressive: return 0.42;
        case FillerMode::Off: break;
    }
    return 2.0;
}

}  // namespace

std::vector<FillerHit> detect_fillers(const std::vector<float>& mono, int sample_rate,
                                      FillerMode mode,
                                      std::vector<FillerCandidate>* candidates) {
    std::vector<FillerHit> hits;
    if (candidates) candidates->clear();
    if (mode == FillerMode::Off || mono.size() < static_cast<std::size_t>(sample_rate) / 2)
        return hits;

    Segmenter seg(SegmenterConfig{sample_rate, 10.0, 35.0});
    std::vector<FrameFeat> feats;
    seg.features(mono, feats);
    if (feats.size() < 20) return hits;

    std::vector<double> levels;
    levels.reserve(feats.size());
    for (const auto& f : feats) levels.push_back(f.rms_db);
    const double peak_db = percentile_of(levels, 0.98);
    const double floor_db = percentile_of(levels, 0.10);
    // Material below this is a pause, not speech.
    const double pause_thr = std::max(floor_db + 6.0, peak_db - 40.0);

    const std::size_t n = feats.size();
    const std::size_t hop = static_cast<std::size_t>(segmenter_hop(sample_rate));
    std::size_t i = 0;
    while (i < n) {
        // Segments are runs of *audible* material. Segmenting on voicing instead
        // would break every word into two-frame fragments, because the voiced
        // flag flickers at consonant and pitch-period boundaries.
        if (feats[i].rms_db <= pause_thr) {
            i++;
            continue;
        }
        std::size_t j = i;
        while (j < n && feats[j].rms_db > pause_thr) j++;

        const double dur_ms = (j - i) * 10.0;
        if (dur_ms >= 90.0) {
            // Surrounding pauses, measured on level so that a fricative or a
            // plosive is never mistaken for silence.
            std::size_t pa = 0;
            for (std::size_t k = i; k-- > 0 && feats[k].rms_db <= pause_thr;) pa++;
            std::size_t pb = 0;
            for (std::size_t k = j; k < n && feats[k].rms_db <= pause_thr; k++) pb++;
            const double pause_before_ms = pa * 10.0;
            const double pause_after_ms = pb * 10.0;

            std::vector<double> f0s, fluxes, highs;
            std::size_t voiced_frames = 0;
            for (std::size_t k = i; k < j; k++) {
                if (feats[k].voiced) {
                    voiced_frames++;
                    if (feats[k].f0 > 0) f0s.push_back(feats[k].f0);
                }
                fluxes.push_back(feats[k].flux);
                highs.push_back(feats[k].high_ratio);
            }
            const std::size_t len = j - i;
            const double voiced_ratio = static_cast<double>(voiced_frames) / static_cast<double>(len);
            const double f0_std = stdev_of(f0s);
            const double flux = mean_of(fluxes);
            const double high = mean_of(highs);

            // A hesitation is one sustained vowel: it is long, it is voiced
            // throughout, its pitch does not move, its spectrum is steady and it
            // carries no consonant energy. A normal word fails at least one of
            // those tests, and it is usually not isolated between two pauses.
            const double s_dur = (dur_ms >= 150.0 && dur_ms <= 900.0) ? 1.0
                                 : ((dur_ms >= 110.0 && dur_ms <= 1100.0) ? 0.6 : 0.0);
            const double s_iso = std::min(1.0, std::min(pause_before_ms, pause_after_ms) / 150.0);
            const double s_voiced = std::max(0.0, std::min(1.0, (voiced_ratio - 0.55) / 0.40));
            const double s_f0 = f0_std < 3.0 ? 1.0 : (f0_std < 7.0 ? 0.5 : 0.0);
            const double s_flux = flux < 0.10 ? 1.0 : (flux < 0.20 ? 0.5 : 0.0);
            const double s_cons = high < 0.02 ? 1.0 : (high < 0.06 ? 0.5 : 0.0);

            const double score = 0.20 * s_dur + 0.25 * s_iso + 0.20 * s_voiced +
                                 0.15 * s_f0 + 0.12 * s_flux + 0.08 * s_cons;

            const bool remove = score >= threshold_for(mode);
            if (candidates) {
                FillerCandidate c;
                c.start = i * hop;
                c.end = std::min(mono.size(), j * hop);
                c.score = score;
                c.duration_ms = dur_ms;
                c.removed = remove;
                candidates->push_back(c);
            }
            if (remove) {
                FillerHit h;
                h.start = i * hop;
                h.end = std::min(mono.size(), j * hop);
                h.score = score;
                h.duration_ms = dur_ms;
                hits.push_back(h);
            }
        }
        i = j;
    }

    // Merge hits that are closer than 60 ms so a single hesitation is not cut
    // twice, and drop hits that would cut too little.
    std::vector<FillerHit> merged;
    for (const auto& h : hits) {
        if (!merged.empty() && h.start <= merged.back().end + static_cast<std::size_t>(0.06 * sample_rate)) {
            if (h.end > merged.back().end) {
                merged.back().end = h.end;
                merged.back().score = std::max(merged.back().score, h.score);
                merged.back().duration_ms = (merged.back().end - merged.back().start) * 1000.0 / sample_rate;
            }
        } else {
            merged.push_back(h);
        }
    }
    return merged;
}

std::vector<float> remove_fillers(const std::vector<float>& mono, int sample_rate,
                                  const std::vector<FillerHit>& hits, double crossfade_ms) {
    if (hits.empty() || mono.empty()) return mono;
    const std::size_t xf = static_cast<std::size_t>(std::max(1.0, crossfade_ms * 0.001 * sample_rate));

    std::vector<float> out;
    out.reserve(mono.size());
    std::size_t cursor = 0;
    for (const auto& h : hits) {
        const std::size_t s = std::min(h.start, mono.size());
        const std::size_t e = std::min(h.end, mono.size());
        if (e <= s || s < cursor) continue;
        // Keep everything up to the cut.
        out.insert(out.end(), mono.begin() + static_cast<long>(cursor), mono.begin() + static_cast<long>(s));
        // Equal power crossfade between the material before and after the cut.
        const std::size_t avail = std::min({xf, out.size(), mono.size() - e});
        if (avail > 1) {
            const std::size_t base = out.size() - avail;
            for (std::size_t k = 0; k < avail; k++) {
                const double t = (static_cast<double>(k) + 0.5) / static_cast<double>(avail);
                const double w1 = std::cos(t * kPi * 0.5);
                const double w2 = std::sin(t * kPi * 0.5);
                out[base + k] = static_cast<float>(out[base + k] * w1 + mono[e + k] * w2);
            }
            cursor = e + avail;
        } else {
            cursor = e;
        }
    }
    if (cursor < mono.size())
        out.insert(out.end(), mono.begin() + static_cast<long>(cursor), mono.end());
    return out;
}

}  // namespace sanity
