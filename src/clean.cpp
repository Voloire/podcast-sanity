#include "sanity/clean.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/loudness.hpp"

namespace sanity {

StftConfig stft_for(int sample_rate) {
    int frame = 1024;
    if (sample_rate >= 88200)
        frame = 2048;
    else if (sample_rate <= 16000)
        frame = 512;
    else if (sample_rate <= 24000)
        frame = 1024;
    return StftConfig{frame, frame / 4};
}

namespace {

double median_of(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const std::size_t n = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(n), v.end());
    return v[n];
}

double percentile_of(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t i = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1.0 - frac) + v[i + 1] * frac : v[i];
}

// Time regions with no wanted content, used to extract noise prints.
struct QuietRegion {
    std::size_t begin_sample = 0;
    std::size_t end_sample = 0;
};

std::vector<QuietRegion> quiet_regions(const std::vector<FrameFeat>& feats, int sample_rate,
                                       double min_ms) {
    std::vector<QuietRegion> out;
    const std::size_t hop = static_cast<std::size_t>(segmenter_hop(sample_rate));
    const std::size_t min_frames = static_cast<std::size_t>(min_ms / 10.0);
    std::size_t i = 0;
    while (i < feats.size()) {
        if (!feats[i].speech) {
            std::size_t j = i;
            while (j < feats.size() && !feats[j].speech) j++;
            if (j - i >= min_frames) out.push_back({i * hop, j * hop});
            i = j;
        } else {
            i++;
        }
    }
    return out;
}

}  // namespace

// ------------------------------------------------------------------ declick
// Clicks are single sample impulses (a bump on the cable, a dropped buffer).
// They are found with a three point median predictor: the median of the three
// samples around a click still sits on the waveform, so the residual is large
// exactly at the impulse. Comparing that residual with its *local* mean rather
// than a global one is what keeps fricatives, plosive onsets and the noise floor
// from being mistaken for damage; a global scale is hopeless here because the
// residual of speech spans two orders of magnitude.
std::size_t declick(std::vector<float>& x, int sample_rate) {
    const std::size_t n = x.size();
    if (n < 16) return 0;

    std::vector<double> e(n, 0.0);
    double peak = 0.0;
    for (std::size_t i = 0; i < n; i++) peak = std::max(peak, std::fabs(static_cast<double>(x[i])));
    for (std::size_t i = 1; i + 1 < n; i++) {
        const double a = x[i - 1], b = x[i], c = x[i + 1];
        const double med = std::max(std::min(a, b), std::min(std::max(a, b), c));
        e[i] = std::fabs(b - med);
    }
    if (peak <= 1e-9) return 0;

    // Local mean of the residual, 25 ms either side.
    const std::size_t w = static_cast<std::size_t>(std::max(64.0, 0.025 * sample_rate));
    std::vector<double> pre(n + 1, 0.0);
    for (std::size_t i = 0; i < n; i++) pre[i + 1] = pre[i] + e[i];

    const double floor_level = 0.02 * peak;
    std::vector<bool> bad(n, false);
    std::size_t count = 0;
    for (std::size_t i = 1; i + 1 < n; i++) {
        const std::size_t a = i > w ? i - w : 0;
        const std::size_t b = std::min(n, i + w + 1);
        const double local = (pre[b] - pre[a]) / static_cast<double>(b - a);
        const double thr = std::max(25.0 * local, floor_level);
        if (e[i] > thr) {
            const std::size_t lo = i >= 2 ? i - 2 : 0;
            const std::size_t hi = std::min(n - 1, i + 2);
            for (std::size_t k = lo; k <= hi; k++)
                if (!bad[k]) {
                    bad[k] = true;
                    count++;
                }
        }
    }
    if (count == 0) return 0;

    // Repair each run by cubic Hermite interpolation between the good edges.
    // Runs longer than a few milliseconds are left alone: they are clipping
    // plateaus or dropouts, not clicks, and interpolating them would replace
    // real audio with a guess.
    const std::size_t max_run = static_cast<std::size_t>(std::max(8.0, 0.004 * sample_rate));
    std::size_t repaired = 0;
    std::size_t i = 0;
    while (i < n) {
        if (!bad[i]) {
            i++;
            continue;
        }
        std::size_t j = i;
        while (j < n && bad[j]) j++;
        if (j - i <= max_run) {
            const std::size_t a = i >= 1 ? i - 1 : 0;
            const std::size_t b = j < n ? j : n - 1;
            if (a + 1 < b) {
                const double y0 = x[a], y1 = x[b];
                const double s0 = a >= 1 ? (x[a] - x[a - 1]) : 0.0;
                const double s1 = b + 1 < n ? (x[b + 1] - x[b]) : 0.0;
                const double len = static_cast<double>(b - a);
                for (std::size_t k = a + 1; k < b; k++) {
                    const double t = static_cast<double>(k - a) / len;
                    const double t2 = t * t, t3 = t2 * t;
                    const double h00 = 2 * t3 - 3 * t2 + 1;
                    const double h10 = t3 - 2 * t2 + t;
                    const double h01 = -2 * t3 + 3 * t2;
                    const double h11 = t3 - t2;
                    x[k] = static_cast<float>(h00 * y0 + h10 * len * s0 + h01 * y1 +
                                              h11 * len * s1);
                }
                repaired += j - i;
            }
        }
        i = j;
    }
    return repaired;
}

