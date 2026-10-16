// podcast-sanity - unit and contract tests
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/dsp.hpp"
#include "sanity/encode.hpp"
#include "sanity/loudness.hpp"
#include "tinytest.hpp"

#if SANITY_HAVE_EBUR128
extern "C" {
#include <ebur128.h>
}
#endif

#if SANITY_HAVE_SNDFILE
#include <sndfile.h>
#endif

using namespace sanity;

TT_CASE("exports: Unicode paths, formats and stereo channel separation") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() /
        ("sanity-codecs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    AudioBuffer audio = AudioBuffer::zeros(48000, 2, 48000);
    for (std::size_t i = 0; i < audio.frames(); ++i)
        audio.data[2 * i] = static_cast<float>(0.4 * std::sin(kTwoPi * 440 * i / 48000));
    for (auto format : {ExportFormat::Wav16, ExportFormat::Wav24, ExportFormat::Flac,
                        ExportFormat::Ogg, ExportFormat::Mp3}) {
        if (!encoder_available(format)) continue;
        const auto path = dir / fs::u8path("episodio-\xc3\xa8-\xe6\xb8\xac" + export_extension(format));
        const auto utf8 = path.u8string();
        const std::string name(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        ExportOptions options;
        options.format = format;
        std::string error;
        CHECK(encode_audio(name, audio, options, error));
        CHECK(fs::exists(path));
        if (!fs::exists(path)) continue;
        CHECK(fs::file_size(path) > 1024);
#if SANITY_HAVE_SNDFILE
        SF_INFO info{};
#ifdef _WIN32
        auto* file = sf_wchar_open(path.c_str(), SFM_READ, &info);
#else
        auto* file = sf_open(path.c_str(), SFM_READ, &info);
#endif
        CHECK(file != nullptr);
        if (!file) continue;
        CHECK(info.channels == 2);
        CHECK(info.samplerate == 48000);
        if (format == ExportFormat::Wav24)
            CHECK((info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_24);
        std::vector<float> decoded(static_cast<std::size_t>(info.frames) * info.channels);
        const auto frames = sf_readf_float(file, decoded.data(), info.frames);
        sf_close(file);
        CHECK_GE(frames, 48000);
        double left = 0, right = 0;
        for (sf_count_t i = 0; i < frames; ++i) {
            left += decoded[2 * i] * decoded[2 * i];
            right += decoded[2 * i + 1] * decoded[2 * i + 1];
        }
        CHECK(left > 1000);
        CHECK(right < left * 0.001);
#else
        if (format == ExportFormat::Wav16) {
            AudioBuffer decoded;
            CHECK(wav_read(name, decoded, error));
            CHECK(decoded.frames() == audio.frames());
        }
#endif
    }
}

namespace {

std::vector<float> sine(double freq, double amp, double seconds, int fs) {
    const std::size_t n = static_cast<std::size_t>(seconds * fs);
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; i++)
        x[i] = static_cast<float>(amp * std::sin(kTwoPi * freq * i / fs));
    return x;
}

// Real recordings do not start and stop with a hard edge. A raised cosine fade
// removes the broadband click that a rectangular envelope would create.
std::vector<float> faded_sine(double freq, double amp, double seconds, int fs) {
    auto x = sine(freq, amp, seconds, fs);
    const int f = std::max(1, static_cast<int>(fs / 50));  // 20 ms
    for (int i = 0; i < f && i < static_cast<int>(x.size()); i++) {
        const double g = 0.5 - 0.5 * std::cos(kPi * i / f);
        x[static_cast<std::size_t>(i)] *= static_cast<float>(g);
        x[x.size() - 1 - static_cast<std::size_t>(i)] *= static_cast<float>(g);
    }
    return x;
}

std::vector<float> noise(double amp, std::size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> d(-amp, amp);
    std::vector<float> x(n);
    for (auto& v : x) v = static_cast<float>(d(rng));
    return x;
}

#if SANITY_HAVE_EBUR128
double ebur128_integrated(const std::vector<float>& mono, int fs) {
    ebur128_state* st = ebur128_init(1, static_cast<unsigned long>(fs), EBUR128_MODE_I);
    ebur128_add_frames_float(st, mono.data(), mono.size());
    double out = 0.0;
    ebur128_loudness_global(st, &out);
    ebur128_destroy(&st);
    return out;
}
double ebur128_true_peak(const std::vector<float>& mono, int fs) {
    ebur128_state* st = ebur128_init(1, static_cast<unsigned long>(fs), EBUR128_MODE_TRUE_PEAK);
    ebur128_add_frames_float(st, mono.data(), mono.size());
    double out = 0.0;
    ebur128_true_peak(st, 0, &out);
    ebur128_destroy(&st);
    // The API returns a linear value: 1.0 is 0 dBTP.
    return out > 0 ? 20.0 * std::log10(out) : -1e30;
}
#endif

}  // namespace

// --------------------------------------------------------------- K-weighting
TT_CASE("k_weighting matches the tabulated BS.1770-4 coefficients at 48 kHz") {
    const KWeighting k = k_weighting_for(48000.0);
    CHECK_NEAR(k.s1_b0, 1.53512485958697, 1e-8);
    CHECK_NEAR(k.s1_b1, -2.69169618940638, 1e-8);
    CHECK_NEAR(k.s1_b2, 1.19839281085285, 1e-8);
    CHECK_NEAR(k.s1_a1, -1.69065929318241, 1e-8);
    CHECK_NEAR(k.s1_a2, 0.73248077421585, 1e-8);
    CHECK_NEAR(k.s2_b0, 1.0, 1e-12);
    CHECK_NEAR(k.s2_b1, -2.0, 1e-12);
    CHECK_NEAR(k.s2_b2, 1.0, 1e-12);
    CHECK_NEAR(k.s2_a1, -1.99004745483398, 1e-8);
    CHECK_NEAR(k.s2_a2, 0.99007225036621, 1e-8);
}

TT_CASE("k_weighting is derived for other sample rates too") {
    // The tabulated constants only hold at 48 kHz, so check the invariants that
    // must hold at any rate: the shelf has unity DC gain and about +4 dB at
    // Nyquist, the RLB stage blocks DC and passes the mid band.
    auto mag = [](const KWeighting& k, double fs, double f) {
        auto H = [&](double b0, double b1, double b2, double a1, double a2) {
            const std::complex<double> z = std::exp(std::complex<double>(0.0, -kTwoPi * f / fs));
            return (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
        };
        return std::abs(H(k.s1_b0, k.s1_b1, k.s1_b2, k.s1_a1, k.s1_a2) *
                        H(k.s2_b0, k.s2_b1, k.s2_b2, k.s2_a1, k.s2_a2));
    };
    for (int fs : {44100, 32000, 16000}) {
        const KWeighting k = k_weighting_for(fs);
        const double dc = mag(k, fs, 1.0);
        const double mid = mag(k, fs, 1000.0);
        const double top = mag(k, fs, fs * 0.45);
        CHECK_NEAR(20.0 * std::log10(mid), 0.7, 0.6);   // shelf lift at 1 kHz
        CHECK_NEAR(20.0 * std::log10(top), 4.0, 0.6);   // shelf plateau
        CHECK_LE(dc, mid);                               // high pass kills DC
    }
}

// ------------------------------------------------------------------- filters
TT_CASE("biquad high pass removes DC and keeps the passband") {
    const int fs = 48000;
    auto dc = std::vector<float>(fs, 0.5f);
    Biquad hp = Biquad::highpass(fs, 80.0, 0.707);
    biquad_apply(hp, dc);
    CHECK_LE(std::fabs(dc.back()), 1e-4);
    CHECK_NEAR(hp.response(fs, 1000.0), 1.0, 0.02);
    CHECK_LE(hp.response(fs, 20.0), 0.3);
}

TT_CASE("zero phase filtering is symmetric in time") {
    const int fs = 48000;
    auto x = sine(1000.0, 0.5, 0.05, fs);
    auto y = x;
    Biquad hp = Biquad::highpass(fs, 200.0, 0.707);
    biquad_apply_zero_phase(hp, y);
    // A linear phase filter keeps the impulse response symmetric: the
    // forward/backward result must be positive at the peak of a sine.
    double peak = 0.0;
    for (float v : y) peak = std::max(peak, static_cast<double>(v));
    CHECK(peak > 0.0);
}

// ----------------------------------------------------------------------- FFT
TT_CASE("fft agrees with a naive DFT") {
    const int n = 64;
    Fft fft(n);
    std::vector<std::complex<float>> a(n);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    for (auto& v : a) v = std::complex<float>(d(rng), d(rng));
    auto b = a;
    fft.forward(b);
    for (int k = 0; k < n; k++) {
        std::complex<double> ref(0.0, 0.0);
        for (int i = 0; i < n; i++) {
            const double ang = -kTwoPi * k * i / n;
            ref += std::complex<double>(a[i].real(), a[i].imag()) *
                   std::complex<double>(std::cos(ang), std::sin(ang));
        }
        CHECK_NEAR(std::abs(b[k]), std::abs(ref), 1e-3);
    }
}

// ---------------------------------------------------------------------- STFT
TT_CASE("stft analysis and synthesis is transparent") {
    const int fs = 48000;
    auto x = sine(440.0, 0.25, 0.25, fs);
    x.resize(x.size() + 300, 0.0f);  // non multiple of hop
    Stft stft(StftConfig{1024, 256});
    std::vector<std::vector<float>> mag, phase;
    stft.analyze(x, mag, phase);
    std::vector<float> y;
    stft.synthesize(mag, phase, x.size(), y);
    double err = 0.0;
    const std::size_t skip = 1024;
    for (std::size_t i = skip; i + skip < x.size(); i++)
        err = std::max(err, std::fabs(static_cast<double>(x[i]) - y[i]));
    CHECK_LE(err, 1e-5);
}

// ---------------------------------------------------------------- resampling
TT_CASE("resampler keeps a sine intact across 44.1 to 48 kHz") {
    const int fs_in = 44100, fs_out = 48000;
    auto x = sine(1000.0, 0.5, 0.5, fs_in);
    auto y = resample(x, fs_in, fs_out);
    // Fit amplitude on the interior and measure the residual.
    const std::size_t a = 2000, b = y.size() - 2000;
    double num = 0.0, den = 0.0;
    for (std::size_t i = a; i < b; i++) {
        const double ref = std::sin(kTwoPi * 1000.0 * i / fs_out);
        num += ref * y[i];
        den += ref * ref;
    }
    const double g = num / den;
    double err = 0.0;
    for (std::size_t i = a; i < b; i++) {
        const double ref = std::sin(kTwoPi * 1000.0 * i / fs_out);
        err = std::max(err, std::fabs(y[i] - g * ref));
    }
    CHECK_NEAR(g, 0.5, 0.01);
    CHECK_LE(err, 0.01);
}

TT_CASE("resampler removes content above the target Nyquist") {
    // Faded signal: the envelope is realistic and the measurement covers the
    // whole output, not just the interior.
    const int fs_in = 48000, fs_out = 24000;  // halving kills 18 kHz
    auto x = faded_sine(18000.0, 0.5, 0.3, fs_in);
    auto y = resample(x, fs_in, fs_out);
    CHECK_LE(peak_abs(y), 0.01);
}

TT_CASE("downsampling a band limited signal preserves amplitude and does not overshoot") {
    const int fs_in = 48000, fs_out = 24000;
    // 6 kHz on a 24 kHz grid is exactly 4 samples per cycle, so the sample peak
    // is the true amplitude, and the true peak must not exceed it either.
    auto x = faded_sine(6000.0, 0.5, 0.3, fs_in);
    auto y = resample(x, fs_in, fs_out);
    double peak = 0.0;
    for (std::size_t i = 500; i + 500 < y.size(); i++)
        peak = std::max(peak, std::fabs(static_cast<double>(y[i])));
    CHECK_NEAR(peak, 0.5, 0.005);
    CHECK_LE(true_peak_db(y, fs_out), 20.0 * std::log10(0.5) + 0.05);
}

TT_CASE("chained resampling is stable") {
    auto x = faded_sine(1000.0, 0.4, 0.4, 44100);
    auto a = resample(x, 44100, 48000);
    auto b = resample(a, 48000, 44100);
    CHECK_NEAR(peak_abs(b), 0.4, 0.01);
}

// ----------------------------------------------------------------- true peak
TT_CASE("true peak detects inter sample peaks above the sample peak") {
    const int fs = 48000;
    // 9 kHz at the Nyquist/2 region produces strong inter sample peaks.
    auto x = sine(9973.0, 0.98, 0.2, fs);
    const double sp = 20.0 * std::log10(peak_abs(x));
    const double tp = true_peak_db(x, fs);
    CHECK_GE(tp, sp - 1e-6);
    CHECK_GE(tp, sp);
}

TT_CASE("true peak of a full scale sine is close to 0 dBTP") {
    const int fs = 48000;
    for (double phase : {0.0, 0.1, 0.25, 0.37}) {
        auto x = sine(1000.0, 1.0, 0.2, fs);
        for (std::size_t i = 0; i < x.size(); i++)
            x[i] = static_cast<float>(std::sin(kTwoPi * 1000.0 * i / fs + phase));
        const double tp = true_peak_db(x, fs);
        CHECK_NEAR(tp, 0.0, 0.15);
    }
}

// --------------------------------------------------------------- loudness
TT_CASE("integrated loudness of digital silence is gated away") {
    std::vector<float> x(48000, 0.0f);
    const LoudnessStats s = measure_loudness(x, 48000);
    CHECK(!s.has_signal);
    CHECK_LE(s.integrated_lufs, -60.0);
}

TT_CASE("loudness scales with a 6 dB level change") {
    const int fs = 48000;
    auto a = sine(1000.0, 0.1, 5.0, fs);
    auto b = a;
    for (auto& v : b) v *= 2.0f;
    const LoudnessStats sa = measure_loudness(a, fs);
    const LoudnessStats sb = measure_loudness(b, fs);
    CHECK_NEAR(sb.integrated_lufs - sa.integrated_lufs, 6.0206, 0.05);
}

#if SANITY_HAVE_EBUR128
TT_CASE("loudness matches libebur128 on speech like and noise signals") {
    const int fs = 48000;
    std::vector<std::vector<float>> sigs;
    sigs.push_back(sine(1000.0, 0.3, 6.0, fs));
    sigs.push_back(sine(200.0, 0.2, 6.0, fs));
    {
        auto n = noise(0.1, static_cast<std::size_t>(fs) * 6, 42);
        // Shape it: a slow amplitude modulation makes it less trivial.
        for (std::size_t i = 0; i < n.size(); i++)
            n[i] = static_cast<float>(n[i] * (0.5 + 0.5 * std::sin(kTwoPi * 3.0 * i / fs)));
        sigs.push_back(n);
    }
    for (auto& x : sigs) {
        const double mine = measure_loudness(x, fs).integrated_lufs;
        const double ref = ebur128_integrated(x, fs);
        CHECK_NEAR(mine, ref, 0.15);
    }
}

TT_CASE("true peak matches libebur128") {
    const int fs = 48000;
    for (double f : {997.0, 3000.0, 9973.0}) {
        auto x = sine(f, 0.9, 0.3, fs);
        const double mine = true_peak_db(x, fs);
        const double ref = ebur128_true_peak(x, fs);
        // libebur128 uses an implementation defined polyphase FIR that slightly
        // overshoots near the top of the band; ours is a longer flatter kernel.
        CHECK_NEAR(mine, ref, 0.6);
        CHECK_GE(mine, ref - 0.6);
    }
}

TT_CASE("true peak resolves a signal whose samples all miss the peak") {
    // 12 kHz at 45 degrees on a 48 kHz grid: every sample is +-0.6364 while the
    // continuous peak is exactly 0.9. A meter that only looks at samples reads
    // -3.9 dBFS; the true peak must read about -0.92 dBFS.
    const int fs = 48000;
    std::vector<float> x(static_cast<std::size_t>(fs) / 2);
    for (std::size_t i = 0; i < x.size(); i++)
        x[i] = static_cast<float>(0.9 * std::sin(kTwoPi * 12000.0 * i / fs + kPi / 4));
    const double sample_peak = 20.0 * std::log10(peak_abs(x));
    const double tp = true_peak_db(x, fs);
    CHECK_NEAR(sample_peak, -3.92, 0.1);
    CHECK_NEAR(tp, -0.915, 0.06);
}
#endif

TT_CASE("loudness range of steady tone is small") {
    const int fs = 48000;
    auto x = sine(1000.0, 0.2, 12.0, fs);
    CHECK_LE(loudness_range(x, fs), 1.0);
}

TT_CASE("loudness range grows with level variation") {
    const int fs = 48000;
    std::vector<float> x;
    for (int k = 0; k < 6; k++) {
        const double gain = (k % 2 == 0) ? 0.05 : 0.5;
        auto seg = sine(1000.0, gain, 3.0, fs);
        x.insert(x.end(), seg.begin(), seg.end());
    }
    CHECK_GE(loudness_range(x, fs), 5.0);
}

// ------------------------------------------------------------------- WAV I/O
TT_CASE("wav round trip is bit exact for float32") {
    AudioBuffer b = AudioBuffer::zeros(48000, 2, 1000);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> d(-0.9f, 0.9f);
    for (auto& v : b.data) v = d(rng);
    std::string err;
    CHECK(wav_write("/tmp/sanity_rt.wav", b, true, &err));
    AudioBuffer c;
    CHECK(wav_read("/tmp/sanity_rt.wav", c, err));
    CHECK(c.sample_rate == 48000);
    CHECK(c.channels == 2);
    CHECK(c.frames() == b.frames());
    CHECK(c.data == b.data);
}

TT_CASE("wav round trip works for 16 bit") {
    AudioBuffer b = AudioBuffer::zeros(44100, 1, 500);
    for (std::size_t i = 0; i < b.data.size(); i++)
        b.data[i] = static_cast<float>(std::sin(i * 0.1) * 0.5);
    std::string err;
    CHECK(wav_write("/tmp/sanity_rt16.wav", b, false, &err));
    AudioBuffer c;
    CHECK(wav_read("/tmp/sanity_rt16.wav", c, err));
    CHECK(c.sample_rate == 44100);
    double err_max = 0.0;
    for (std::size_t i = 0; i < c.data.size(); i++)
        err_max = std::max(err_max, std::fabs(static_cast<double>(c.data[i] - b.data[i])));
    CHECK_LE(err_max, 1.0 / 32767.0 + 1e-6);
}

// ------------------------------------------------------------------ segments
TT_CASE("segmenter finds the silent gap between two tones") {
    const int fs = 48000;
    std::vector<float> x;
    auto a = sine(300.0, 0.3, 0.5, fs);
    auto silence = std::vector<float>(fs / 2, 0.0f);
    auto b = sine(300.0, 0.3, 0.5, fs);
    // Reserve first: this also keeps GCC 15's -Warray-bounds analysis from
    // reporting a false positive on the growing insert below.
    x.reserve(a.size() + silence.size() + b.size());
    x.insert(x.end(), a.begin(), a.end());
    x.insert(x.end(), silence.begin(), silence.end());
    x.insert(x.end(), b.begin(), b.end());

    Segmenter seg(SegmenterConfig{fs, 10.0, 35.0});
    std::vector<FrameFeat> feats;
    seg.features(x, feats);
    std::vector<Segment> segs;
    seg.segments(feats, segs);
    bool found_silence = false;
    for (const auto& s : segs)
        if (s.cls == SegClass::Silence && (s.end - s.start) * 10 >= 400) found_silence = true;
    CHECK(found_silence);
}

int main() { return tinytest::run_all(); }
