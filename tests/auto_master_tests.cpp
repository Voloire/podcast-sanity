// podcast-sanity - autonomous restoration policy tests
//
// The policy is the part of 0.2 that decides what to do with a file, so it is
// tested on the same synthetic material the end-to-end harness uses: clean
// recordings must stay untouched, contaminated ones must be cleaned with
// measurable evidence, and stereo must survive intact.
#include <cmath>
#include <string>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/auto_master.hpp"
#include "sanity/corpus.hpp"
#include "sanity/dsp.hpp"
#include "tinytest.hpp"

using namespace sanity;

namespace {

AudioBuffer buffer_of(const CorpusItem& item) {
    AudioBuffer b;
    b.sample_rate = item.sample_rate;
    b.channels = 1;
    b.data = item.degraded;
    return b;
}

// Right channel: the same voice delayed and slightly quieter. This is the
// shape of a real two-mic interview and it must not be folded to mono.
AudioBuffer to_stereo(const AudioBuffer& mono, int delay_samples, double right_gain) {
    AudioBuffer b;
    b.sample_rate = mono.sample_rate;
    b.channels = 2;
    const std::size_t n = mono.frames();
    b.data.assign(n * 2, 0.0f);
    for (std::size_t i = 0; i < n; i++) {
        b.data[i * 2] = mono.data[i];
        const std::size_t j = i >= static_cast<std::size_t>(delay_samples)
                                  ? i - static_cast<std::size_t>(delay_samples)
                                  : 0;
        b.data[i * 2 + 1] = static_cast<float>(mono.data[j] * right_gain);
    }
    return b;
}

const CandidateResult* find_candidate(const AutoReport& r, const std::string& profile) {
    for (const auto& c : r.candidates)
        if (c.profile == profile) return &c;
    return nullptr;
}

}  // namespace

TT_CASE("auto policy is deterministic for the same input") {
    const CorpusItem item = make_corpus_talk(48000, 6.0, degradation_home(), 7, "det");
    const AudioBuffer in = buffer_of(item);
    const AutoReport a = choose_auto_settings(in);
    const AutoReport b = choose_auto_settings(in);
    CHECK(a.chosen.profile == b.chosen.profile);
    CHECK(a.chosen.denoise_strength == b.chosen.denoise_strength);
    CHECK(a.chosen.leveler_strength == b.chosen.leveler_strength);
    CHECK(a.candidates.size() == b.candidates.size());
    for (std::size_t i = 0; i < a.candidates.size() && i < b.candidates.size(); i++) {
        CHECK(a.candidates[i].profile == b.candidates[i].profile);
        CHECK(a.candidates[i].quiet_reduction_db == b.candidates[i].quiet_reduction_db);
        CHECK(a.candidates[i].speech_si_sdr_db == b.candidates[i].speech_si_sdr_db);
        CHECK(a.candidates[i].rejected == b.candidates[i].rejected);
    }
}

TT_CASE("clean material keeps the preservation candidate") {
    const CorpusItem item = make_corpus_talk(48000, 6.0, degradation_studio(), 3, "clean");
    const AutoReport r = choose_auto_settings(buffer_of(item));
    CHECK(r.chosen.profile == "preservation");
    CHECK(r.chosen.denoise_strength == 0.0);
    CHECK(r.full_file_fallback == false);
    // Even the untouched reference must pass the gates it is measured against.
    const CandidateResult* p = find_candidate(r, "preservation");
    CHECK(p != nullptr);
    if (p) {
        CHECK(!p->rejected);
        CHECK(p->new_clipped == 0);
    }
}

TT_CASE("noisy material selects restoration with measured evidence") {
    const CorpusItem item = make_corpus_talk(48000, 8.0, degradation_rough(), 5, "noisy");
    const AutoReport r = choose_auto_settings(buffer_of(item));
    CHECK(!r.candidates.empty());
    CHECK(r.chosen.denoise_strength > 0.0);
    CHECK(r.chosen.profile != "preservation");
    CHECK(r.evidence_confidence > 0.5);
    const CandidateResult* chosen = find_candidate(r, r.chosen.profile);
    CHECK(chosen != nullptr);
    if (chosen) {
        // Cleaning has to be measurable, and it must not have eaten the voice.
        CHECK(chosen->quiet_reduction_db >= 1.0);
        CHECK(chosen->speech_si_sdr_db >= 0.0);
        CHECK(chosen->speech_band_db >= -1.5);
        CHECK(!chosen->rejected);
    }
    // Exactly one candidate is the answer.
    int selected = 0;
    for (const auto& c : r.candidates)
        if (c.selected) selected++;
    CHECK(selected == 1);
}

