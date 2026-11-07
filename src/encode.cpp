#include "sanity/encode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    if (in.channels < 1 || in.channels > 2) {
        // MP3 has no surround here, and silently folding a wider file would
        // destroy the very content the caller asked to keep.
        err = "MP3 export supports mono or stereo only";
        return false;
    }
    const std::size_t frames = in.frames();
    if (frames == 0) {
        err = "nothing to encode";
        return false;
    }

    // Deinterleave first: LAME wants one planar buffer per channel, and the
    // previous code passed the interleaved buffer as if it were planar.
    int rate = in.sample_rate;
    std::vector<std::vector<float>> channels(static_cast<std::size_t>(in.channels));
    for (int c = 0; c < in.channels; c++) {
        channels[static_cast<std::size_t>(c)].resize(frames);
        for (std::size_t i = 0; i < frames; i++)
            channels[static_cast<std::size_t>(c)][i] =
                in.data[i * static_cast<std::size_t>(in.channels) + static_cast<std::size_t>(c)];
    }

    // LAME accepts a fixed set of rates. Resample each channel on its own; the
    // old fallback folded the material to mono and duplicated it.
    if (!lame_rate_ok(rate)) {
        const int target = nearest_lame_rate(rate);
        Resampler rs(rate, target);
        for (auto& channel : channels) channel = rs.process(channel);
        rate = target;
    }

    lame_t gfp = lame_init();
    if (!gfp) {
        err = "lame_init failed";
        return false;
    }
    lame_set_in_samplerate(gfp, rate);
    lame_set_num_channels(gfp, in.channels);
    lame_set_mode(gfp, in.channels == 1 ? MONO : JOINT_STEREO);
    lame_set_brate(gfp, opt.mp3_bitrate_kbps);
    lame_set_quality(gfp, opt.mp3_quality);
    lame_set_VBR(gfp, vbr_off);
    if (lame_init_params(gfp) < 0) {
        lame_close(gfp);
        err = "lame_init_params failed";
        return false;
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        lame_close(gfp);
        err = "cannot open " + path;
        return false;
    }

    const std::size_t out_frames = channels[0].size();
    const int chunk = 1152;
    std::vector<unsigned char> out(static_cast<std::size_t>(chunk) * 5 / 4 + 7200);
    const float* left = channels[0].data();
    const float* right = in.channels > 1 ? channels[1].data() : nullptr;
    std::size_t pos = 0;
    bool ok = true;
    while (pos < out_frames) {
        const int n = static_cast<int>(std::min<std::size_t>(chunk, out_frames - pos));
        int written;
        if (in.channels == 1) {
            written = lame_encode_buffer_ieee_float(gfp, left + pos, left + pos, n, out.data(),
                                                    static_cast<int>(out.size()));
        } else {
            written = lame_encode_buffer_ieee_float(gfp, left + pos, right + pos, n, out.data(),
                                                    static_cast<int>(out.size()));
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
        case ExportFormat::Wav24: info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24; break;
        case ExportFormat::WavFloat32: info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT; break;
        default: info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16; break;
    }
    SF_INFO check = info;
    if (!sf_format_check(&check)) {
        err = "unsupported format";
        return false;
    }
    SNDFILE* sf = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!sf) {
        err = sf_strerror(nullptr);
        return false;
    }
    if (opt.format == ExportFormat::Ogg) {
        double q = opt.vorbis_quality;
        sf_command(sf, SFC_SET_VBR_ENCODING_QUALITY, &q, sizeof(double));
    }
    if (opt.format == ExportFormat::Flac) {
        // libsndfile takes the FLAC compression level as a 0..1 double, not as
        // the 0..8 integer the CLI and the GUI expose.
        double level = static_cast<double>(opt.flac_compression) / 8.0;
        level = std::min(1.0, std::max(0.0, level));
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
        case ExportFormat::Mp3:
            return "MP3 " + std::to_string(o.mp3_bitrate_kbps) + " kbps";
    }
    return "WAV 16 bit";
}

bool known_audio_extension(const std::string& path) {
    const std::string e = lower_ext(path);
    return e == ".wav" || e == ".wave" || e == ".mp3" || e == ".flac" || e == ".ogg" ||
           e == ".opus";
}

ExportOptions export_options_for_path(const std::string& path) {
    ExportOptions o;
    const std::string e = lower_ext(path);
    if (e == ".mp3") o.format = ExportFormat::Mp3;
    else if (e == ".flac") o.format = ExportFormat::Flac;
    else if (e == ".ogg" || e == ".opus") o.format = ExportFormat::Ogg;
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
                return wav_write(path, in, true, &err);
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
#if SANITY_HAVE_SNDFILE
            return encode_sndfile(path, in, opt, err);
#else
            err = "this build has no FLAC/Ogg encoder";
            return false;
#endif
    }
    err = "unknown format";
    return false;
}

}  // namespace sanity
