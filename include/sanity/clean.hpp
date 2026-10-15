// podcast-sanity - repair and cleaning stages
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sanity/dsp.hpp"

namespace sanity {

// Kills impulses (clicks, pops, cable bumps) by interpolating the damaged
// samples. Returns the number of samples repaired.
std::size_t declick(std::vector<float>& x, int sample_rate);

// Removes residual DC offset.
double remove_dc(std::vector<float>& x);

struct HumInfo {
    double freq = 0.0;        // 50 or 60 Hz, 0 when no hum was found
    double level_db = -1e30;  // level of the fundamental
    double excess_db = -1e30; // how far the fundamental stands out locally
    double noise_floor_db = -1e30;
    std::vector<int> harmonics;  // harmonics that were notched
    bool applied = false;
};

// Adaptive mains hum removal. Only harmonics that actually carry hum are
// notched, and the mains frequency is decided from the material.
HumInfo dehum(std::vector<float>& x, int sample_rate, const std::vector<FrameFeat>& feats,
              double noise_floor_db, bool apply);

struct HighpassInfo {
    double cutoff_min = 0.0;
    double cutoff_max = 0.0;
    double cutoff_used = 0.0;
    std::size_t voices_measured = 0;  // voiced frames the corner was derived from
    bool applied = false;
};

// Adaptive high pass. The corner is set from the actual content: it is placed
// below the lowest voice fundamental present (with a hard ceiling), so it only
// removes rumble. `floor_hz` is the absolute lowest cutoff allowed (about 55 Hz
// for talk, 30 Hz for music).
HighpassInfo adaptive_highpass(std::vector<float>& x, int sample_rate,
                               const std::vector<FrameFeat>& feats, double floor_hz, bool apply);

struct DenoiseInfo {
    bool applied = false;
    int regions = 0;
    double strength = 0.0;
    double noise_floor_db = -1e30;
    double reduction_db = 0.0;  // estimated average gain reduction applied
};

// Spectral denoiser following the classic design: the material is segmented
// into regions with different background noise characteristics, a noise print
// is extracted per region, and each region is denoised with its own print.
DenoiseInfo denoise(std::vector<float>& x, int sample_rate, const std::vector<FrameFeat>& feats,
                    double strength);

struct DereverbInfo {
    bool applied = false;
    double rt60_before = 0.0;
    double reduction_db = 0.0;
};

// Late reverberation suppression from the spectral decay envelope.
DereverbInfo dereverb(std::vector<float>& x, int sample_rate, const std::vector<FrameFeat>& feats,
                      double strength);

struct DeessInfo {
    bool applied = false;
    double max_reduction_db = 0.0;
    double sibilance_before_db = -1e30;
};

// Dynamic sibilance control in the 5-8 kHz band.
DeessInfo deess(std::vector<float>& x, int sample_rate, double amount);

// STFT geometry chosen from the sample rate (about 21 ms window, 75 % overlap).
StftConfig stft_for(int sample_rate);

}  // namespace sanity