TT_CASE("candidate search stays inside the documented bounds") {
    const CorpusItem item = make_corpus_talk(48000, 5.0, degradation_home(), 11, "bounds");
    const AutoReport r = choose_auto_settings(buffer_of(item));
    CHECK(r.candidates.size() == 4);
    CHECK(r.excerpts.size() <= 8);
    for (const auto& c : r.candidates) {
        CHECK(c.settings.denoise_strength >= 0.0);
        CHECK(c.settings.denoise_strength <= 1.2);
        CHECK(c.settings.dereverb_strength >= 0.0);
        CHECK(c.settings.dereverb_strength <= 0.6);
        // Content editing is never part of the policy.
        CHECK(c.settings.profile != "filler");
    }
    CHECK(r.chosen.confidence >= 0.0);
    CHECK(r.chosen.confidence <= 1.0);
    // A file shorter than the excerpt budget is compared in full.
    CHECK(r.excerpts.size() == 1);
    CHECK(r.excerpts[0].reason.find("whole file") != std::string::npos);
}

TT_CASE("stereo survives the full render with its image intact") {
    const CorpusItem item = make_corpus_talk(48000, 5.0, degradation_home(), 13, "stereo");
    const AudioBuffer in = to_stereo(buffer_of(item), 96, 0.8);
    AudioBuffer out;
    const AutoReport r = auto_clean(in, out);
    CHECK(out.channels == 2);
    CHECK(out.sample_rate == in.sample_rate);
    CHECK(out.frames() == in.frames());
    CHECK(r.full_file_stereo_imbalance_change_db <= 0.5);
    CHECK(r.full_file_new_clipped == 0);

    // Two channels are not proof of stereo: the content has to differ.
    double difference = 0.0;
    for (std::size_t i = 0; i < out.frames(); i++)
        difference += std::fabs(static_cast<double>(out.data[i * 2]) - out.data[i * 2 + 1]);
    difference /= static_cast<double>(out.frames());
    CHECK(difference > 1e-4);

    double left = 0.0, right = 0.0;
    for (std::size_t i = 0; i < out.frames(); i++) {
        left += static_cast<double>(out.data[i * 2]) * out.data[i * 2];
        right += static_cast<double>(out.data[i * 2 + 1]) * out.data[i * 2 + 1];
    }
    CHECK(left > 0.0);
    CHECK(right > 0.0);
}

TT_CASE("mono stays mono through the full render") {
    const CorpusItem item = make_corpus_talk(48000, 5.0, degradation_home(), 17, "mono");
    AudioBuffer out;
    auto_clean(buffer_of(item), out);
    CHECK(out.channels == 1);
    CHECK(out.frames() == item.degraded.size());
}

TT_CASE("empty input is reported, not guessed") {
    AudioBuffer empty;
    empty.sample_rate = 48000;
    empty.channels = 1;
    const AutoReport r = choose_auto_settings(empty);
    CHECK(!r.warnings.empty());
    AudioBuffer out;
    auto_clean(empty, out);
    CHECK(out.frames() == 0);
}

TT_CASE("report is valid JSON with the documented sections") {
    const CorpusItem item = make_corpus_talk(48000, 5.0, degradation_home(), 19, "report");
    const AutoReport r = choose_auto_settings(buffer_of(item));
    const std::string json = r.to_json();
    CHECK(!json.empty());
    CHECK(json.find("\"policy_version\"") != std::string::npos);
    CHECK(json.find("\"evidence_confidence\"") != std::string::npos);
    CHECK(json.find("\"chosen\"") != std::string::npos);
    CHECK(json.find("\"candidates\"") != std::string::npos);
    CHECK(json.find("\"full_file_check\"") != std::string::npos);
    CHECK(json.find("\"excerpts\"") != std::string::npos);
    CHECK(json.find("\"channel_rms_db\"") != std::string::npos);
    // Non-finite numbers would make the document unparseable for the runner.
    CHECK(json.find("nan") == std::string::npos);
    CHECK(json.find("inf") == std::string::npos);
}

int main() { return tinytest::run_all(); }
