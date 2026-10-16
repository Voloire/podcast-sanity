// podcast-sanity - mastering pipeline: analysis, cleaning, leveling, standards
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/dsp.hpp"

namespace sanity {

enum class Preset { Talk, Music };
enum class FillerMode { Off, Cautious, Balanced, Aggressive };

struct Params {
    Preset preset = Preset::Talk;

    // --- standards -------------------------------------------------------
    bool normalize = true;
    double target_lufs = 0.0;            // 0 = auto: -16 stereo, -19 mono
    double target_true_peak_db = -1.0;
    double max_lra = 0.0;                // 0 = unconstrained
    double max_short_term = 0.0;         // 0 = unconstrained
    double max_momentary = 0.0;          // 0 = unconstrained

    // --- Adaptive Leveler ------------------------------------------------
    bool adaptive_leveler = true;
    double leveler_strength = 1.0;       // 0..1
    bool compress = true;

    // --- cleaning --------------------------------------------------------
    bool adaptive_highpass = true;
    bool dehum = true;
    bool declick = true;
    bool deess = true;
    bool power_clean = false;            // aggressive denoise + de-reverb
    double denoise_strength = -1.0;      // <0 = auto
    bool dereverb = false;

    // --- Filler Killer ---------------------------------------------------
    FillerMode filler = FillerMode::Off;

    // --- output ----------------------------------------------------------
    int out_sample_rate = 0;             // 0 = keep input rate
    int out_channels = 0;                // 0 = keep input channel count
};

struct StageTiming {
    std::string name;
    bool enabled = false;
    double ms = 0.0;
    double rtf = 1.0;  // processing_ms / audio_ms
    std::vector<std::pair<std::string, double>> metrics;
};

struct Analysis {
    int sample_rate = 0;
    int channels = 0;
    double duration = 0.0;

    double integrated_lufs = -1e30;
    double lra = 0.0;
    double true_peak_db = -1e30;
    double sample_peak_db = -1e30;
    double max_momentary = -1e30;
    double max_short_term = -1e30;

    double noise_floor_db = -1e30;   // quietest region
    double noise_avg_db = -1e30;
    int noise_regions = 0;
    std::vector<double> region_noise_db;

    double hum_freq = 0.0;
    double hum_db = -1e30;
    bool hum_present = false;

    double dc_offset = 0.0;
    long long clipped_samples = 0;
    double rt60 = 0.0;               // estimated reverberation time
    double speech_ratio = 0.0;
    double music_ratio = 0.0;
    double silence_ratio = 0.0;
    double sibilance_db = -1e30;     // 5-8 kHz to total ratio
    double speech_activity_db = -1e30;
};

struct FillerHit {
    std::size_t start = 0;  // sample index into the mono signal
    std::size_t end = 0;
    double score = 0.0;
    double duration_ms = 0.0;
};

// Every segment the Filler Killer considered, whether or not it was removed.
// Kept so the GUI can show why a decision was made and so the test harness can
// measure precision and recall.
struct FillerCandidate {
    std::size_t start = 0;
    std::size_t end = 0;
    double score = 0.0;
    double duration_ms = 0.0;
    bool removed = false;
};

struct Report {
    std::string version;
    Params params;
    std::string params_hash;

    Analysis before;
    Analysis after;
    std::vector<FillerHit> fillers;
    std::size_t fillers_removed = 0;
    double filler_seconds_removed = 0.0;

    std::vector<StageTiming> stages;

    bool conformant_loudness = false;
    bool conformant_true_peak = false;
    bool conformant = false;
    std::vector<std::string> warnings;

    std::size_t frames_in = 0;
    std::size_t frames_out = 0;
    double total_ms = 0.0;
    double rtf = 1.0;

    // Serialised as JSON (the "quality report").
    std::string to_json() const;
    // Short human readable summary for the CLI and the UI.
    std::string to_summary() const;
};

// Measures everything the report needs from a mono signal.
Analysis analyze(const std::vector<float>& mono, int sample_rate);
// Same measurement, reusing features that the caller already computed for this
// exact signal (the pipeline needs them anyway, and the pass is the single most
// expensive part of the analysis).
Analysis analyze(const std::vector<float>& mono, int sample_rate,
                 const std::vector<FrameFeat>& feats);

// Measures an interleaved buffer: loudness, true peak and peak statistics are
// BS.1770 multichannel values; the content features (noise, hum, RT60, speech
// ratio) are measured on the mono sum, which is what the chain processes.
Analysis analyze(const AudioBuffer& b);
// Overwrites the loudness and peak fields of `a` with the multichannel
// measurement of `b` and sets a.channels. No-op for mono.
void apply_multichannel_loudness(Analysis& a, const AudioBuffer& b);

// Filler Killer: hesitation candidate detection (acoustic, no ASR).
std::vector<FillerHit> detect_fillers(const std::vector<float>& mono, int sample_rate,
                                      FillerMode mode,
                                      std::vector<FillerCandidate>* candidates = nullptr);
// Cuts the detected regions and joins the two sides with an equal power
// crossfade, so no digital hole is heard where the hesitation used to be.
std::vector<float> remove_fillers(const std::vector<float>& mono, int sample_rate,
                                  const std::vector<FillerHit>& hits,
                                  double crossfade_ms = 15.0);

// Full mastering run. `out` matches the requested output rate/channels.
Report master(const AudioBuffer& in, const Params& params, AudioBuffer& out);

std::string params_hash(const Params& p);

}  // namespace sanity
