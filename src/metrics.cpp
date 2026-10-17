#include "sanity/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/dsp.hpp"

namespace sanity {

namespace {

// Frame energies used by several metrics.
std::vector<double> frame_rms_db(const std::vector<float>& x, int frame, int hop) {
    std::vector<double> out;
    if (static_cast<int>(x.size()) < frame) return out;
    for (std::size_t a = 0; a + frame <= x.size(); a += static_cast<std::size_t>(hop)) {
        double s = 0.0;
        for (int i = 0; i < frame; i++) s += static_cast<double>(x[a + i]) * x[a + i];
        const double ms = s / frame;
        out.push_back(ms > 0 ? 10.0 * std::log10(ms) : -200.0);
    }
    return out;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return -200.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t i = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1.0 - frac) + v[i + 1] * frac : v[i];
}

}  // namespace

double si_sdr_db(const std::vector<float>& est, const std::vector<float>& ref) {
    const std::size_t n = std::min(est.size(), ref.size());
    if (n == 0) return -1e30;
    double dot = 0.0, rr = 0.0;
    for (std::size_t i = 0; i < n; i++) {
        dot += static_cast<double>(est[i]) * ref[i];
        rr += static_cast<double>(ref[i]) * ref[i];
    }
    if (rr <= 1e-20) return -1e30;
    const double alpha = dot / rr;
    double tn = 0.0, nn = 0.0;
    for (std::size_t i = 0; i < n; i++) {
        const double t = alpha * ref[i];
        const double e = est[i] - t;
        tn += t * t;
        nn += e * e;
    }
    if (nn <= 1e-20) return 120.0;
    return 10.0 * std::log10(tn / nn);
}

double aligned_si_sdr_db(const std::vector<float>& est, const std::vector<float>& ref,
                         int max_lag) {
    if (est.empty() || ref.empty()) return -1e30;
    // Coarse search on a decimated copy, then a full rate refinement: the
    // correlation surface is smooth, so this finds the same peak far cheaper.
    const int dec = 8;
    auto best_lag = [&](int lo, int hi, int step) {
        int best = 0;
        double best_v = -1e300;
        for (int lag = lo; lag <= hi; lag += step) {
            double num = 0.0, a = 0.0, b = 0.0;
            // Correlate only over the overlap.
            const long long n = static_cast<long long>(std::min(est.size(), ref.size()));
            for (long long i = 0; i < n; i += dec) {
                const long long j = i + lag;
                if (j < 0 || j >= static_cast<long long>(ref.size())) continue;
                if (i >= static_cast<long long>(est.size())) break;
                const double e = est[static_cast<std::size_t>(i)];
                const double r = ref[static_cast<std::size_t>(j)];
                num += e * r;
                a += e * e;
                b += r * r;
            }
            if (a <= 0.0 || b <= 0.0) continue;
            const double v = std::fabs(num) / std::sqrt(a * b);
            if (v > best_v) {
                best_v = v;
                best = lag;
            }
        }
        return best;
    };
    const int coarse = best_lag(-max_lag, max_lag, dec);
    const int fine = best_lag(std::max(-max_lag, coarse - dec), std::min(max_lag, coarse + dec), 1);
    const long long lag = fine;
    // SI-SDR with the reference shifted by `lag`.
    double dot = 0.0, rr = 0.0;
    for (std::size_t i = 0; i < est.size(); i++) {
        const long long j = static_cast<long long>(i) + lag;
        if (j < 0 || j >= static_cast<long long>(ref.size())) continue;
        dot += static_cast<double>(est[i]) * ref[static_cast<std::size_t>(j)];
        rr += static_cast<double>(ref[static_cast<std::size_t>(j)]) *
              ref[static_cast<std::size_t>(j)];
    }
    if (rr <= 1e-20) return -1e30;
    const double alpha = dot / rr;
    double tn = 0.0, nn = 0.0;
    for (std::size_t i = 0; i < est.size(); i++) {
        const long long j = static_cast<long long>(i) + lag;
        if (j < 0 || j >= static_cast<long long>(ref.size())) continue;
        const double t = alpha * ref[static_cast<std::size_t>(j)];
        const double e = est[i] - t;
        tn += t * t;
        nn += e * e;
    }
    if (nn <= 1e-20) return 120.0;
    return 10.0 * std::log10(tn / nn);
}

double active_speech_level_db(const std::vector<float>& x, int sample_rate) {
    const int frame = std::max(64, sample_rate / 50);
    const int hop = frame / 2;
    auto f = frame_rms_db(x, frame, hop);
    if (f.empty()) return -200.0;
    const double top = percentile(f, 0.95);
    std::vector<double> active;
    for (double v : f)
        if (v > top - 25.0) active.push_back(v);
    if (active.empty()) return -200.0;
    double sum = 0.0;
    for (double v : active) sum += std::pow(10.0, v / 10.0);
    return 10.0 * std::log10(sum / static_cast<double>(active.size()));
}

double noise_floor_db(const std::vector<float>& x, int sample_rate) {
    const int frame = std::max(64, sample_rate / 50);
    const int hop = frame / 2;
    auto f = frame_rms_db(x, frame, hop);
    if (f.empty()) return -200.0;
    return percentile(f, 0.10);
}

