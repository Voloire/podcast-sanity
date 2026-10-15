#include "sanity/dsp.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <cmath>
#include <cstdint>
#include <thread>

namespace sanity {

// ----------------------------------------------------------------- biquads
double Biquad::response(double fs, double f) const {
    const std::complex<double> z = std::exp(std::complex<double>(0.0, -kTwoPi * f / fs));
    const std::complex<double> num = b0 + b1 * z + b2 * z * z;
    const std::complex<double> den = 1.0 + a1 * z + a2 * z * z;
    return std::abs(num / den);
}

Biquad Biquad::identity() { return Biquad{}; }

Biquad Biquad::highpass(double fs, double f0, double q) {
    const double w0 = kTwoPi * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = (1.0 + cw) / 2.0 / a0;
    b.b1 = -(1.0 + cw) / a0;
    b.b2 = (1.0 + cw) / 2.0 / a0;
    b.a1 = -2.0 * cw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

Biquad Biquad::lowpass(double fs, double f0, double q) {
    const double w0 = kTwoPi * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = (1.0 - cw) / 2.0 / a0;
    b.b1 = (1.0 - cw) / a0;
    b.b2 = (1.0 - cw) / 2.0 / a0;
    b.a1 = -2.0 * cw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

Biquad Biquad::peaking(double fs, double f0, double q, double gain_db) {
    const double A = std::pow(10.0, gain_db / 40.0);
    const double w0 = kTwoPi * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    Biquad b;
    b.b0 = (1.0 + alpha * A) / a0;
    b.b1 = (-2.0 * cw) / a0;
    b.b2 = (1.0 - alpha * A) / a0;
    b.a1 = (-2.0 * cw) / a0;
    b.a2 = (1.0 - alpha / A) / a0;
    return b;
}

Biquad Biquad::high_shelf(double fs, double f0, double q, double gain_db) {
    const double A = std::pow(10.0, gain_db / 40.0);
    const double w0 = kTwoPi * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double two_sqrt_a_alpha = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cw + two_sqrt_a_alpha;
    Biquad b;
    b.b0 = A * ((A + 1.0) + (A - 1.0) * cw + two_sqrt_a_alpha) / a0;
    b.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
    b.b2 = A * ((A + 1.0) + (A - 1.0) * cw - two_sqrt_a_alpha) / a0;
    b.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
    b.a2 = ((A + 1.0) - (A - 1.0) * cw - two_sqrt_a_alpha) / a0;
    return b;
}

Biquad Biquad::low_shelf(double fs, double f0, double q, double gain_db) {
    const double A = std::pow(10.0, gain_db / 40.0);
    const double w0 = kTwoPi * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double two_sqrt_a_alpha = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * cw + two_sqrt_a_alpha;
    Biquad b;
    b.b0 = A * ((A + 1.0) - (A - 1.0) * cw + two_sqrt_a_alpha) / a0;
    b.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0;
    b.b2 = A * ((A + 1.0) - (A - 1.0) * cw - two_sqrt_a_alpha) / a0;
    b.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0;
    b.a2 = ((A + 1.0) + (A - 1.0) * cw - two_sqrt_a_alpha) / a0;
    return b;
}

Biquad Biquad::notch(double fs, double f0, double q) {
    const double w0 = kTwoPi * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = 1.0 / a0;
    b.b1 = -2.0 * cw / a0;
    b.b2 = 1.0 / a0;
    b.a1 = -2.0 * cw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

void biquad_apply(Biquad& f, std::vector<float>& x) {
    f.reset();
    for (float& v : x) v = static_cast<float>(f.process(v));
}

void biquad_apply_zero_phase(Biquad& f, std::vector<float>& x) {
    f.reset();
    for (float& v : x) v = static_cast<float>(f.process(v));
    f.reset();
    for (std::size_t i = x.size(); i-- > 0;) x[i] = static_cast<float>(f.process(x[i]));
}

// --------------------------------------------------------------------- FFT
Fft::Fft(int n) : n_(n) {
    int levels = 0;
    while ((1 << levels) < n_) levels++;
    rev_.resize(n_);
    for (int i = 0; i < n_; i++) {
        int r = 0;
        for (int b = 0; b < levels; b++)
            if (i & (1 << b)) r |= 1 << (levels - 1 - b);
        rev_[i] = r;
    }
    tw_.resize(n_ / 2);
    for (int i = 0; i < n_ / 2; i++)
        tw_[i] = std::complex<float>(static_cast<float>(std::cos(-kTwoPi * i / n_)),
                                    static_cast<float>(std::sin(-kTwoPi * i / n_)));
}

void Fft::forward(std::vector<std::complex<float>>& a) const {
    for (int i = 0; i < n_; i++) {
        const int j = rev_[i];
        if (j > i) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n_; len <<= 1) {
        const int half = len / 2;
        const int step = n_ / len;
        for (int i = 0; i < n_; i += len) {
            for (int k = 0; k < half; k++) {
                const std::complex<float> w = tw_[k * step];
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + half] * w;
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
        }
    }
}

void Fft::inverse(std::vector<std::complex<float>>& a) const {
    for (auto& v : a) v = std::conj(v);
    forward(a);
    const float s = 1.0f / static_cast<float>(n_);
    for (auto& v : a) v = std::conj(v) * s;
}

// -------------------------------------------------------------------- STFT
Stft::Stft(StftConfig cfg) : cfg_(cfg), fft_(cfg.frame) {
    win_.resize(cfg_.frame);
    for (int i = 0; i < cfg_.frame; i++)
        win_[i] = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / cfg_.frame));
}

int Stft::num_frames(std::size_t n) const {
    if (n == 0) return 1;
    return static_cast<int>((n + cfg_.hop - 1) / cfg_.hop) + 1;
}

std::size_t Stft::padded_length(int nframes) const {
    if (nframes <= 0) return 0;
    return static_cast<std::size_t>(nframes - 1) * cfg_.hop + cfg_.frame;
}

void Stft::analyze(const std::vector<float>& x,
                   std::vector<std::vector<float>>& mag,
                   std::vector<std::vector<float>>& phase) const {
    const int nf = num_frames(x.size());
    mag.assign(nf, std::vector<float>(bins(), 0.0f));
    phase.assign(nf, std::vector<float>(bins(), 0.0f));
    std::vector<std::complex<float>> buf(cfg_.frame);
    for (int f = 0; f < nf; f++) {
        const std::size_t off = static_cast<std::size_t>(f) * cfg_.hop;
        for (int i = 0; i < cfg_.frame; i++) {
            const std::size_t idx = off + i;
            const float s = idx < x.size() ? x[idx] : 0.0f;
            buf[i] = std::complex<float>(s * win_[i], 0.0f);
        }
        fft_.forward(buf);
        for (int k = 0; k < bins(); k++) {
            mag[f][k] = std::abs(buf[k]);
            phase[f][k] = std::arg(buf[k]);
        }
    }
}

void Stft::synthesize(const std::vector<std::vector<float>>& mag,
                      const std::vector<std::vector<float>>& phase,
                      std::size_t out_len,
                      std::vector<float>& out) const {
    const int nf = static_cast<int>(mag.size());
    const std::size_t total = padded_length(nf);
    out.assign(total, 0.0f);
    std::vector<std::complex<float>> buf(cfg_.frame);
    for (int f = 0; f < nf; f++) {
        for (int k = 0; k < bins(); k++) {
            const float m = mag[f][k];
            const float p = phase[f][k];
            buf[k] = std::complex<float>(m * std::cos(p), m * std::sin(p));
            if (k > 0 && k < cfg_.frame - k) buf[cfg_.frame - k] = std::conj(buf[k]);
        }
        fft_.inverse(buf);
        const std::size_t off = static_cast<std::size_t>(f) * cfg_.hop;
        for (int i = 0; i < cfg_.frame; i++) out[off + i] += buf[i].real() * win_[i];
    }
    // Exact normalisation of the weighted overlap-add sum.
    std::vector<float> norm(total, 0.0f);
    for (int f = 0; f < nf; f++) {
        const std::size_t off = static_cast<std::size_t>(f) * cfg_.hop;
        for (int i = 0; i < cfg_.frame; i++) norm[off + i] += win_[i] * win_[i];
    }
    for (std::size_t i = 0; i < total; i++)
        if (norm[i] > 1e-6f) out[i] /= norm[i];
    out.resize(out_len);
}

// ---------------------------------------------------------------- resampler
namespace {

double sinc(double x) {
    if (std::fabs(x) < 1e-12) return 1.0;
    const double p = kPi * x;
    return std::sin(p) / p;
}

// 4-term Blackman-Harris window over t in [-1, 1].
double bh_window(double t) {
    const double u = 0.5 * (t + 1.0);
    return 0.35875 - 0.48829 * std::cos(kTwoPi * u) + 0.14128 * std::cos(2.0 * kTwoPi * u) -
           0.01168 * std::cos(3.0 * kTwoPi * u);
}

int gcd_int(int a, int b) {
    while (b) {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

double percentile_of(std::vector<double> v, double p) {
    if (v.empty()) return -200.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t i = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1.0 - frac) + v[i + 1] * frac : v[i];
}

}  // namespace

Resampler::Resampler(int in_rate, int out_rate, int taps_per_phase) {
    taps_ = taps_per_phase;
    if (in_rate <= 0 || out_rate <= 0) {
        up_ = down_ = 1;
    } else {
        const int g = gcd_int(in_rate, out_rate);
        up_ = out_rate / g;
        down_ = in_rate / g;
    }
    const double ratio = static_cast<double>(up_) / down_;
    const double fc = 0.5 * std::min(1.0, ratio);
    const int half = taps_ / 2;
    phases_.assign(up_, std::vector<float>(taps_, 0.0f));
    for (int p = 0; p < up_; p++) {
        const double frac = static_cast<double>(p) / up_;
        double sum = 0.0;
        for (int k = 0; k < taps_; k++) {
            const int idx = -half + 1 + k;  // relative input offset
            const double t = frac - idx;
            const double h = 2.0 * fc * sinc(2.0 * fc * t) * bh_window(t / half);
            phases_[p][k] = static_cast<float>(h);
            sum += h;
        }
        if (std::fabs(sum) > 1e-9)
            for (int k = 0; k < taps_; k++) phases_[p][k] = static_cast<float>(phases_[p][k] / sum);
    }
}

std::vector<float> Resampler::process(const std::vector<float>& in) const {
    if (up_ == down_) return in;
    const int half = taps_ / 2;
    const long long n_in = static_cast<long long>(in.size());
    // Reflect the signal at both boundaries instead of zero padding: the
    // extension stays continuous, so the outer half kernels produce no step
    // transient (zero padding puts a click in the first few output samples).
    std::vector<float> padded(in.size() + 2 * static_cast<std::size_t>(half), 0.0f);
    for (long long j = -half; j < n_in + half; j++) {
        long long src = j;
        if (src < 0) src = -src;
        if (src >= n_in) src = 2 * n_in - 2 - src;
        if (src < 0) src = 0;
        if (src >= n_in) src = n_in - 1;
        padded[static_cast<std::size_t>(j + half)] = in[static_cast<std::size_t>(src)];
    }

    const std::size_t n_out =
        static_cast<std::size_t>((static_cast<double>(in.size()) * up_) / down_);
    std::vector<float> out(n_out, 0.0f);
    const std::uint64_t off_num = static_cast<std::uint64_t>(half) * up_;
    const long long lim = static_cast<long long>(padded.size());
    for (std::size_t n = 0; n < n_out; n++) {
        const std::uint64_t t = off_num + static_cast<std::uint64_t>(n) * down_;
        const std::uint64_t base = t / up_;
        const int p = static_cast<int>(t % up_);
        const std::vector<float>& h = phases_[p];
        double acc = 0.0;
        for (int k = 0; k < taps_; k++) {
            const long long idx = static_cast<long long>(base) + (-half + 1 + k);
            if (idx < 0 || idx >= lim) continue;
            acc += h[k] * static_cast<double>(padded[static_cast<std::size_t>(idx)]);
        }
        out[n] = static_cast<float>(acc);
    }
    return out;
}

std::vector<float> resample(const std::vector<float>& in, int in_rate, int out_rate) {
    if (in_rate == out_rate || in_rate <= 0 || out_rate <= 0) return in;
    Resampler r(in_rate, out_rate, 32);
    return r.process(in);
}

double Resampler::peak(const std::vector<float>& in) const {
    const long long n_in = static_cast<long long>(in.size());
    if (n_in <= 0) return 0.0;
    // The sample peak is always a valid lower bound, and it is usually already
    // close to the true peak, which lets most windows be skipped below.
    double best = 0.0;
    for (float v : in) best = std::max(best, std::fabs(static_cast<double>(v)));
    if (up_ == down_ || n_in < 2) return best;

    const int half = taps_ / 2;
    // Upper bound of any single interpolated sample: the L1 norm of the kernel
    // times the largest input sample inside the window.
    double g = 0.0;
    for (const auto& h : phases_) {
        double s = 0.0;
        for (float c : h) s += std::fabs(static_cast<double>(c));
        g = std::max(g, s);
    }

    // Sliding window maximum of |x| over the window each output sample reads,
    // plus margin. Outside the signal the boundary is extended by repeating the
    // edge sample, which never increases any magnitude.
    const long long ext = 3LL * half;
    const long long N = n_in + 2 * ext;
    std::vector<double> a(static_cast<std::size_t>(N), 0.0);
    for (long long i = 0; i < N; i++) {
        const long long j = std::min(n_in - 1, std::max(0LL, i - ext));
        a[static_cast<std::size_t>(i)] = std::fabs(static_cast<double>(in[static_cast<std::size_t>(j)]));
    }
    const long long win = 2LL * half + 2;
    std::vector<double> wmax(static_cast<std::size_t>(N), 0.0);
    {
        std::deque<long long> dq;
        for (long long i = 0; i < N; i++) {
            while (!dq.empty() && a[static_cast<std::size_t>(dq.back())] <= a[static_cast<std::size_t>(i)])
                dq.pop_back();
            dq.push_back(i);
            while (!dq.empty() && dq.front() < i - win + 1) dq.pop_front();
            wmax[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(dq.front())];
        }
    }

    const std::size_t n_out =
        static_cast<std::size_t>((static_cast<double>(in.size()) * up_) / down_);
    const std::uint64_t off_num = static_cast<std::uint64_t>(half) * up_;
    for (std::size_t n = 0; n < n_out; n++) {
        const std::uint64_t t = off_num + static_cast<std::uint64_t>(n) * down_;
        const std::uint64_t ubase = t / up_;
        // Only windows that lie completely inside the signal are meaningful:
        // the first and last half kernel of an oversampled signal are not
        // backed by real data. Those edge samples are covered by the sample
        // peak computed above.
        if (ubase < static_cast<std::uint64_t>(2 * half) ||
            ubase >= static_cast<std::uint64_t>(n_in))
            continue;
        const long long center = static_cast<long long>(ubase) + ext;
        if (center >= 0 && center < N && wmax[static_cast<std::size_t>(center)] * g <= best)
            continue;  // this window cannot beat the value already found
        const int p = static_cast<int>(t % up_);
        const std::vector<float>& h = phases_[static_cast<std::size_t>(p)];
        double acc = 0.0;
        for (int k = 0; k < taps_; k++) {
            const long long idx = static_cast<long long>(ubase) + (-half + 1 + k) - half;
            acc += static_cast<double>(h[k]) * static_cast<double>(in[static_cast<std::size_t>(idx)]);
        }
        best = std::max(best, std::fabs(acc));
    }
    return best;
}

std::vector<float> oversample_4x(const std::vector<float>& in) {
    Resampler r(1, 4, 64);
    return r.process(in);
}

double oversample_4x_peak(const std::vector<float>& in) {
    Resampler r(1, 4, 64);
    return r.peak(in);
}

// ----------------------------------------------------------- VAD/segments
const char* seg_class_name(SegClass c) {
    switch (c) {
        case SegClass::Silence: return "silence";
        case SegClass::Noise: return "noise";
        case SegClass::Speech: return "speech";
        case SegClass::Music: return "music";
        case SegClass::Hesitation: return "hesitation";
    }
    return "unknown";
}

Segmenter::Segmenter(SegmenterConfig c) : cfg_(c) {
    hop_ = static_cast<int>(cfg_.sample_rate * cfg_.frame_ms / 1000.0);
    if (hop_ < 1) hop_ = 1;
    frame_ = hop_ * 4;
}

void Segmenter::features(const std::vector<float>& x, std::vector<FrameFeat>& out) const {
    const int nf = x.empty() ? 0 : static_cast<int>(x.size() / hop_);
    out.assign(static_cast<std::size_t>(nf), FrameFeat{});
    if (nf <= 0) return;

    int nfft = 1;
    while (nfft < frame_) nfft <<= 1;

    // Frame energy first: the activity threshold is a percentile over the whole
    // recording, so it must be known before any frame can be classified.
    std::vector<double> frame_db(static_cast<std::size_t>(nf), -200.0);
    for (int f = 0; f < nf; f++) {
        const std::size_t off = static_cast<std::size_t>(f) * hop_;
        const int n = static_cast<int>(std::min<std::size_t>(frame_, x.size() - off));
        double s = 0.0;
        for (int i = 0; i < n; i++) s += static_cast<double>(x[off + i]) * x[off + i];
        const double ms = n > 0 ? s / n : 0.0;
        frame_db[f] = ms > 0 ? 10.0 * std::log10(ms) : -200.0;
    }
    // Activity is anchored to the loud speech level (90th percentile) rather
    // than to the absolute peak: one loud cough must not turn the whole noise
    // floor into "speech".
    const double active_db = percentile_of(frame_db, 0.90) - 25.0;

    // Every frame is analysed independently, except for the spectral flux which
    // compares a frame with its predecessor. Tasks therefore have a fixed size
    // and recompute their first predecessor, so the result never depends on how
    // many threads happened to pick up which task.
    const int grain = 128;
    const int ntask = (nf + grain - 1) / grain;

    auto work = [&](int first, int last) {
        const Fft fft(nfft);
        const int bins = nfft / 2 + 1;
        std::vector<std::complex<float>> buf(static_cast<std::size_t>(nfft));
        std::vector<std::complex<float>> acf(acf_fft_.size());
        std::vector<float> mag(static_cast<std::size_t>(bins), 0.0f);
        std::vector<float> prev_mag(static_cast<std::size_t>(bins), 0.0f);

        auto spectrum = [&](int f, std::vector<float>& dst) {
            const std::size_t off = static_cast<std::size_t>(f) * hop_;
            const int n = static_cast<int>(std::min<std::size_t>(frame_, x.size() - off));
            for (int i = 0; i < n; i++) {
                const float win =
                    static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / std::max(1, n)));
                buf[static_cast<std::size_t>(i)] = std::complex<float>(x[off + i] * win, 0.0f);
            }
            for (int i = n; i < nfft; i++)
                buf[static_cast<std::size_t>(i)] = std::complex<float>(0.0f, 0.0f);
            fft.forward(buf);
            for (int k = 0; k < bins; k++)
                dst[static_cast<std::size_t>(k)] = std::abs(buf[static_cast<std::size_t>(k)]);
        };

        if (first > 0) spectrum(first - 1, prev_mag);

        for (int f = first; f < last; f++) {
            FrameFeat& ft = out[f];
            const std::size_t off = static_cast<std::size_t>(f) * hop_;
            const int n = static_cast<int>(std::min<std::size_t>(frame_, x.size() - off));
            ft.rms_db = frame_db[f];

            int zc = 0;
            for (int i = 1; i < n; i++)
                if ((x[off + i - 1] < 0.0f) != (x[off + i] < 0.0f)) zc++;
            ft.zcr = n > 1 ? static_cast<double>(zc) / (n - 1) : 0.0;

            spectrum(f, mag);

            double sum = 0.0, log_sum = 0.0, cent_num = 0.0, total_e = 0.0, low_e = 0.0, high_e = 0.0;
            for (int k = 0; k < bins; k++) {
                const double m = mag[static_cast<std::size_t>(k)];
                sum += m;
                log_sum += std::log(m + 1e-12);
                const double hz = static_cast<double>(k) * cfg_.sample_rate / nfft;
                cent_num += hz * m;
                const double e = m * m;
                total_e += e;
                if (hz < 300.0) low_e += e;
                if (hz > 4000.0) high_e += e;
            }
            const double eps = 1e-12;
            ft.flatness = sum > eps ? std::exp(log_sum / bins) / (sum / bins) : 0.0;
            ft.centroid = sum > eps ? cent_num / sum : 0.0;
            ft.low_ratio = total_e > eps ? low_e / total_e : 0.0;
            ft.high_ratio = total_e > eps ? high_e / total_e : 0.0;
            {
                double acc = 0.0;
                const double thr = 0.85 * total_e;
                for (int k = 0; k < bins; k++) {
                    acc += static_cast<double>(mag[static_cast<std::size_t>(k)]) * mag[static_cast<std::size_t>(k)];
                    if (acc >= thr) {
                        ft.rolloff = static_cast<double>(k) * cfg_.sample_rate / nfft;
                        break;
                    }
                }
            }
            {
                double d = 0.0, s0 = 0.0;
                for (int k = 0; k < bins; k++) {
                    const double diff = static_cast<double>(mag[static_cast<std::size_t>(k)]) -
                                        static_cast<double>(prev_mag[static_cast<std::size_t>(k)]);
                    if (diff > 0) d += diff;
                    s0 += prev_mag[static_cast<std::size_t>(k)];
                }
                ft.flux = s0 > eps ? d / s0 : 0.0;
            }
            std::copy(mag.begin(), mag.end(), prev_mag.begin());

            // Pitch via the autocorrelation of the raw frame. Windowing the
            // frame first would make the normalised autocorrelation decay with
            // lag purely because of the window, which hides the pitch period.
            // The FFT gives the same numbers as the direct sum, far faster.
            {
                constexpr int kAcfLen = 1024;
                const int alen = std::min(kAcfLen, n);
                const int min_lag = std::max(2, cfg_.sample_rate / 400);
                const int max_lag = std::min(kAcfLen - 1, cfg_.sample_rate / 60);
                double e0 = 0.0;
                std::fill(acf.begin(), acf.end(), std::complex<float>(0.0f, 0.0f));
                for (int i = 0; i < alen; i++) {
                    const double v = x[off + i];
                    acf[static_cast<std::size_t>(i)] = static_cast<float>(v);
                    e0 += v * v;
                }
                bool voiced = false;
                if (e0 > 1e-12 && max_lag > min_lag) {
                    acf_fft_.forward(acf);
                    for (auto& c : acf) c = c * std::conj(c);  // power spectrum
                    acf_fft_.inverse(acf);                     // circular autocorrelation
                    double best = 0.0, prev = 0.0;
                    int best_lag = 0;
                    for (int lag = min_lag; lag <= max_lag; lag++) {
                        const double r =
                            static_cast<double>(acf[static_cast<std::size_t>(lag)].real()) / e0;
                        if (r > best && r > prev) {  // local maximum only
                            best = r;
                            best_lag = lag;
                        }
                        prev = r;
                    }
                    if (best_lag > 0) ft.f0 = static_cast<double>(cfg_.sample_rate) / best_lag;
                    voiced = best > 0.40;
                }
                ft.voiced = voiced && ft.rms_db > active_db;
            }
            ft.speech = ft.voiced && ft.flatness < 0.45;
            if (!ft.speech && ft.rms_db > active_db && ft.high_ratio > 0.08 && ft.flatness > 0.10)
                ft.speech = true;  // candidate fricative, refined below
        }
    };

    if (ntask <= 1) {
        work(0, nf);
    } else {
        std::vector<std::thread> pool;
        bool parallel = false;
        // The work counter must outlive the workers: they are joined after the
        // block below, so a counter declared inside it would already be out of
        // scope while they still fetch from it.
        std::atomic<int> next{0};
        try {
            const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
            const int nthreads = std::min<int>(ntask, static_cast<int>(hw));
            for (int t = 0; t < nthreads; t++) {
                pool.emplace_back([&] {
                    for (;;) {
                        const int i = next.fetch_add(1);
                        if (i >= ntask) break;
                        const int a = i * grain;
                        work(a, std::min(nf, a + grain));
                    }
                });
            }
            parallel = true;
        } catch (...) {
            parallel = false;  // fall back to a single thread below
        }
        for (auto& t : pool)
            if (t.joinable()) t.join();
        if (!parallel) {
            for (int i = 0; i < ntask; i++)
                work(i * grain, std::min(nf, i * grain + grain));
        }
    }

    // Refinement pass. Broadband noise looks exactly like a fricative frame by
    // frame, so a run of unvoiced "speech" is only accepted when it is short and
    // attached to voiced speech. Everything else is background noise, which is
    // what the denoiser needs to see.
    {
        int i = 0;
        while (i < nf) {
            if (out[i].speech && !out[i].voiced) {
                int j = i;
                while (j < nf && out[j].speech && !out[j].voiced) j++;
                const bool voiced_near =
                    (i > 0 && out[i - 1].voiced) || (j < nf && out[j].voiced);
                const int dur = j - i;
                if (dur > 60 || (!voiced_near && dur > 25)) {
                    for (int k = i; k < j; k++) out[k].speech = false;
                }
                i = j;
            } else {
                i++;
            }
        }
    }
}

void Segmenter::segments(const std::vector<FrameFeat>& feats, std::vector<Segment>& out) const {
    out.clear();
    if (feats.empty()) return;
    double peak_db = -200.0;
    for (const auto& f : feats) peak_db = std::max(peak_db, f.rms_db);
    std::vector<double> all;
    all.reserve(feats.size());
    for (const auto& f : feats) all.push_back(f.rms_db);
    // Same anchor as the frame classifier: activity relative to the loud speech
    // level, and silence a little below the activity threshold.
    const double active_db = percentile_of(all, 0.90) - 25.0;
    const double silence_thr = active_db - 6.0;
    (void)peak_db;

    SegClass cur = SegClass::Silence;
    std::size_t start = 0;
    double lvl = 0.0;
    std::size_t cnt = 0;
    auto flush = [&](std::size_t end) {
        if (end > start) {
            Segment s;
            s.start = start;
            s.end = end;
            s.cls = cur;
            s.level_db = cnt ? lvl / static_cast<double>(cnt) : kFloorDb;
            out.push_back(s);
        }
        lvl = 0.0;
        cnt = 0;
    };
    for (std::size_t i = 0; i < feats.size(); i++) {
        const FrameFeat& f = feats[i];
        SegClass c;
        if (f.rms_db < silence_thr)
            c = SegClass::Silence;
        else if (f.speech)
            c = SegClass::Speech;
        else
            c = SegClass::Noise;
        if (c != cur) {
            flush(i);
            cur = c;
            start = i;
        }
        lvl += f.rms_db;
        cnt++;
    }
    flush(feats.size());
}

}  // namespace sanity
