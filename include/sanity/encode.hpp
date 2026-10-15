// podcast-sanity - file export / encoding
//
// Podcast hosts accept MP3 (and, increasingly, FLAC and Opus). The host only
// leaves the file alone when it is already inside the "no re-encode" envelope,
// so the exporter must be able to hit an exact bitrate and channel count.
#pragma once

#include <string>

#include "sanity/audio.hpp"

namespace sanity {

enum class ExportFormat { Wav16, Wav24, WavFloat32, Flac, Ogg, Mp3 };

struct ExportOptions {
    ExportFormat format = ExportFormat::Wav16;
    // MP3: constant bitrate in kbps (hosts prefer 96 mono / 192 stereo or less).
    int mp3_bitrate_kbps = 192;
    // MP3: LAME quality for the constant bitrate mode, 0 (best) .. 9 (fastest).
    int mp3_quality = 2;
    // FLAC/OGG compression level or quality.
    double vorbis_quality = 0.6;
    int flac_compression = 5;
};

// True when the build can actually produce this format.
bool encoder_available(ExportFormat f);
// Human label, e.g. "MP3 192 kbps".
std::string export_format_name(const ExportOptions& o);
// File extension including the dot, e.g. ".mp3".
std::string export_extension(ExportFormat f);
// Guesses the format from the extension, falling back to WAV 16.
ExportOptions export_options_for_path(const std::string& path);

// Encodes `in` to `path`. Sample rate and channel count are taken from `in`.
bool encode_audio(const std::string& path, const AudioBuffer& in, const ExportOptions& opt,
                  std::string& err);

// The bitrate that keeps a file inside the host's "no re-encode" envelope.
int recommended_mp3_bitrate(int channels);

}  // namespace sanity