double segval(const std::vector<float>& est, const std::vector<float>& ref, int sample_rate,
              bool lsd) {
    const int frame = std::max(64, sample_rate / 50);
    const int hop = frame / 2;
    const std::size_t n = std::min(est.size(), ref.size());
    if (n < static_cast<std::size_t>(frame)) return -200.0;
    int nfft = 1;
    while (nfft < frame) nfft <<= 1;
    Fft fft(nfft);
    std::vector<std::complex<float>> a(nfft), b(nfft);
    double acc = 0.0;
    int count = 0;
    for (std::size_t off = 0; off + frame <= n; off += static_cast<std::size_t>(hop)) {
        // Only evaluate where the reference actually carries signal.
        double re = 0.0;
        for (int i = 0; i < frame; i++) re += static_cast<double>(ref[off + i]) * ref[off + i];
        if (re / frame < 1e-8) continue;
        std::fill(a.begin(), a.end(), std::complex<float>(0.0f, 0.0f));
        std::fill(b.begin(), b.end(), std::complex<float>(0.0f, 0.0f));
        for (int i = 0; i < frame; i++) {
            const float w = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / frame));
            a[i] = std::complex<float>(ref[off + i] * w, 0.0f);
            b[i] = std::complex<float>(est[off + i] * w, 0.0f);
        }
        fft.forward(a);
        fft.forward(b);
        double v = 0.0;
        int used = 0;
        for (int k = 1; k < nfft / 2; k++) {
            const double ma = std::abs(b[k]);
            const double mr = std::abs(a[k]);
            if (mr < 1e-9) continue;
            if (lsd)
                v += std::pow(20.0 * std::log10((ma + 1e-12) / mr), 2.0);
            else
                v += std::pow(std::max(0.0, 20.0 * std::log10(mr / (ma + 1e-12))), 2.0);
            used++;
        }
        if (used > 0) {
            acc += lsd ? std::sqrt(v / used) : std::sqrt(v / used);
            count++;
        }
    }
    return count ? acc / count : -200.0;
}

double segmental_snr_db(const std::vector<float>& est, const std::vector<float>& ref, int sample_rate) {
    return segval(est, ref, sample_rate, false);
}

double log_spectral_distance_db(const std::vector<float>& est, const std::vector<float>& ref, int sample_rate) {
    return segval(est, ref, sample_rate, true);
}

double IntervalScore::precision() const {
    const int d = true_positive + false_positive;
    return d > 0 ? static_cast<double>(true_positive) / d : 0.0;
}
double IntervalScore::recall() const {
    const int d = true_positive + false_negative;
    return d > 0 ? static_cast<double>(true_positive) / d : 0.0;
}
double IntervalScore::f1() const {
    const double p = precision(), r = recall();
    return (p + r) > 0 ? 2 * p * r / (p + r) : 0.0;
}

IntervalScore score_intervals(const std::vector<Interval>& detected,
                              const std::vector<Interval>& truth, int sample_rate,
                              double tolerance_ms) {
    IntervalScore s;
    s.tolerance_ms = tolerance_ms;
    const std::size_t tol = static_cast<std::size_t>(tolerance_ms * 0.001 * sample_rate);
    std::vector<bool> truth_matched(truth.size(), false);
    for (const auto& det : detected) {
        bool hit = false;
        for (std::size_t i = 0; i < truth.size(); i++) {
            if (truth_matched[i]) continue;
            // Both edges must sit on the ground truth within the tolerance, and
            // the two regions must overlap.
            if (det.start + tol >= truth[i].start && det.end <= truth[i].end + tol &&
                det.start <= truth[i].end && det.end >= truth[i].start) {
                truth_matched[i] = true;
                hit = true;
                break;
            }
        }
        if (hit) s.true_positive++;
        else s.false_positive++;
    }
    for (bool m : truth_matched)
        if (!m) s.false_negative++;
    return s;
}

double speech_preservation_db(const std::vector<float>& processed,
                              const std::vector<float>& reference, int sample_rate) {
    // Energy ratio between the processed signal and the reference over the
    // frames where the reference carries speech, after fitting a single gain.
    // A value near 0 dB means real speech survived untouched; a strongly
    // negative value means it was cut. The fit makes the result independent of
    // the level change the mastering chain applied on purpose.
    const int frame = std::max(64, sample_rate / 50);
    const int hop = frame / 2;
    const std::size_t n = std::min(processed.size(), reference.size());
    if (n < static_cast<std::size_t>(frame)) return -200.0;
    double dot = 0.0, rr = 0.0;
    for (std::size_t off = 0; off + frame <= n; off += static_cast<std::size_t>(hop)) {
        double re = 0.0, pe = 0.0;
        for (int i = 0; i < frame; i++) {
            re += static_cast<double>(reference[off + i]) * reference[off + i];
            pe += static_cast<double>(processed[off + i]) * processed[off + i];
        }
        if (re / frame < 1e-6) continue;  // reference is silent here
        dot += std::sqrt(re * pe);
        rr += re;
    }
    if (rr <= 1e-20) return -200.0;
    const double gain = dot / rr;  // energy match over speech frames
    double tn = 0.0, nn = 0.0;
    for (std::size_t off = 0; off + frame <= n; off += static_cast<std::size_t>(hop)) {
        double re = 0.0, pe = 0.0;
        for (int i = 0; i < frame; i++) {
            re += static_cast<double>(reference[off + i]) * reference[off + i];
            pe += static_cast<double>(processed[off + i]) * processed[off + i];
        }
        if (re / frame < 1e-6) continue;
        const double want = gain * std::sqrt(re);
        const double got = std::sqrt(pe);
        tn += want * want;
        nn += (got - want) * (got - want);
    }
    if (nn <= 1e-20) return 120.0;
    return 10.0 * std::log10(tn / nn);
}

}  // namespace sanity
