// podcast-sanity - synthetic corpus with ground truth for the test harness
//
// The harness needs recordings whose clean version and whose defects it knows
// exactly. This generator speaks: it synthesises intelligible formant based
// speech, inserts hesitations that only a "filler" detector should remove, and
// then degrades the result with a known amount of noise, hum, reverberation,
// clipping, clicks and dropouts.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sanity {

struct Degradations {
    double noise_snr_db = 30.0;    // additive broadband noise vs speech level
    double noise_tilt = 0.0;       // 0 = white, >0 = pinker
    double hum_freq = 0.0;         // 0 disables hum
    double hum_db = -52.0;         // level of the fundamental
    double rt60 = 0.0;             // 0 = dry
    double clip_at = 0.0;          // 0 = no clipping, else threshold
    double dc = 0.0;
    int clicks = 0;
    double dropout_per_min = 0.0;
    int filler_every = 11;  // insert a hesitation every N syllables, 0 = never
    int seed = 1;
};

struct Span {
    std::size_t start = 0;
    std::size_t end = 0;
};

struct CorpusItem {
    std::string name;
    int sample_rate = 48000;
    std::vector<float> clean;     // ground truth, no noise
    std::vector<float> degraded;  // what the pipeline receives
    std::vector<Span> fillers;    // hesitation positions, in degraded samples
    std::vector<Span> words;      // real word positions
    Degradations degradations;
    double speech_level_db = -20.0;
    double measured_noise_db = 0.0;  // realised noise level
    double measured_hum_db = 0.0;
};

// A talking-head style episode: words, hesitations, pauses, sibilants.
CorpusItem make_corpus_talk(int sample_rate, double seconds, const Degradations& d,
                            unsigned seed, const std::string& name, bool female = false);

// Convenience presets used by the harness scenarios.
Degradations degradation_studio();   // clean-ish recording
Degradations degradation_home();     // fan noise, some hum, mild room
Degradations degradation_rough();    // traffic, reverb, clipping, clicks

}  // namespace sanity
