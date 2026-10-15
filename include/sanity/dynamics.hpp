// podcast-sanity - leveling, compression and peak control
#pragma once

#include <cstddef>
#include <vector>

#include "sanity/dsp.hpp"

namespace sanity {

struct LevelerInfo {
    bool applied = false;
    double target_db = -1e30;   // active speech level the file was levelled to
    double spread_before_db = 0.0;  // inter segment level spread before
    double spread_after_db = 0.0;
    double max_boost_db = 0.0;
    double max_cut_db = 0.0;
};

// Adaptive Leveler: corrects level differences between segments inside one
// file, excluding unwanted segments (noise, breaths, silence) from being
// amplified. `strength` 0..1 blends between untouched and full leveling.
LevelerInfo adaptive_leveler(std::vector<float>& x, int sample_rate,
                             const std::vector<FrameFeat>& feats, double strength);

struct CompressorInfo {
    bool applied = false;
    double threshold_db = 0.0;
    double ratio = 0.0;
    double gain_reduction_db = 0.0;
};

// Gentle speech compressor with a soft knee, then automatic makeup.
CompressorInfo compressor(std::vector<float>& x, int sample_rate, double threshold_db,
                          double ratio, double attack_ms, double release_ms);

struct LimiterInfo {
    bool applied = false;
    double ceiling_db = 0.0;
    // Mean reduction over the samples that were actually reduced, so that the
    // silence in between does not dilute the number.
    double gain_reduction_db = 0.0;
    double max_reduction_db = 0.0;
    double affected_ratio = 0.0;  // fraction of samples limited
    double true_peak_after_db = -1e30;
};

// True peak safe lookahead limiter. The gain envelope is derived from the 4x
// oversampled signal, so inter sample peaks stay under the ceiling instead of
// only the sample peaks. `measure_peak` can be turned off when the caller is
// going to measure the true peak itself right afterwards.
LimiterInfo limiter(std::vector<float>& x, int sample_rate, double ceiling_db,
                    bool measure_peak = true);

struct LoudnessTargetInfo {
    bool applied = false;
    double target_lufs = 0.0;
    double achieved_lufs = -1e30;
    double gain_applied_db = 0.0;
    int iterations = 0;
    double error_lu = 0.0;
    double max_short_term_before = -1e30;
    double max_short_term_after = -1e30;
    double loudness_after_gain = -1e30;  // converged, before peak control
    double loudness_after_limit = -1e30;  // after peak control
    double true_peak_before_db = -1e30;   // before the limiter
    double limiter_reduction_db = 0.0;    // average gain reduction
    double limiter_max_reduction_db = 0.0;
    int peak_rounds = 0;                  // limit/measure rounds performed
};

// Multi pass loudness normalisation to an integrated target with a true peak
// ceiling. Level changes are re-measured after limiting and corrected, which is
// what makes the result land inside the tolerance instead of "near" it.
LoudnessTargetInfo normalize_loudness(std::vector<float>& x, int sample_rate, double target_lufs,
                                      double true_peak_ceiling_db, double max_short_term = 0.0,
                                      double max_momentary = 0.0);

}  // namespace sanity
