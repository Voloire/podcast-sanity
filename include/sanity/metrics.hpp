// podcast-sanity - objective quality metrics for the harness
#pragma once

#include <cstddef>
#include <vector>

namespace sanity {

// Scale invariant signal to distortion ratio between an estimate and the clean
// reference. This is the number that proves a denoiser actually helped.
double si_sdr_db(const std::vector<float>& est, const std::vector<float>& ref);

// SI-SDR after compensating for a constant delay of up to +/- max_lag samples
// (the processing chain is not guaranteed to be delay free) and for a global
// gain. Without this, a two millisecond group delay from a filter would be
// counted as distortion.
double aligned_si_sdr_db(const std::vector<float>& est, const std::vector<float>& ref,
                         int max_lag);

// Active speech level (ITU-T P.56 style, simplified): the level of the frames
// that actually carry speech, ignoring pauses.
double active_speech_level_db(const std::vector<float>& x, int sample_rate);

// Noise floor estimated from the quietest 10 % of frames.
double noise_floor_db(const std::vector<float>& x, int sample_rate);

// Mean square error in dB relative to the reference power, over speech frames.
double segmental_snr_db(const std::vector<float>& est, const std::vector<float>& ref, int sample_rate);

// Log spectral distance (dB), lower is closer to the reference.
double log_spectral_distance_db(const std::vector<float>& est, const std::vector<float>& ref, int sample_rate);

// Precision / recall of interval detection against ground truth, with a
// tolerance in milliseconds for the boundaries.
struct IntervalScore {
    int true_positive = 0;
    int false_positive = 0;
    int false_negative = 0;
    double tolerance_ms = 0.0;
    double precision() const;
    double recall() const;
    double f1() const;
};

struct Interval {
    std::size_t start = 0;
    std::size_t end = 0;
};

IntervalScore score_intervals(const std::vector<Interval>& detected,
                              const std::vector<Interval>& truth, int sample_rate,
                              double tolerance_ms);

// Fraction of ground truth speech that was destroyed by the processing
// (used to prove the Filler Killer does not eat real words).
double speech_preservation_db(const std::vector<float>& processed,
                              const std::vector<float>& reference, int sample_rate);

}  // namespace sanity