double remove_dc(std::vector<float>& x) {
    if (x.empty()) return 0.0;
    double mean = 0.0;
    for (float v : x) mean += v;
    mean /= static_cast<double>(x.size());
    for (float& v : x) v = static_cast<float>(v - mean);
    return mean;
}

// -------------------------------------------------------------------- dehum
namespace {

// Magnitude of the signal at a frequency, from the noise-only regions.
double tone_level_db(const std::vector<float>& x, int fs, double f, std::size_t a, std::size_t b) {
    if (b <= a || a >= x.size() || f <= 0.0 || f >= fs * 0.5) return -1e30;
    const double w = kTwoPi * f / fs;
    const double cw = std::cos(w), sw = std::sin(w);
    double re = 1.0, im = 0.0;
    double acc_re = 0.0, acc_im = 0.0;
    const std::size_t n = std::min(b, x.size()) - a;
    for (std::size_t i = 0; i < n; i++) {
        acc_re += x[a + i] * re;
        acc_im += x[a + i] * im;
        const double nr = re * cw - im * sw;
        im = re * sw + im * cw;
        re = nr;
    }
    const double mag = std::sqrt(acc_re * acc_re + acc_im * acc_im) * 2.0 / static_cast<double>(n);
    return mag > 0 ? 20.0 * std::log10(mag + 1e-20) : -1e30;
}

}  // namespace

HumInfo dehum(std::vector<float>& x, int sample_rate, const std::vector<FrameFeat>& feats,
              double noise_floor_db, bool apply) {
    HumInfo info;
    if (x.empty()) return info;
    const auto regions = quiet_regions(feats, sample_rate, 200.0);
    std::size_t a = 0, b = x.size();
    if (!regions.empty()) {
        // Use the longest quiet region.
        std::size_t best = 0;
        for (std::size_t i = 1; i < regions.size(); i++)
            if (regions[i].end_sample - regions[i].begin_sample >
                regions[best].end_sample - regions[best].begin_sample)
                best = i;
        a = regions[best].begin_sample;
        b = regions[best].end_sample;
    }
    // Frequency resolution of the measurement region: a short region has a wide
    // spectral main lobe, so the probes have to spread out with it.
    const double res =
        static_cast<double>(sample_rate) / static_cast<double>(std::max<std::size_t>(1, b - a));
    auto hum_strength = [&](double f) { return tone_level_db(x, sample_rate, f, a, b); };
    // A mains tone is only real if it stands out from its own neighbourhood:
    // comparing it with the broadband noise floor fails whenever the recording
    // is noisy (the floor masks the tone even though it is clearly audible).
    auto excess = [&](double f) {
        if (f <= 0.0 || f >= sample_rate * 0.45) return -1e30;
        const double tone = hum_strength(f);
        if (tone < -150.0) return -1e30;
        double near = -1e30;
        // Every probe sits at least two bins away from the tone, where the
        // window leakage of the tone itself has died down.
        for (double d : {std::max(3.0, 2.0 * res), std::max(5.0, 3.0 * res),
                         std::max(8.0, 4.0 * res), std::max(12.0, 6.0 * res)}) {
            if (f - d > 5.0) near = std::max(near, hum_strength(f - d));
            if (f + d < sample_rate * 0.45) near = std::max(near, hum_strength(f + d));
        }
        return tone - near;
    };
    const double e50 = excess(50.0);
    const double e60 = excess(60.0);
    double f0 = 0.0;
    double ef = -1e30;
    if (e50 >= e60) {
        f0 = 50.0;
        ef = e50;
    } else {
        f0 = 60.0;
        ef = e60;
    }
    info.excess_db = ef;
    if (ef < 6.0) {
        info.freq = 0.0;
        info.level_db = hum_strength(50.0) > hum_strength(60.0) ? hum_strength(50.0)
                                                               : hum_strength(60.0);
        return info;
    }
    info.freq = f0;
    info.level_db = hum_strength(f0);
    info.noise_floor_db = noise_floor_db;

    // Which harmonics carry hum?
    std::vector<int> harms;
    for (int h = 1; h <= 8; h++) {
        const double f = f0 * h;
        if (f >= sample_rate * 0.45) break;
        if (excess(f) > 4.0) harms.push_back(h);
    }
    info.harmonics = harms;
    if (!apply || harms.empty()) return info;

    for (int h : harms) {
        const double f = f0 * h;
        Biquad n = Biquad::notch(sample_rate, f, std::max(4.0, f / 4.0));
        biquad_apply_zero_phase(n, x);
    }
    info.applied = true;
    return info;
}

