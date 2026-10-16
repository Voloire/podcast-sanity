#include "sanity/dynamics.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/loudness.hpp"

namespace sanity {

namespace {

double median_of(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double percentile_of(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t i = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1.0 - frac) + v[i + 1] * frac : v[i];
}

void apply_gain_db(std::vector<float>& x, double db) {
    const double g = db_to_gain(db);
    for (float& v : x) v = static_cast<float>(v * g);
}

}  // namespace

// ------------------------------------------------------- Adaptive Leveler
LevelerInfo adaptive_leveler(std::vector<float>& x, int sample_rate,
                             const std::vector<FrameFeat>& feats, double strength) {
    LevelerInfo info;
    if (x.empty() || feats.size() < 20) return info;
    const int hop = segmenter_hop(sample_rate);

    // Group consecutive speech frames into segments and measure each segment.
    struct Seg {
        std::size_t a = 0, b = 0;
        double level_db = 0.0;
    };
    std::vector<Seg> segs;
    std::size_t i = 0;
    while (i < feats.size()) {
        if (feats[i].speech && feats[i].rms_db > -70.0) {
            std::size_t j = i;
            std::vector<double> lv;
            while (j < feats.size() && feats[j].speech) {
                lv.push_back(feats[j].rms_db);
                j++;
            }
            if (j - i >= 10) {  // at least 100 ms
                Seg s;
                s.a = i;
                s.b = j;
                s.level_db = median_of(lv);
                segs.push_back(s);
            }
            i = j;
        } else {
            i++;
        }
    }
    if (segs.size() < 3) return info;

    std::vector<double> levels;
    for (const auto& s : segs) levels.push_back(s.level_db);
    const double target = median_of(levels);
    info.target_db = target;
    info.spread_before_db = percentile_of(levels, 0.90) - percentile_of(levels, 0.10);

    // Per frame gain. Speech is moved towards the segment median; unwanted
    // material (noise, breaths, room tone) is never amplified - over those
    // frames the gain falls back to at most unity, which is exactly the
    // "segments with unwanted content are excluded from amplification" rule.
    std::vector<double> gain(feats.size(), 0.0);
    double held = 0.0;
    double max_boost = 0.0, max_cut = 0.0;
    for (std::size_t f = 0; f < feats.size(); f++) {
        double g;
        if (feats[f].speech && feats[f].rms_db > std::max(-70.0, target - 30.0)) {
            g = target - feats[f].rms_db;
            g = std::max(-15.0, std::min(18.0, g));
            held = g;
        } else {
            g = std::min(held, 0.0);
        }
        gain[f] = g;
        max_boost = std::max(max_boost, g);
        max_cut = std::min(max_cut, g);
    }
    info.max_boost_db = std::max(0.0, max_boost);
    info.max_cut_db = std::min(0.0, max_cut);
    if (info.max_boost_db - info.max_cut_db < 0.5) return info;

    // Asymmetric smoothing: fast down, slow up, so levels breathe naturally.
    const double atk = 1.0 - std::exp(-1.0 / (0.10 * 100.0));   // 100 ms
    const double rel = 1.0 - std::exp(-1.0 / (0.60 * 100.0));   // 600 ms
    double cur = gain.empty() ? 0.0 : gain[0];
    for (std::size_t f = 0; f < gain.size(); f++) {
        const double a = gain[f] < cur ? atk : rel;
        cur += a * (gain[f] - cur);
        gain[f] = cur;
    }

    const double s = std::max(0.0, std::min(1.0, strength));
    for (std::size_t f = 0; f < x.size(); f++) {
        const std::size_t fi = std::min(gain.size() - 1, f / static_cast<std::size_t>(hop));
        const std::size_t fn = std::min(gain.size() - 1, fi + 1);
        const double t = static_cast<double>(f % static_cast<std::size_t>(hop)) / hop;
        const double g_db = (gain[fi] * (1.0 - t) + gain[fn] * t) * s;
        x[f] = static_cast<float>(x[f] * db_to_gain(g_db));
    }
    if (s > 0.0) info.applied = true;

    // Spread after: re-measure the segment levels from the processed signal.
    {
        std::vector<double> after;
        for (const auto& seg : segs) {
            const std::size_t a = seg.a * static_cast<std::size_t>(hop);
            const std::size_t b = std::min(x.size(), seg.b * static_cast<std::size_t>(hop));
            if (b <= a) continue;
            double e = 0.0;
            for (std::size_t k = a; k < b; k++) e += static_cast<double>(x[k]) * x[k];
            const double ms = e / static_cast<double>(b - a);
            after.push_back(ms > 0 ? 10.0 * std::log10(ms) : -200.0);
        }
        info.spread_after_db = percentile_of(after, 0.90) - percentile_of(after, 0.10);
    }
    return info;
}

// ------------------------------------------------------------- compressor
CompressorInfo compressor(std::vector<float>& x, int sample_rate, double threshold_db,
                          double ratio, double attack_ms, double release_ms) {
    CompressorInfo info;
    if (x.empty() || ratio <= 1.0) return info;
    info.threshold_db = threshold_db;
    info.ratio = ratio;
    const double atk = 1.0 - std::exp(-1.0 / std::max(1.0, attack_ms * 0.001 * sample_rate));
    const double rel = 1.0 - std::exp(-1.0 / std::max(1.0, release_ms * 0.001 * sample_rate));
    double gr = 0.0;
    double sum = 0.0;
    for (float& v : x) {
        const double lvl = 20.0 * std::log10(std::fabs(static_cast<double>(v)) + 1e-12);
        double want = 0.0;
        if (lvl > threshold_db) want = (lvl - threshold_db) * (1.0 - 1.0 / ratio);
        if (want > gr) gr += atk * (want - gr);
        else gr += rel * (want - gr);
        sum += gr;
        v = static_cast<float>(v * db_to_gain(-gr));
    }
    info.gain_reduction_db = x.empty() ? 0.0 : sum / static_cast<double>(x.size());
    info.applied = info.gain_reduction_db > 0.05;
    return info;
}

// ---------------------------------------------------------------- limiter
LimiterInfo limiter(std::vector<float>& x, int sample_rate, double ceiling_db,
                    bool measure_peak) {
    LimiterInfo info;
    info.ceiling_db = ceiling_db;
    if (x.empty()) return info;
    const double ceiling = db_to_gain(ceiling_db);
    const std::vector<float> up = oversample_4x(x);
    const std::size_t n = up.size();
    if (n < 8) return info;

    // Required gain per oversampled sample, then a running minimum over a
    // *forward* lookahead window, so the gain is already down when the peak
    // arrives. Taking the minimum over the past instead would keep the gain
    // reduced well after every peak and cost real loudness.
    const std::size_t look = static_cast<std::size_t>(std::max(4.0, 0.003 * sample_rate * 4));
    std::vector<float> gain(n, 1.0f);
    for (std::size_t i = 0; i < n; i++) {
        const double a = std::fabs(static_cast<double>(up[i]));
        gain[i] = a > ceiling ? static_cast<float>(ceiling / a) : 1.0f;
    }
    std::vector<float> minf(n, 1.0f);
    {
        // Sliding minimum with a monotonic deque, scanning backwards.
        std::deque<std::size_t> dq;
        for (std::size_t idx = n; idx-- > 0;) {
            while (!dq.empty() && gain[dq.back()] >= gain[idx]) dq.pop_back();
            dq.push_back(idx);
            while (!dq.empty() && dq.front() > idx + look) dq.pop_front();
            minf[idx] = gain[dq.front()];
        }
    }
    // Release smoothing (attack is naturally instantaneous because of the
    // lookahead minimum).
    const double rel = 1.0 - std::exp(-1.0 / std::max(1.0, 0.050 * sample_rate * 4));
    double cur = 1.0;
    for (std::size_t i = 0; i < n; i++) {
        const double want = minf[i];
        if (want < cur) cur = want;
        else cur += rel * (want - cur);
        minf[i] = static_cast<float>(cur);
    }

    // Downsample the gain to the base rate taking the minimum of each group.
    const std::size_t base = x.size();
    std::vector<float> gbase(base, 1.0f);
    for (std::size_t i = 0; i < base; i++) {
        float g = 1.0f;
        for (int k = 0; k < 4; k++) {
            const std::size_t j = i * 4 + static_cast<std::size_t>(k);
            if (j < n) g = std::min(g, minf[j]);
        }
        gbase[i] = g;
    }
    double sum = 0.0, min_gain = 1.0;
    std::size_t affected = 0;
    for (std::size_t i = 0; i < base; i++) {
        if (gbase[i] < 0.999f) {
            sum += gbase[i];
            affected++;
            min_gain = std::min(min_gain, static_cast<double>(gbase[i]));
        }
        x[i] = static_cast<float>(x[i] * gbase[i]);
    }
    info.affected_ratio = base > 0 ? static_cast<double>(affected) / static_cast<double>(base) : 0.0;
    info.gain_reduction_db =
        affected > 0 ? -20.0 * std::log10(std::max(1e-6, sum / static_cast<double>(affected))) : 0.0;
    info.max_reduction_db = affected > 0 ? -20.0 * std::log10(std::max(1e-6, min_gain)) : 0.0;
    info.applied = affected > 0;
    if (measure_peak) info.true_peak_after_db = true_peak_db(x, sample_rate);
    return info;
}

// ------------------------------------------------- loudness normalisation
namespace {

// Slow gain reduction applied only where the short term or momentary loudness
// exceeds the configured ceiling. This is the "MaxS / MaxM" constraint that
// prevents a loud intro or a shouted passage from blowing past the target.
void enforce_short_term(std::vector<float>& x, int sample_rate, double max_st, double max_m) {
    if (x.empty() || (max_st <= 0.0 && max_m <= 0.0)) return;
    const std::size_t step = static_cast<std::size_t>(sample_rate) / 10;
    const std::size_t sw = static_cast<std::size_t>(sample_rate) * 3;
    const std::size_t mw = static_cast<std::size_t>(sample_rate) * 4 / 10;
    if (sw > x.size()) return;
    KWeighting kw = k_weighting_for(sample_rate);
    std::vector<double> prefix(x.size() + 1, 0.0);
    {
        double z1 = 0, z2 = 0, z3 = 0, z4 = 0;
        for (std::size_t i = 0; i < x.size(); i++) {
            const double s = x[i];
            double y = kw.s1_b0 * s + z1;
            z1 = kw.s1_b1 * s - kw.s1_a1 * y + z2;
            z2 = kw.s1_b2 * s - kw.s1_a2 * y;
            double v = kw.s2_b0 * y + z3;
            z3 = kw.s2_b1 * y - kw.s2_a1 * v + z4;
            z4 = kw.s2_b2 * y - kw.s2_a2 * v;
            prefix[i + 1] = prefix[i] + v * v;
        }
    }
    auto lufs = [&](std::size_t a, std::size_t b) {
        const double ms = (prefix[b] - prefix[a]) / static_cast<double>(b - a);
        return ms > 0 ? -0.691 + 10.0 * log10(ms) : -1e30;
    };
    std::vector<double> gain(x.size(), 1.0);
    for (std::size_t a = 0; a + sw <= x.size(); a += step) {
        double g = 1.0;
        const double st = lufs(a, a + sw);
        if (max_st > 0.0 && st > max_st) g = std::min(g, db_to_gain(max_st - st));
        if (mw <= x.size() - a && max_m > 0.0) {
            const double mo = lufs(a, a + mw);
            if (mo > max_m) g = std::min(g, db_to_gain(max_m - mo));
        }
        for (std::size_t i = a; i < std::min(x.size(), a + step); i++) gain[i] = g;
    }
    // Smooth so the constraint does not sound like a gate.
    const double a = 1.0 - std::exp(-1.0 / (0.40 * sample_rate));
    double cur = 1.0;
    for (std::size_t i = 0; i < x.size(); i++) {
        const double want = gain[i];
        if (want < cur) cur = want;
        else cur += a * (want - cur);
        x[i] = static_cast<float>(x[i] * cur);
    }
}

}  // namespace

LoudnessTargetInfo normalize_loudness(std::vector<float>& x, int sample_rate, double target_lufs,
                                      double true_peak_ceiling_db, double max_short_term,
                                      double max_momentary) {
    LoudnessTargetInfo info;
    info.target_lufs = target_lufs;
    if (x.empty()) return info;
    info.max_short_term_before = measure_loudness(x, sample_rate).max_short_term;

    // Stage 1: converge on the integrated loudness target. Only the integrated
    // loudness is measured here, which is cheap, so the loop can run several
    // times without the (much more expensive) true peak and LRA passes.
    double total_gain = 0.0;
    const int max_iter = 8;
    for (int it = 0; it < max_iter; it++) {
        info.iterations = it + 1;
        const double cur = integrated_lufs(x, sample_rate);
        if (cur < -1e29) break;
        const double err = target_lufs - cur;
        if (std::fabs(err) < 0.05 && it > 0) break;
        apply_gain_db(x, err);
        total_gain += err;
        enforce_short_term(x, sample_rate, max_short_term, max_momentary);
    }

    // Stage 2: peak control. Limiting removes energy, so the loudness target and
    // the true peak ceiling are solved together: limit, measure the peak and the
    // loudness, correct, and repeat. Two or three rounds are enough for the
    // result to land on the target with the peak exactly on the ceiling.
    info.true_peak_before_db = true_peak_db(x, sample_rate);
    bool gained_after_limit = false;
    for (int round = 0; round < 4; round++) {
        const LimiterInfo li = limiter(x, sample_rate, true_peak_ceiling_db, false);
        gained_after_limit = false;
        info.peak_rounds = round + 1;
        info.limiter_reduction_db = li.gain_reduction_db;
        info.limiter_max_reduction_db = li.max_reduction_db;
        const double tp = true_peak_db(x, sample_rate);
        if (tp > true_peak_ceiling_db) {
            const double fix = true_peak_ceiling_db - tp;
            apply_gain_db(x, fix);
            total_gain += fix;
        }
        info.loudness_after_limit = integrated_lufs(x, sample_rate);
        if (info.loudness_after_limit < -1e29) break;
        const double err = target_lufs - info.loudness_after_limit;
        if (std::fabs(err) < 0.05) break;
        apply_gain_db(x, err);
        total_gain += err;
        gained_after_limit = err > 0.0;
    }
    if (gained_after_limit) {
        // The loop ran out of rounds right after a gain-up: limit once more so
        // the ceiling holds, and report the residual loudness error honestly.
        const LimiterInfo li = limiter(x, sample_rate, true_peak_ceiling_db, false);
        info.limiter_reduction_db = li.gain_reduction_db;
        info.limiter_max_reduction_db = std::max(info.limiter_max_reduction_db, li.max_reduction_db);
        const double tp = true_peak_db(x, sample_rate);
        if (tp > true_peak_ceiling_db) {
            apply_gain_db(x, true_peak_ceiling_db - tp);
            total_gain += true_peak_ceiling_db - tp;
        }
        info.peak_rounds++;
    }

    const LoudnessStats fin = measure_loudness(x, sample_rate);
    info.achieved_lufs = fin.integrated_lufs;
    info.error_lu = fin.integrated_lufs - target_lufs;
    info.max_short_term_after = fin.max_short_term;
    info.gain_applied_db = total_gain;
    info.applied = true;
    return info;
}

}  // namespace sanity
