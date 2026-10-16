// podcast-sanity - audio buffer and file I/O
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sanity {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = kPi / 2.0;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kFloorDb = -200.0;

inline double db_to_gain(double db) { return std::pow(10.0, db / 20.0); }
inline double gain_to_db(double g) {
    return g <= 1e-12 ? kFloorDb : 20.0 * std::log10(g);
}

struct AudioBuffer {
    int sample_rate = 48000;
    int channels = 1;
    std::vector<float> data;  // interleaved: frame * channels

    std::size_t frames() const {
        return channels > 0 ? data.size() / static_cast<std::size_t>(channels) : 0;
    }
    double duration() const {
        return sample_rate > 0 ? static_cast<double>(frames()) / sample_rate : 0.0;
    }
    void resize_frames(std::size_t n) { data.assign(n * static_cast<std::size_t>(channels), 0.0f); }

    float* channel(std::size_t c) { return data.data() + c; }
    const float* channel(std::size_t c) const { return data.data() + c; }

    static AudioBuffer zeros(int rate, int ch, std::size_t n) {
        AudioBuffer b;
        b.sample_rate = rate;
        b.channels = ch;
        b.resize_frames(n);
        return b;
    }
};

// Reads PCM16, PCM24, PCM32, FLOAT32 and FLOAT64 WAV (also RF64/BW64 headers).
bool wav_read(const std::string& path, AudioBuffer& out, std::string& err);
// Reads WAV natively and compressed formats through libsndfile when available.
bool audio_read(const std::string& path, AudioBuffer& out, std::string& err);
std::string audio_read_filter();
// Writes 32-bit float WAV when float32 is true, else 16-bit PCM.
bool wav_write(const std::string& path, const AudioBuffer& in, bool float32, std::string* err);
// Writes 24-bit PCM WAV without going through libsndfile.
bool wav_write_pcm24(const std::string& path, const AudioBuffer& in, std::string* err);

// Averages all channels into one.
std::vector<float> to_mono(const AudioBuffer& b);
AudioBuffer from_mono(int sample_rate, const std::vector<float>& mono);
// Replaces every channel with the same mono signal (duplication).
AudioBuffer mono_to_channels(int sample_rate, const std::vector<float>& mono, int channels);

double peak_abs(const std::vector<float>& x);
double rms(const std::vector<float>& x);

}  // namespace sanity