// ----------------------------------------------------------- adaptive high pass
// A single 12 dB/oct high pass whose corner is derived from the material: it is
// placed well below the lowest fundamental that actually occurs in the recording,
// so rumble and handling noise go away while the body of the voice stays intact.
// Cascading several zero-phase stages would be tempting, but a 24 or 36 dB/oct
// slope reaches far enough up to take several dB out of the fundamental of a low
// male voice.
HighpassInfo adaptive_highpass(std::vector<float>& x, int sample_rate,
                               const std::vector<FrameFeat>& feats, double floor_hz, bool apply) {
    HighpassInfo info;
    info.cutoff_min = floor_hz;
    info.cutoff_max = floor_hz;
    info.cutoff_used = floor_hz;
    if (x.empty() || feats.empty()) return info;

    std::vector<double> f0s;
    for (const FrameFeat& f : feats)
        if (f.speech && f.voiced && f.f0 > 0.0) f0s.push_back(f.f0);
    info.voices_measured = f0s.size();

    double cut = floor_hz;
    if (f0s.size() >= 20) {
        // The 5th percentile is the deepest voice in the take; anchoring to it
        // means the corner never rises into anybody's fundamental.
        const double f0_low = percentile_of(f0s, 0.05);
        cut = std::max(floor_hz, std::min(0.7 * f0_low, 120.0));
    }
    info.cutoff_min = cut;
    info.cutoff_max = cut;
    info.cutoff_used = cut;
    if (!apply) return info;

    Biquad hp = Biquad::highpass(sample_rate, cut, 0.707);
    biquad_apply(hp, x);
    info.applied = true;
    return info;
}

