// podcast-sanity - compressed audio input through libsndfile
#include "sanity/audio.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

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
}  // namespace

bool audio_read(const std::string& path, AudioBuffer& out, std::string& err) {
    if (lower_ext(path) == ".wav") return wav_read(path, out, err);
#if SANITY_HAVE_SNDFILE
    SF_INFO info{};
#ifdef _WIN32
    SNDFILE* sf = sf_wchar_open(std::filesystem::u8path(path).c_str(), SFM_READ, &info);
#else
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &info);
#endif
    if (!sf) {
        err = sf_strerror(nullptr);
        return false;
    }
    if (info.channels <= 0 || info.samplerate <= 0 || info.frames < 0) {
        sf_close(sf);
        err = "unsupported stream";
        return false;
    }
    out.sample_rate = info.samplerate;
    out.channels = info.channels;
    out.resize_frames(static_cast<std::size_t>(info.frames));
    const sf_count_t got = sf_readf_float(sf, out.data.data(), info.frames);
    sf_close(sf);
    if (got < 0) {
        err = "decode failed";
        return false;
    }
    out.data.resize(static_cast<std::size_t>(got) * static_cast<std::size_t>(out.channels));
    return true;
#else
    err = "this build reads WAV only";
    return false;
#endif
}

std::string audio_read_filter() {
#if SANITY_HAVE_SNDFILE
    return "Audio (*.wav *.flac *.ogg *.opus *.mp3 *.aiff);;Tutti i file (*)";
#else
    return "Audio (*.wav);;Tutti i file (*)";
#endif
}

}  // namespace sanity
