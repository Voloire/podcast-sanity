// podcast-sanity - named delivery profiles
#pragma once

#include <string>
#include <vector>

#include "sanity/encode.hpp"
#include "sanity/master.hpp"

namespace sanity {

struct DistributionProfile {
    const char* id;            // stable, used by the CLI and QSettings
    const char* name;          // English, for the CLI table
    bool recommended;         // exactly one profile is the recommended one
    double target_lufs;        // 0 = auto (-19 mono, -16 stereo)
    double true_peak_db;
    int channels;             // 0 = keep the input channel count
    int sample_rate;          // 0 = keep the input rate
    ExportFormat format;
    int mp3_bitrate_kbps;      // only for Mp3
    const char* note;          // one sentence: why this is right
};

const std::vector<DistributionProfile>& distribution_profiles();
const DistributionProfile* find_distribution_profile(const std::string& id);
const DistributionProfile& recommended_distribution_profile();
// Writes the profile into the mastering parameters and the export options.
void apply_distribution_profile(const DistributionProfile& d, Params& p, ExportOptions& o);
std::string distribution_label(const DistributionProfile& d);

}  // namespace sanity
