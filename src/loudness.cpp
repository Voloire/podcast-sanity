#include "sanity/loudness.hpp"

#include <algorithm>
#include <cmath>

#include "sanity/audio.hpp"
#include "sanity/dsp.hpp"

namespace sanity {

namespace {

// Two cascaded biquads holding K-weighting state, double precision.
struct KFilter {
    KWeighting k;
    double s1z1 = 0, s1z2 = 0, s2z1 = 0, s2z2 = 0;
    void reset() { s1z1 = s1z2 = s2z1 = s2z2 = 0; }
    inline double process(double x) {
        double y = k.s1_b0 * x + s1z1;
        s1z1 = k.s1_b1 * x - k.s1_a1 * y + s1z2;
        s1z2 = k.s1_b2 * x - k.s1_a2 * y;
        double z = k.s2_b0 * y + s2z1;
        s2z1 = k.s2_b1 * y - k.s2_a1 * z + s2z2;
        s2z2 = k.s2_b2 * y - k.s2_a2 * z;
        return z;
    }
};

double lufs_from_ms(double mean_square) {
    if (mean_square <= 0.0) return -1e30;
    return -0.691 + 10.0 * std::log10(mean_square);
}

// Mean square of a range of the K-weighted signal.
inline double ms_range(const std::vector<double>& prefix, std::size_t a, std::size_t b) {
    if (b <= a) return 0.0;
    return (prefix[b] - prefix[a]) / static_cast<double>(b - a);
}

// Gating shared by measure_loudness() and integrated_lufs(). It works on the
// energy prefix of the K-weighted signal: 400 ms blocks with 75 % overlap, the
// -70 LUFS absolute gate and the -10 LU relative gate. The -0.691 offset is
// applied exactly once, here.
double integrated_from_prefix(const std::vector<double>& prefix, std::size_t n,
                              int sample_rate) {
    const std::size_t blk = std::max<std::size_t>(1, static_cast<std::size_t>(sample_rate) * 4 / 10);
    const std::size_t step = std::max<std::size_t>(1, static_cast<std::size_t>(sample_rate) / 10);
    std::vector<double> z_abs;
    for (std::size_t a = 0; a + blk <= n; a += step) {
        const double z = ms_range(prefix, a, a + blk);
        if (lufs_from_ms(z) > -70.0) z_abs.push_back(z);
    }
    if (z_abs.empty()) return -1e30;
    double zsum = 0.0;
    for (double z : z_abs) zsum += z;
    const double rel = lufs_from_ms(zsum / static_cast<double>(z_abs.size())) - 10.0;
    double fsum = 0.0;
    std::size_t fcount = 0;
    for (double z : z_abs) {
        if (lufs_from_ms(z) > rel) {
            fsum += z;
            fcount++;
        }
    }
    if (fcount == 0) {
        fsum = zsum;
        fcount = z_abs.size();
    }
    return lufs_from_ms(fsum / static_cast<double>(fcount));
}

// K-weights the signal and fills an energy prefix.
void kweight_prefix(const std::vector<float>& mono, int sample_rate,
                    std::vector<double>& prefix) {
    KFilter kf;
    kf.k = k_weighting_for(sample_rate);
    prefix.assign(mono.size() + 1, 0.0);
    for (std::size_t i = 0; i < mono.size(); i++) {
        const double y = kf.process(mono[i]);
        prefix[i + 1] = prefix[i] + y * y;
    }
}

}  // namespace

KWeighting k_weighting_for(double fs) {
    // Derivation used by reference implementations, expressed analytically so
    // that every sample rate is supported. At 48 kHz this reproduces the
    // tabulated BS.1770-4 coefficients (checked by tests).
    KWeighting k{};
    {
        const double f0 = 1681.974450955533;
        const double G = 3.999843853973347;
        const double Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        k.s1_b0 = (Vh + Vb * K / Q + K * K) / a0;
        k.s1_b1 = 2.0 * (K * K - Vh) / a0;
        k.s1_b2 = (Vh - Vb * K / Q + K * K) / a0;
        k.s1_a1 = 2.0 * (K * K - 1.0) / a0;
        k.s1_a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444;
        const double Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        k.s2_b0 = 1.0;
        k.s2_b1 = -2.0;
        k.s2_b2 = 1.0;
        k.s2_a1 = 2.0 * (K * K - 1.0) / a0;
        k.s2_a2 = (1.0 - K / Q + K * K) / a0;
    }
    return k;
}

double true_peak_db(const std::vector<float>& mono, int sample_rate) {
    if (mono.empty()) return -1e30;
    // Sample peak is a lower bound and it also covers the first and last half
    // kernel, which the oversampler cannot see.
    double p = 0.0;
    for (float v : mono) p = std::max(p, std::fabs(static_cast<double>(v)));
    p = std::max(p, oversample_4x_peak(mono));
    (void)sample_rate;
    return p > 0 ? 20.0 * std::log10(p) : -1e30;
}

double loudness_range(const std::vector<float>& mono, int sample_rate) {
    if (mono.size() < static_cast<std::size_t>(sample_rate)) return 0.0;
    KFilter kf;
    kf.k = k_weighting_for(sample_rate);
    std::vector<double> prefix(mono.size() + 1, 0.0);
    for (std::size_t i = 0; i < mono.size(); i++) {
        const double y = kf.process(mono[i]);
        prefix[i + 1] = prefix[i] + y * y;
    }
    const std::size_t win = static_cast<std::size_t>(sample_rate) * 3;  // 3 s
    const std::size_t step = static_cast<std::size_t>(sample_rate);     // 1 s
    if (win > mono.size()) return 0.0;
    // Gating must be done on energies, never on already offset loudness values.
    std::vector<double> z_abs, l_abs;
    for (std::size_t a = 0; a + win <= mono.size(); a += step) {
        const double z = ms_range(prefix, a, a + win);
        const double l = lufs_from_ms(z);
        if (l > -70.0) {
            z_abs.push_back(z);
            l_abs.push_back(l);
        }
    }
    if (l_abs.size() < 2) return 0.0;
    double zsum = 0.0;
    for (double z : z_abs) zsum += z;
    const double rel = lufs_from_ms(zsum / static_cast<double>(z_abs.size())) - 20.0;
    std::vector<double> g;
    for (std::size_t i = 0; i < l_abs.size(); i++)
        if (l_abs[i] > rel) g.push_back(l_abs[i]);
    if (g.size() < 2) return 0.0;
    std::sort(g.begin(), g.end());
    auto pct = [&](double p) {
        const double pos = p * static_cast<double>(g.size() - 1);
        const std::size_t i = static_cast<std::size_t>(pos);
        const double frac = pos - static_cast<double>(i);
        return i + 1 < g.size() ? g[i] * (1.0 - frac) + g[i + 1] * frac : g[i];
    };
    return std::max(0.0, pct(0.95) - pct(0.10));
}

LoudnessStats measure_loudness(const std::vector<float>& mono, int sample_rate) {
    LoudnessStats s;
    if (mono.empty() || sample_rate <= 0) return s;

    KFilter kf;
    kf.k = k_weighting_for(sample_rate);
    std::vector<double> prefix(mono.size() + 1, 0.0);
    double peak = 0.0, dc = 0.0;
    long long clipped = 0;
    for (std::size_t i = 0; i < mono.size(); i++) {
        const double x = mono[i];
        const double y = kf.process(x);
        prefix[i + 1] = prefix[i] + y * y;
        peak = std::max(peak, std::fabs(x));
        dc += x;
        if (std::fabs(x) >= 0.9999) clipped++;
    }
    s.dc_offset = dc / static_cast<double>(mono.size());
    s.clipped_samples = clipped;
    s.sample_peak_db = peak > 0 ? 20.0 * std::log10(peak) : -1e30;

    // Gating blocks per ITU-R BS.1770-4: 400 ms long with 75 % overlap, i.e. a
    // 100 ms step. Gating is performed on block energies; the -0.691 offset is
    // applied exactly once, inside integrated_from_prefix().
    s.integrated_lufs = integrated_from_prefix(prefix, mono.size(), sample_rate);
    if (s.integrated_lufs < -1e29) return s;
    s.has_signal = true;

    // Momentary (400 ms) and short term (3 s) maxima, 100 ms hop.
    const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(sample_rate) / 10);
    const std::size_t mw = static_cast<std::size_t>(sample_rate) * 4 / 10;
    const std::size_t sw = static_cast<std::size_t>(sample_rate) * 3;
    for (std::size_t a = 0; a + mw <= mono.size(); a += hop)
        s.max_momentary = std::max(s.max_momentary, lufs_from_ms(ms_range(prefix, a, a + mw)));
    for (std::size_t a = 0; a + sw <= mono.size(); a += hop) {
        const double l = lufs_from_ms(ms_range(prefix, a, a + sw));
        if (l <= -70.0) continue;
        s.max_short_term = std::max(s.max_short_term, l);
        s.min_short_term = std::min(s.min_short_term, l);
    }
    if (s.min_short_term > 1e29) s.min_short_term = s.integrated_lufs;

    s.true_peak_db = true_peak_db(mono, sample_rate);
    s.lra = loudness_range(mono, sample_rate);
    return s;
}

double integrated_lufs(const std::vector<float>& mono, int sample_rate) {
    if (mono.empty() || sample_rate <= 0) return -1e30;
    std::vector<double> prefix;
    kweight_prefix(mono, sample_rate, prefix);
    return integrated_from_prefix(prefix, mono.size(), sample_rate);
}

}  // namespace sanity
