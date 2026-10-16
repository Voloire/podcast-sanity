#include "sanity/audio.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace sanity {

namespace {

std::uint32_t rd32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t rd16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
bool fourcc(const unsigned char* p, const char* s) { return std::memcmp(p, s, 4) == 0; }

void wr32(std::vector<unsigned char>& v, std::uint32_t x) {
    v.push_back(x & 0xff);
    v.push_back((x >> 8) & 0xff);
    v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 24) & 0xff);
}
void wr16(std::vector<unsigned char>& v, std::uint16_t x) {
    v.push_back(x & 0xff);
    v.push_back((x >> 8) & 0xff);
}

// Canonical 44-byte RIFF/WAVE header around an already encoded body.
bool write_wav(const std::string& path, const AudioBuffer& in, int bits, std::uint16_t tag,
               const std::vector<unsigned char>& body, std::string* err) {
    const int channels = in.channels;
    const int bytes = bits / 8;
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(body.size());

    std::vector<unsigned char> hdr;
    hdr.reserve(44);
    const char* riff = "RIFF";
    hdr.insert(hdr.end(), riff, riff + 4);
    wr32(hdr, 36 + data_bytes);
    const char* wave = "WAVE";
    hdr.insert(hdr.end(), wave, wave + 4);
    const char* fmt = "fmt ";
    hdr.insert(hdr.end(), fmt, fmt + 4);
    wr32(hdr, 16);
    wr16(hdr, tag);
    wr16(hdr, static_cast<std::uint16_t>(channels));
    wr32(hdr, static_cast<std::uint32_t>(in.sample_rate));
    wr32(hdr, static_cast<std::uint32_t>(in.sample_rate * channels * bytes));
    wr16(hdr, static_cast<std::uint16_t>(channels * bytes));
    wr16(hdr, static_cast<std::uint16_t>(bits));
    const char* dat = "data";
    hdr.insert(hdr.end(), dat, dat + 4);
    wr32(hdr, data_bytes);

    std::ofstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
    f.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    if (!f) {
        if (err) *err = "write failed";
        return false;
    }
    return true;
}

}  // namespace

bool wav_read(const std::string& path, AudioBuffer& out, std::string& err) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 44) {
        err = "file too small";
        return false;
    }
    if (!(fourcc(&buf[0], "RIFF") || fourcc(&buf[0], "RF64") || fourcc(&buf[0], "BW64")) ||
        !fourcc(&buf[8], "WAVE")) {
        err = "not a RIFF/WAVE file";
        return false;
    }

    int channels = 0, bits = 0, format = 0;
    std::size_t data_off = 0, data_len = 0;
    std::size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const unsigned char* ck = &buf[pos];
        const std::uint32_t sz = rd32(ck + 4);
        const std::size_t body = pos + 8;
        if (fourcc(ck, "fmt ")) {
            if (body + 16 > buf.size()) break;
            format = rd16(&buf[body]);
            channels = rd16(&buf[body + 2]);
            // sample rate at body+4 (unused: we trust the header value)
            bits = rd16(&buf[body + 14]);
            if (format == 0xFFFE && sz >= 40) {  // WAVE_FORMAT_EXTENSIBLE
                format = rd16(&buf[body + 24]);  // first two bytes of SubFormat GUID
            }
        } else if (fourcc(ck, "data")) {
            data_off = body;
            data_len = sz;
            if (data_len == 0 || data_off + data_len > buf.size())
                data_len = buf.size() > data_off ? buf.size() - data_off : 0;
            // 0xFFFFFFFF is the RF64 placeholder.
            if (sz == 0xFFFFFFFFu && data_off < buf.size()) data_len = buf.size() - data_off;
        }
        if (sz == 0xFFFFFFFFu) break;
        pos = body + sz + (sz & 1u);
    }

    if (channels <= 0 || channels > 64) {
        err = "unsupported channel count";
        return false;
    }
    if (data_off == 0 || data_len == 0) {
        err = "no data chunk";
        return false;
    }
    if (!(format == 1 || format == 3)) {
        err = "unsupported format tag (only PCM and IEEE float)";
        return false;
    }

    const int bytes = bits / 8;
    if (bytes <= 0) {
        err = "unsupported bit depth";
        return false;
    }
    const std::size_t frames = data_len / (static_cast<std::size_t>(bytes) * channels);
    out.sample_rate = 0;
    {
        // sample rate lives at fmt body+4; re-scan cheaply
        std::size_t p = 12;
        while (p + 8 <= buf.size()) {
            const unsigned char* ck = &buf[p];
            const std::uint32_t sz = rd32(ck + 4);
            if (fourcc(ck, "fmt ")) {
                out.sample_rate = static_cast<int>(rd32(&buf[p + 8 + 4]));
                break;
            }
            p = p + 8 + sz + (sz & 1u);
        }
    }
    if (out.sample_rate <= 0) {
        err = "invalid sample rate";
        return false;
    }
    out.channels = channels;
    out.resize_frames(frames);

    const unsigned char* d = &buf[data_off];
    for (std::size_t i = 0; i < frames * static_cast<std::size_t>(channels); i++) {
        const unsigned char* s = d + i * static_cast<std::size_t>(bytes);
        float v = 0.0f;
        if (format == 3) {
            if (bits == 32) {
                std::uint32_t u = rd32(s);
                float tmp;
                std::memcpy(&tmp, &u, 4);
                v = tmp;
            } else if (bits == 64) {
                std::uint64_t u = 0;
                for (int b = 0; b < 8; b++) u |= static_cast<std::uint64_t>(s[b]) << (8 * b);
                double tmp;
                std::memcpy(&tmp, &u, 8);
                v = static_cast<float>(tmp);
            }
        } else if (bits == 16) {
            v = static_cast<float>(static_cast<std::int16_t>(rd16(s))) / 32768.0f;
        } else if (bits == 24) {
            std::int32_t x = (s[0] << 8) | (s[1] << 16) | (s[2] << 24);
            v = static_cast<float>(x) / 2147483648.0f;
        } else if (bits == 32) {
            v = static_cast<float>(static_cast<std::int32_t>(rd32(s))) / 2147483648.0f;
        } else if (bits == 8) {
            v = (static_cast<float>(s[0]) - 128.0f) / 128.0f;
        }
        out.data[i] = v;
    }
    return true;
}