// ------------------------------------------------------------------ denoise
DenoiseInfo denoise(std::vector<float>& x, int sample_rate, const std::vector<FrameFeat>& feats,
                    double strength) {
    DenoiseInfo info;
    if (x.size() < 4096) return info;
    const StftConfig cfg = stft_for(sample_rate);
    Stft stft(cfg);
    std::vector<std::vector<float>> mag, phase;
    stft.analyze(x, mag, phase);
    const int nf = static_cast<int>(mag.size());
    const int nb = stft.bins();
    if (nf < 4 || nb < 8) return info;

    // --- noise prints, one per quiet region ---------------------------------
    const auto regions = quiet_regions(feats, sample_rate, 150.0);
    std::vector<std::vector<double>> prints;
    std::vector<double> print_pos;
    for (const auto& r : regions) {
        const int fa = static_cast<int>(r.begin_sample / cfg.hop);
        const int fb = static_cast<int>(r.end_sample / cfg.hop);
        if (fb <= fa) continue;
        std::vector<double> print(static_cast<std::size_t>(nb), 0.0);
        int used = 0;
        for (int t = std::max(0, fa); t < std::min(nf, fb); t++) {
            for (int k = 0; k < nb; k++) print[k] += static_cast<double>(mag[t][k]) * mag[t][k];
            used++;
        }
        if (used < 2) continue;
        for (double& v : print) v = std::sqrt(v / used);
        prints.push_back(std::move(print));
        print_pos.push_back(static_cast<double>(fa + fb) * 0.5);
    }
    // --- fallback: minimum statistics over the whole file -------------------
    if (prints.empty()) {
        std::vector<double> print(static_cast<std::size_t>(nb), 0.0);
        std::vector<double> col(nf);
        for (int k = 0; k < nb; k++) {
            for (int t = 0; t < nf; t++) col[t] = mag[t][k];
            print[k] = percentile_of(col, 0.10);
        }
        prints.push_back(std::move(print));
        print_pos.push_back(0.0);
        info.regions = 0;
    } else {
        info.regions = static_cast<int>(prints.size());
    }
    info.applied = true;
    info.strength = strength;

    // Noise floor from the prints.
    {
        std::vector<double> floor_vals;
        for (const auto& p : prints) {
            double sum = 0.0;
            for (double v : p) sum += v * v;
            floor_vals.push_back(10.0 * std::log10(sum / nb + 1e-20));
        }
        info.noise_floor_db = percentile_of(floor_vals, 0.0);
    }

    const double alpha = 1.0 + std::max(0.0, std::min(2.0, strength));  // over subtraction
    const double floor_db = -22.0 + 4.0 * std::max(0.0, std::min(2.0, strength));
    const double floor_lin = db_to_gain(floor_db);
    const double beta = 0.96;

    std::vector<double> prev_g(static_cast<std::size_t>(nb), 1.0);
    std::vector<double> prev_gamma(static_cast<std::size_t>(nb), 0.0);
    std::vector<double> gain_bins(static_cast<std::size_t>(nb));
    double sum_db = 0.0;
    long long count = 0;

    for (int t = 0; t < nf; t++) {
        // Nearest print in time (region dependent noise estimate).
        int best = 0;
        double bestd = 1e30;
        for (std::size_t i = 0; i < print_pos.size(); i++) {
            const double d = std::fabs(print_pos[i] - t);
            if (d < bestd) {
                bestd = d;
                best = static_cast<int>(i);
            }
        }
        const std::vector<double>& np = prints[static_cast<std::size_t>(best)];
        for (int k = 0; k < nb; k++) {
            const double n2 = np[k] * np[k] + 1e-20;
            const double y2 = static_cast<double>(mag[t][k]) * mag[t][k];
            const double gamma = y2 / n2;
            double snr = beta * prev_g[k] * prev_gamma[k] + (1.0 - beta) * std::max(gamma - alpha, 0.0);
            if (snr < 0.0) snr = 0.0;
            double g = snr / (1.0 + snr);  // Wiener gain on power
            if (g < floor_lin) g = floor_lin;
            prev_g[k] = g;
            prev_gamma[k] = gamma;
            gain_bins[k] = g;
        }
        for (int k = 0; k < nb; k++) {
            const double m = std::sqrt(gain_bins[k]);
            mag[t][k] = static_cast<float>(mag[t][k] * m);
            sum_db += 20.0 * std::log10(std::max(m, 1e-6));
            count++;
        }
    }
    info.reduction_db = count ? sum_db / static_cast<double>(count) : 0.0;

    std::vector<float> y;
    stft.synthesize(mag, phase, x.size(), y);
    x = y;
    return info;
}

