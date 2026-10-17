// podcast-sanity - named delivery profiles
#include "sanity/distribution.hpp"

#include <cstdio>

namespace sanity {

const std::vector<DistributionProfile>& distribution_profiles() {
    static const std::vector<DistributionProfile> table = {
        {"podcast-voice", "Podcast voice", true, -19.0, -1.0, 1, 0, ExportFormat::Mp3, 96,
         "Apple Podcasts authoring practice for spoken word; hosts do not re-encode mono MP3 at or below 96 kbps."},
        {"podcast-stereo", "Podcast with music, stereo", false, -16.0, -1.0, 2, 0, ExportFormat::Mp3, 192,
         "Stereo delivery at -16 LUFS; hosts do not re-encode stereo MP3 at or below 192 kbps."},
        {"archive-wav24", "Archive master, WAV 24 bit", false, 0.0, -1.0, 0, 0, ExportFormat::Wav24, 0,
         "Lossless master at the automatic target (-19 mono / -16 stereo) to keep and re-export later."},
        {"lossless-flac", "Lossless delivery, FLAC 24 bit", false, 0.0, -1.0, 0, 0, ExportFormat::Flac, 0,
         "For hosts that accept FLAC; same loudness as the archive master, about half the size."},
        {"broadcast-r128", "Broadcast, EBU R128", false, -23.0, -1.0, 0, 48000, ExportFormat::Wav24, 0,
         "EBU R128: -23 LUFS, -1 dBTP, 48 kHz, for radio hand-over rather than podcast hosting."},
    };
    return table;
}

const DistributionProfile* find_distribution_profile(const std::string& id) {
    for (const auto& d : distribution_profiles())
        if (id == d.id) return &d;
    return nullptr;
}

const DistributionProfile& recommended_distribution_profile() {
    for (const auto& d : distribution_profiles())
        if (d.recommended) return d;
    return distribution_profiles().front();
}

void apply_distribution_profile(const DistributionProfile& d, Params& p, ExportOptions& o) {
    p.target_lufs = d.target_lufs;
    p.target_true_peak_db = d.true_peak_db;
    p.out_channels = d.channels;
    p.out_sample_rate = d.sample_rate;
    p.normalize = true;
    o.format = d.format;
    if (d.format == ExportFormat::Mp3) o.mp3_bitrate_kbps = d.mp3_bitrate_kbps;
}

std::string distribution_label(const DistributionProfile& d) {
    ExportOptions o;
    o.format = d.format;
    o.mp3_bitrate_kbps = d.mp3_bitrate_kbps;
    std::string s = std::string(d.name) + ": " + export_format_name(o);
    s += d.channels == 1 ? " mono" : (d.channels == 2 ? " stereo" : "");
    char buf[64];
    if (d.target_lufs != 0.0) {
        std::snprintf(buf, sizeof(buf), ", %.0f LUFS", d.target_lufs);
        s += buf;
    } else {
        s += ", auto LUFS";
    }
    std::snprintf(buf, sizeof(buf), ", %.0f dBTP", d.true_peak_db);
    s += buf;
    if (d.recommended) s += " (recommended)";
    return s;
}

}  // namespace sanity
