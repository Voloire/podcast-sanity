// podcast-sanity - autonomous restoration policy
//
// The user asks for "the maximum cleanup that keeps the voice intact". That is
// not a preset and it is not a fixed strength: it is a bounded search over a
// small, documented set of candidates, scored on excerpts of the actual file.
// This header exposes the deterministic result of that search so the headless
// skill, the CLI and the tests all share one implementation.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/master.hpp"

namespace sanity {

// Bumped whenever a threshold or a candidate changes. The report records it so
// a result can be reproduced against the policy that produced it.
inline constexpr const char* kAutoPolicyVersion = "1";

struct AutoSettings {
    std::string profile = "preservation";
    double denoise_strength = 0.0;
    double dereverb_strength = 0.0;
    bool dereverb = false;
    bool deess = false;
    double deess_amount = 0.0;
    bool dehum = true;
    bool adaptive_highpass = true;
    bool declick = true;
    bool leveler = true;
    double leveler_strength = 0.0;
    bool compress = true;
    double compressor_threshold_db = -18.0;
    double compressor_ratio = 3.0;
    double confidence = 0.0;
};

struct AutoExcerpt {
    double start_s = 0.0;
    double end_s = 0.0;
    std::string reason;
};

struct CandidateResult {
    std::string profile;
    AutoSettings settings;
    bool rejected = false;
    std::string reject_reason;

    // Positive means the candidate reduced the matched quiet-region level.
    double quiet_reduction_db = 0.0;
    double hum_reduction_db = 0.0;
    // Gain-aligned speech preservation proxies; 0 dB means untouched.
    double speech_envelope_db = 0.0;
    double speech_band_db = 0.0;
    // Diagnostic only: SI-SDR against the preservation render. It is not a
    // perceptual proof and it is not a gate on noisy material.
    double speech_si_sdr_db = 0.0;
    double stereo_imbalance_change_db = 0.0;
    long long new_clipped = 0;
    double score_db = 0.0;
    bool selected = false;
};

struct AutoReport {
    std::string version;
    std::string policy_version = kAutoPolicyVersion;
    Analysis before;
    AutoSettings chosen;
    std::vector<CandidateResult> candidates;
    std::vector<AutoExcerpt> excerpts;
    double evidence_confidence = 0.0;
    bool full_file_fallback = false;
    std::string fallback_reason;
    // Preservation re-check on the complete file, before dynamics.
    double full_file_speech_envelope_db = 0.0;
    double full_file_speech_band_db = 0.0;
    double full_file_stereo_imbalance_change_db = 0.0;
    long long full_file_new_clipped = 0;
    std::vector<std::string> warnings;

    // Serialised as the engine section of the runner's JSON report.
    std::string to_json() const;
};

// Runs the excerpt comparison and returns the decision. The audio is untouched.
AutoReport choose_auto_settings(const AudioBuffer& in);

// Chooses the settings and renders the whole recording with them, preserving
// the channel layout. Restoration and leveling only: delivery normalisation is
// the runner's finalizer, which measures the encoded file.
AutoReport auto_clean(const AudioBuffer& in, AudioBuffer& out);

// Applies an already chosen policy. Exposed for tests and for the runner's
// single full-length fallback render.
void apply_auto_settings(const AudioBuffer& in, const AutoSettings& settings, AudioBuffer& out);

}  // namespace sanity