// ----------------------------------------------------------------- dereverb
DereverbInfo dereverb(std::vector<float>& x, int sample_rate, const std::vector<FrameFeat>& feats,
                      double strength) {
    (void)feats;
    DereverbInfo info;
    if (x.size() < 8192) return info;
    const StftConfig cfg = stft_for(sample_rate);
    Stft stft(cfg);
    std::vector<std::vector<float>> mag, phase;
    stft.analyze(x, mag, phase);
    const int nf = static_cast<int>(mag.size());
    const int nb = stft.bins();
    if (nf < 4) return info;

    // Estimate RT60 from the decay of the total energy after speech offsets.
    {
        std::vector<double> e(nf, 0.0);
        for (int t = 0; t < nf; t++) {
            double s = 0.0;
            for (int k = 0; k < nb; k++) s += static_cast<double>(mag[t][k]) * mag[t][k];
            e[t] = 10.0 * std::log10(s + 1e-20);
        }
        double peak = -1e30;
        for (double v : e) peak = std::max(peak, v);
        std::vector<double> decays;
        for (int t = 1; t + 30 < nf; t++) {
            if (e[t] > peak - 20.0 && e[t] < peak - 8.0) {
                const int t2 = std::min(nf - 1, t + 30);
                const double drop = e[t] - e[t2];
                if (drop > 0.5) {
                    const double seconds = (t2 - t) * cfg.hop / static_cast<double>(sample_rate);
                    decays.push_back(60.0 * seconds / drop);  // time for a 60 dB drop
                }
            }
        }
        if (!decays.empty()) info.rt60_before = median_of(decays);
    }

    const double release_window_s = 0.05;  // energy older than 50 ms is late reverb
    const double decay = std::pow(10.0, -3.0 * cfg.hop /
                                            (release_window_s * sample_rate));
    const double s = std::max(0.0, std::min(1.0, strength));
    const double floor_lin = db_to_gain(-14.0 * s);
    std::vector<double> r(static_cast<std::size_t>(nb), 0.0);
    std::vector<double> prev_e(static_cast<std::size_t>(nb), 0.0);
    double sum_db = 0.0;
    long long count = 0;
    for (int t = 0; t < nf; t++) {
        for (int k = 0; k < nb; k++) {
            const double e = static_cast<double>(mag[t][k]) * mag[t][k];
            r[k] = decay * r[k] + (1.0 - decay) * prev_e[k];
            double g = 1.0 - s * (r[k] / (e + 1e-20));
            if (g < floor_lin) g = floor_lin;
            if (g > 1.0) g = 1.0;
            prev_e[k] = e;
            const double m = std::sqrt(g);
            mag[t][k] = static_cast<float>(mag[t][k] * m);
            sum_db += 20.0 * std::log10(std::max(m, 1e-6));
            count++;
        }
    }
    info.reduction_db = count ? sum_db / static_cast<double>(count) : 0.0;
    std::vector<float> y;
    stft.synthesize(mag, phase, x.size(), y);
    x = y;
    info.applied = true;
    return info;
}

// -------------------------------------------------------------------- deess
DeessInfo deess(std::vector<float>& x, int sample_rate, double amount) {
    DeessInfo info;
    if (x.size() < 2048 || amount <= 0.0) return info;
    const double hi = std::min(8000.0, sample_rate * 0.42);
    const double lo = std::min(5000.0, hi * 0.6);
    std::vector<float> band = x;
    Biquad hp = Biquad::highpass(sample_rate, lo, 0.707);
    biquad_apply_zero_phase(hp, band);
    Biquad lp = Biquad::lowpass(sample_rate, hi, 0.707);
    biquad_apply_zero_phase(lp, band);

    // Side chain: full band envelope and sibilant band envelope.
    const int rel = std::max(1, sample_rate / 20);     // 50 ms
    double env_full = 0.0, env_band = 0.0;
    double max_red = 0.0;
    double before_sum = 0.0;
    long long cnt = 0;
    std::vector<float> out(x.size());
    const double max_drop = 9.0 * std::min(1.0, amount);  // dB
    for (std::size_t i = 0; i < x.size(); i++) {
        const double af = std::fabs(static_cast<double>(x[i]));
        const double ab = std::fabs(static_cast<double>(band[i]));
        env_full = af > env_full ? af : env_full + (af - env_full) / rel;
        env_band = ab > env_band ? ab : env_band + (ab - env_band) / rel;
        const double ratio = env_band / (env_full + 1e-9);
        // Above this ratio the material is sibilant and gets tamed.
        const double knee = 0.35;
        double g = 1.0;
        if (ratio > knee) {
            const double over = std::min(1.0, (ratio - knee) / knee);
            g = db_to_gain(-max_drop * over);
        }
        const double red = 20.0 * std::log10(g);
        if (-red > max_red) max_red = -red;
        out[i] = static_cast<float>(x[i] - (1.0 - g) * band[i]);
        before_sum += ab * ab;
        cnt++;
    }
    info.max_reduction_db = max_red;
    if (before_sum > 0) info.sibilance_before_db = 10.0 * std::log10(before_sum / std::max(1LL, cnt));
    x = out;
    info.applied = true;
    return info;
}

}  // namespace sanity