bool wav_write(const std::string& path, const AudioBuffer& in, bool float32, std::string* err) {
    const int bits = float32 ? 32 : 16;
    const std::uint16_t tag = static_cast<std::uint16_t>(float32 ? 3 : 1);

    std::vector<unsigned char> body;
    body.reserve(in.data.size() * (float32 ? 4u : 2u));
    for (float f : in.data) {
        if (float32) {
            std::uint32_t u;
            std::memcpy(&u, &f, 4);
            wr32(body, u);
        } else {
            double v = f;
            v = v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v);
            // TPDF-free deterministic rounding to 16 bit.
            std::int32_t x = static_cast<std::int32_t>(std::lround(v * 32767.0));
            if (x < -32768) x = -32768;
            if (x > 32767) x = 32767;
            wr16(body, static_cast<std::uint16_t>(static_cast<std::int16_t>(x)));
        }
    }
    return write_wav(path, in, bits, tag, body, err);
}

bool wav_write_pcm24(const std::string& path, const AudioBuffer& in, std::string* err) {
    std::vector<unsigned char> body;
    body.reserve(in.data.size() * 3);
    for (float f : in.data) {
        double v = f;
        v = v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v);
        std::int32_t x = static_cast<std::int32_t>(std::lround(v * 8388607.0));
        if (x < -8388608) x = -8388608;
        if (x > 8388607) x = 8388607;
        const std::uint32_t u = static_cast<std::uint32_t>(x) & 0xFFFFFFu;
        body.push_back(static_cast<unsigned char>(u & 0xff));
        body.push_back(static_cast<unsigned char>((u >> 8) & 0xff));
        body.push_back(static_cast<unsigned char>((u >> 16) & 0xff));
    }
    return write_wav(path, in, 24, 1, body, err);
}

std::vector<float> to_mono(const AudioBuffer& b) {
    const std::size_t n = b.frames();
    std::vector<float> m(n, 0.0f);
    if (b.channels == 1) {
        m = b.data;
        return m;
    }
    for (std::size_t i = 0; i < n; i++) {
        double s = 0.0;
        for (int c = 0; c < b.channels; c++) s += b.data[i * b.channels + c];
        m[i] = static_cast<float>(s / b.channels);
    }
    return m;
}

AudioBuffer from_mono(int sample_rate, const std::vector<float>& mono) {
    AudioBuffer b;
    b.sample_rate = sample_rate;
    b.channels = 1;
    b.data = mono;
    return b;
}

AudioBuffer mono_to_channels(int sample_rate, const std::vector<float>& mono, int channels) {
    AudioBuffer b;
    b.sample_rate = sample_rate;
    b.channels = channels;
    b.resize_frames(mono.size());
    for (std::size_t i = 0; i < mono.size(); i++)
        for (int c = 0; c < channels; c++) b.data[i * channels + c] = mono[i];
    return b;
}

double peak_abs(const std::vector<float>& x) {
    double p = 0.0;
    for (float v : x) {
        const double a = std::fabs(static_cast<double>(v));
        if (a > p) p = a;
    }
    return p;
}

double rms(const std::vector<float>& x) {
    if (x.empty()) return 0.0;
    double s = 0.0;
    for (float v : x) s += static_cast<double>(v) * v;
    return std::sqrt(s / static_cast<double>(x.size()));
}

}  // namespace sanity
