// podcast-sanity - ITU-R BS.1770-4 loudness and true peak measurement
#pragma once

#include <cstddef>
#include <vector>

namespace sanity {

// K-weighting: stage 1 high shelf (+4 dB @ ~1681.97 Hz, Q 0.70718),
// stage 2 high pass (38.14 Hz, Q 0.50033). Coefficients are derived
// analytically so that any sample rate is supported; at 48 kHz they match
// the tabulated values of BS.1770-4 to within 1e-9 (see tests).
struct KWeighting {
    double s1_b0, s1_b1, s1_b2, s1_a1, s1_a2;
    double s2_b0, s2_b1, s2_b2, s2_a1, s2_a2;
};

KWeighting k_weighting_for(double sample_rate);

struct LoudnessStats {
    double integrated_lufs = -1e30;
    double lra = 0.0;
    double max_momentary = -1e30;   // 400 ms window
    double max_short_term = -1e30;  // 3 s window
    double min_short_term = 1e30;
    double sample_peak_db = -1e30;
    double true_peak_db = -1e30;
    double dc_offset = 0.0;
    long long clipped_samples = 0;
    bool has_signal = false;
};

// 100 ms gating blocks, -70 LUFS absolute gate and -10 LU relative gate.
LoudnessStats measure_loudness(const std::vector<float>& mono, int sample_rate);

// Integrated loudness only. It runs a single K-weighting pass and skips the
// true peak oversampling and the loudness range pass, which makes it cheap
// enough to be called once per iteration of the normalisation loop.
double integrated_lufs(const std::vector<float>& mono, int sample_rate);

// LRA per EBU Tech 3342: 10th/95th percentile of gated short-term loudness.
double loudness_range(const std::vector<float>& mono, int sample_rate);
double true_peak_db(const std::vector<float>& mono, int sample_rate);

}  // namespace sanity
