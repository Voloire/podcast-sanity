// podcast-sanity - deterministic DSP primitives
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "sanity/audio.hpp"

namespace sanity {

// ------------------------------------------------------------------ biquad
// Transposed direct form II, double precision state.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    inline double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    // Frequency response magnitude at f (Hz), for tests.
    double response(double fs, double f) const;

    static Biquad identity();
    static Biquad highpass(double fs, double f0, double q);
    static Biquad lowpass(double fs, double f0, double q);
    static Biquad peaking(double fs, double f0, double q, double gain_db);
    static Biquad high_shelf(double fs, double f0, double q, double gain_db);
    static Biquad low_shelf(double fs, double f0, double q, double gain_db);
    static Biquad notch(double fs, double f0, double q);
};

void biquad_apply(Biquad& f, std::vector<float>& x);
// Forward then backward pass: linear phase, no group-delay asymmetry
// (the technique Auphonic documents for adaptive high-pass filtering).
void biquad_apply_zero_phase(Biquad& f, std::vector<float>& x);

// --------------------------------------------------------------------- FFT
// Iterative radix-2 Cooley-Tukey.
class Fft {
public:
    explicit Fft(int n);
    void forward(std::vector<std::complex<float>>& a) const;
    void inverse(std::vector<std::complex<float>>& a) const;
    int size() const { return n_; }

private:
    int n_ = 0;
    std::vector<int> rev_;
    std::vector<std::complex<float>> tw_;
};

// -------------------------------------------------------------------- STFT
// Weighted overlap-add with an exactly normalised window sum, so that
// analysis -> synthesis with unmodified magnitudes is transparent.
struct StftConfig {
    int frame = 2048;
    int hop = 512;
};

class Stft {
public:
    explicit Stft(StftConfig cfg);
    const StftConfig& config() const { return cfg_; }
    int frame() const { return cfg_.frame; }
    int hop() const { return cfg_.hop; }
    int bins() const { return cfg_.frame / 2 + 1; }
    int num_frames(std::size_t n) const;
    std::size_t padded_length(int nframes) const;

    void analyze(const std::vector<float>& x,
                 std::vector<std::vector<float>>& mag,
                 std::vector<std::vector<float>>& phase) const;
    void synthesize(const std::vector<std::vector<float>>& mag,
                    const std::vector<std::vector<float>>& phase,
                    std::size_t out_len,
                    std::vector<float>& out) const;

private:
    StftConfig cfg_;
    Fft fft_;
    std::vector<float> win_;
    std::vector<float> norm_;
};

// ---------------------------------------------------------------- resampler
// Rational-ratio polyphase resampler with a Kaiser-windowed sinc prototype.
class Resampler {
public:
    Resampler(int in_rate, int out_rate, int taps_per_phase = 32);
    std::vector<float> process(const std::vector<float>& in) const;
    // Maximum absolute value of the resampled signal, without materialising it.
    // Windows whose samples cannot possibly reach the value already found are
    // skipped using the L1 norm of the kernel as an upper bound.
    double peak(const std::vector<float>& in) const;
    double ratio() const { return static_cast<double>(up_) / down_; }

private:
    int up_ = 1, down_ = 1, taps_ = 32;
    std::vector<std::vector<float>> phases_;
};

// Multiplies in_rate -> out_rate; identity when equal.
std::vector<float> resample(const std::vector<float>& in, int in_rate, int out_rate);

// ------------------------------------------------------ true peak oversample
// 4x oversampling per ITU-R BS.1770-4 Annex 2 (Kaiser-windowed sinc, 4 phases).
std::vector<float> oversample_4x(const std::vector<float>& in);

// Peak of the 4x oversampled signal. Same value as
// max(abs(oversample_4x(in))) but much cheaper: only the windows that can
// still contain the maximum are actually convolved.
double oversample_4x_peak(const std::vector<float>& in);

// ----------------------------------------------------------- VAD / segments
enum class SegClass { Silence, Noise, Speech, Music, Hesitation };
const char* seg_class_name(SegClass c);

struct Segment {
    std::size_t start = 0;  // frame index (not samples)
    std::size_t end = 0;    // exclusive
    SegClass cls = SegClass::Silence;
    double level_db = kFloorDb;
};

struct FrameFeat {
    double rms_db = kFloorDb;
    double zcr = 0;        // zero crossing rate
    double flatness = 0;   // spectral flatness 0..1
    double flux = 0;       // normalised spectral flux
    double centroid = 0;   // Hz
    double rolloff = 0;    // Hz
    double low_ratio = 0;  // energy below 300 Hz
    double high_ratio = 0; // energy above 4 kHz
    double f0 = 0;         // Hz (autocorrelation)
    double f0_stability = 0;
    bool voiced = false;
    bool speech = false;
};

struct SegmenterConfig {
    int sample_rate = 48000;
    double frame_ms = 10.0;  // frame step, i.e. hop between FrameFeat entries
    double range_db = 35.0;  // unused by the current classifier, kept for API stability
};

// FrameFeat arrays produced by Segmenter are spaced by this many samples, which
// is the contract every consumer (denoiser, leveler, filler killer) relies on.
inline int segmenter_hop(int sample_rate) {
    const int h = sample_rate / 100;  // 10 ms
    return h > 0 ? h : 1;
}

class Segmenter {
public:
    explicit Segmenter(SegmenterConfig c);
    int hop() const { return hop_; }
    int frame() const { return frame_; }
    void features(const std::vector<float>& x, std::vector<FrameFeat>& out) const;
    // Groups frames into segments by class.
    void segments(const std::vector<FrameFeat>& feats, std::vector<Segment>& out) const;

private:
    SegmenterConfig cfg_;
    int hop_ = 0;
    int frame_ = 0;
    // Pitch is read from a 1024 sample slice of each frame, zero padded to 2048
    // so that the circular autocorrelation is exact over the lag range used.
    // Computing it through the FFT keeps the whole feature pass linear-ish
    // instead of quadratic in the number of candidate lags.
    Fft acf_fft_{2048};
};

}  // namespace sanity
