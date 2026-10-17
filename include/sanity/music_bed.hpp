// podcast-sanity - background music bed with speech-driven ducking
#pragma once

#include <cstddef>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/dsp.hpp"

namespace sanity {

struct MusicBedOptions {
    bool enabled = false;
    double bed_level_db = -14.0;   // relative to active speech, in pauses
    double duck_db = -10.0;        // extra attenuation while speech is present
    double attack_ms = 80.0;
    double release_ms = 700.0;
    double hold_ms = 300.0;
    double fade_in_s = 1.5;
    double fade_out_s = 3.0;
    double offset_s = 0.0;
    double tail_s = 0.0;
    bool loop = true;
};

struct MusicBedInfo {
    bool applied = false;
    double speech_level_db = -1e30;
    double bed_level_db = -1e30;
    double duck_ratio = 0.0;
    std::size_t frames_added = 0;
    std::size_t loops = 0;
};

// Builds the bed alone, aligned to speech (mono at sample_rate).
// Length: speech.size() + tail frames.
std::vector<float> render_music_bed(const std::vector<float>& speech, int sample_rate,
                                    const std::vector<FrameFeat>& feats, const AudioBuffer& music,
                                    const MusicBedOptions& o, MusicBedInfo* info);
// Adds the bed in place, extending speech by the tail.
MusicBedInfo mix_music_bed(std::vector<float>& speech, int sample_rate,
                           const std::vector<FrameFeat>& feats, const AudioBuffer& music,
                           const MusicBedOptions& o);

}  // namespace sanity
