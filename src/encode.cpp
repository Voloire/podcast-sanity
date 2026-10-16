#include "sanity/encode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "sanity/dsp.hpp"

#if SANITY_HAVE_MP3
#include <lame/lame.h>
#endif
#if SANITY_HAVE_SNDFILE
#include <sndfile.h>
#endif

namespace sanity {

namespace {

std::string lower_ext(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// LAME only accepts these rates; anything else is resampled first.
bool lame_rate_ok(int rate) {
    switch (rate) {
        case 8000: case 11025: case 12000: case 16000: case 22050: case 24000:
        case 32000: case 44100: case 48000: return true;
        default: return false;
    }
}

int nearest_lame_rate(int rate) {
    if (rate >= 44100) return 48000;
    if (rate >= 32000) return 32000;
    if (rate >= 22050) return 24000;
    if (rate >= 16000) return 16000;
    return 12000;
}

#if SANITY_HAVE_MP3
bool encode_mp3(const std::string& path, const AudioBuffer& in, const ExportOptions& opt,
                std::string& err) {
    AudioBuffer src = in;
    if (!lame_rate_ok(src.sample_rate)) {
        const int target = nearest_lame_rate(src.sample_rate);
        const std::size_t frames = src.frames();
        std::vector<std::vector<float>> chans(static_cast<std::size_t>(src.channels));
        for (int c = 0; c < src.channels; c++) {
            std::vector<float> one(frames);
            for (std::size_t i = 0; i < frames; i++)
                one[i] = src.data[i * static_cast<std::size_t>(src.channels) + c];
            chans[static_cast<std::size_t>(c)] = resample(one, src.sample_rate, target);
        }
        AudioBuffer rsb;
        rsb.sample_rate = target;
        rsb.channels = src.channels;
        rsb.resize_frames(chans[0].size());
        for (int c = 0; c < rsb.channels; c++)
            for (std::size_t i = 0; i < chans[0].size(); i++)
                rsb.data[i * static_cast<std::size_t>(rsb.channels) + c] = chans[static_cast<std::size_t>(c)][i];
        src = rsb;
    }
    if (src.channels != 1 && src.channels != 2) {
        // Fold anything wider down; MP3 has no surround here.
        src = mono_to_channels(src.sample_rate, to_mono(src), 2);
    }

    lame_t gfp = lame_init();
    if (!gfp) {
        err = "lame_init failed";
        return false;
    }
    lame_set_in_samplerate(gfp, src.sample_rate);
    lame_set_num_channels(gfp, src.channels);
    lame_set_mode(gfp, src.channels == 1 ? MONO : JOINT_STEREO);
    lame_set_brate(gfp, opt.mp3_bitrate_kbps);
    lame_set_quality(gfp, opt.mp3_quality);
    lame_set_VBR(gfp, vbr_off);
    if (src.channels == 1) lame_set_num_channels(gfp, 1);
    if (lame_init_params(gfp) < 0) {
        lame_close(gfp);
        err = "lame_init_params failed";
        return false;
    }

#ifdef _WIN32
    FILE* f = _wfopen(std::filesystem::u8path(path).c_str(), L"wb");
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) {
        lame_close(gfp);
        err = "cannot open " + path;
        return false;
    }

    const std::size_t frames = src.frames();
    const int chunk = 1152;
    std::vector<unsigned char> out(static_cast<std::size_t>(chunk) * 5 / 4 + 7200);
    const float* left = src.channel(0);
    std::size_t pos = 0;
    bool ok = true;
    while (pos < frames) {
        const int n = static_cast<int>(std::min<std::size_t>(chunk, frames - pos));
        int written;
        if (src.channels == 1) {
            written = lame_encode_buffer_ieee_float(gfp, left + pos, left + pos, n, out.data(),
                                                    static_cast<int>(out.size()));
        } else {
            written = lame_encode_buffer_interleaved_ieee_float(
                gfp, src.data.data() + 2 * pos, n, out.data(), static_cast<int>(out.size()));
        }
        if (written < 0) {
            ok = false;
            err = "lame encode error";
            break;
        }
        if (std::fwrite(out.data(), 1, static_cast<std::size_t>(written), f) !=
            static_cast<std::size_t>(written)) {
            ok = false;
            err = "write failed";
            break;
        }
        pos += static_cast<std::size_t>(n);
    }
    if (ok) {
        const int flushed = lame_encode_flush(gfp, out.data(), static_cast<int>(out.size()));
        if (flushed > 0) std::fwrite(out.data(), 1, static_cast<std::size_t>(flushed), f);
    }
    std::fclose(f);
    lame_close(gfp);
    return ok;
}
#endif

#if SANITY_HAVE_SNDFILE
bool encode_sndfile(const std::string& path, const AudioBuffer& in, const ExportOptions& opt,
                    std::string& err) {
    SF_INFO info{};
    info.samplerate = in.sample_rate;
    info.channels = in.channels;
    switch (opt.format) {
        case ExportFormat::Flac: info.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_24; break;
        case ExportFormat::Ogg: info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS; break;
        // SF_FORMAT_OPUS is an enum constant (since libsndfile 1.0.29), not a
        // macro, so the preprocessor cannot test for it; the runtime
        // sf_format_check below and in encoder_available() decide support.
        case ExportFormat::Opus: info.format = SF_FORMAT_OGG | SF_FORMAT_OPUS; break;
        case ExportFormat::Wav24: info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24; break;
        case ExportFormat::WavFloat32: info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT; break;
        default: info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16; break;
    }
    SF_INFO check = info;
    if (!sf_format_check(&check)) {
        err = "unsupported format";
        return false;
    }
#ifdef _WIN32
    SNDFILE* sf = sf_wchar_open(std::filesystem::u8path(path).c_str(), SFM_WRITE, &info);
#else
    SNDFILE* sf = sf_open(path.c_str(), SFM_WRITE, &info);
#endif
    if (!sf) {
        err = sf_strerror(nullptr);
        return false;
    }
    if (opt.format == ExportFormat::Ogg) {
        double q = opt.vorbis_quality;
        sf_command(sf, SFC_SET_VBR_ENCODING_QUALITY, &q, sizeof(double));
    }
    if (opt.format == ExportFormat::Flac) {
        double level = std::clamp(opt.flac_compression, 0, 8) / 8.0;
        sf_command(sf, SFC_SET_COMPRESSION_LEVEL, &level, sizeof(double));
    }
    const std::size_t frames = in.frames();
    const sf_count_t wrote = frames > 0 ? sf_writef_float(sf, in.data.data(),
                                                          static_cast<sf_count_t>(frames))
                                        : 0;
    sf_close(sf);
    if (static_cast<std::size_t>(wrote) != frames) {
        err = "short write";
        return false;
    }
    return true;
}
#endif

}  // namespace

bool encoder_available(ExportFormat f) {
    switch (f) {
        case ExportFormat::Wav16:
        case ExportFormat::Wav24:
        case ExportFormat::WavFloat32: return true;
        case ExportFormat::Mp3:
#if SANITY_HAVE_MP3
            return true;
#else
            return false;
#endif
        case ExportFormat::Flac:
        case ExportFormat::Ogg:
#if SANITY_HAVE_SNDFILE
            return true;
#else
            return false;
#endif
        case ExportFormat::Opus:
#if SANITY_HAVE_SNDFILE
            {
                SF_INFO info{};
                info.samplerate = 48000;
                info.channels = 1;
                info.format = SF_FORMAT_OGG | SF_FORMAT_OPUS;
                return sf_format_check(&info) != 0;
            }
#else
            return false;
#endif
    }
    return false;
}

std::string export_extension(ExportFormat f) {
    switch (f) {
        case ExportFormat::Wav16:
        case ExportFormat::Wav24:
        case ExportFormat::WavFloat32: return ".wav";
        case ExportFormat::Flac: return ".flac";
        case ExportFormat::Ogg: return ".ogg";
        case ExportFormat::Mp3: return ".mp3";
        case ExportFormat::Opus: return ".opus";
    }
    return ".wav";
}

std::string export_format_name(const ExportOptions& o) {
    switch (o.format) {
        case ExportFormat::Wav16: return "WAV 16 bit";
        case ExportFormat::Wav24: return "WAV 24 bit";
        case ExportFormat::WavFloat32: return "WAV 32 bit float";
        case ExportFormat::Flac: return "FLAC 24 bit";
        case ExportFormat::Ogg: return "Ogg Vorbis";
        case ExportFormat::Opus: return "Opus";
        case ExportFormat::Mp3:
            return "MP3 " + std::to_string(o.mp3_bitrate_kbps) + " kbps";
    }
    return "WAV 16 bit";
}

ExportOptions export_options_for_path(const std::string& path) {
    ExportOptions o;
    const std::string e = lower_ext(path);
    if (e == ".mp3") o.format = ExportFormat::Mp3;
    else if (e == ".flac") o.format = ExportFormat::Flac;
    else if (e == ".ogg") o.format = ExportFormat::Ogg;
    else if (e == ".opus") o.format = ExportFormat::Opus;
    else o.format = ExportFormat::Wav16;
    return o;
}

int recommended_mp3_bitrate(int channels) {
    // Buzzsprout and friends promise not to re-encode MP3 when mono stays at or
    // below 96 kbps and stereo at or below 192 kbps.
    return channels <= 1 ? 96 : 192;
}

bool encode_audio(const std::string& path, const AudioBuffer& in, const ExportOptions& opt,
                  std::string& err) {
    if (in.data.empty()) {
        err = "nothing to encode";
        return false;
    }
    switch (opt.format) {
        case ExportFormat::Wav16:
        case ExportFormat::Wav24:
        case ExportFormat::WavFloat32:
            if (opt.format == ExportFormat::WavFloat32)
                return wav_write(path, in, true, &err);
            if (opt.format == ExportFormat::Wav24) {
#if SANITY_HAVE_SNDFILE
                return encode_sndfile(path, in, opt, err);
#else
                return wav_write_pcm24(path, in, &err);
#endif
            }
            return wav_write(path, in, false, &err);
        case ExportFormat::Mp3:
#if SANITY_HAVE_MP3
            return encode_mp3(path, in, opt, err);
#else
            err = "this build has no MP3 encoder";
            return false;
#endif
        case ExportFormat::Flac:
        case ExportFormat::Ogg:
        case ExportFormat::Opus:
#if SANITY_HAVE_SNDFILE
            return encode_sndfile(path, in, opt, err);
#else
            err = "this build has no FLAC/Ogg/Opus encoder";
            return false;
#endif
    }
    err = "unknown format";
    return false;
}

}  // namespace sanity
