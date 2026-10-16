// podcast-sanity - the mastering pipeline and the quality report
#include "sanity/master.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/clean.hpp"
#include "sanity/dsp.hpp"
#include "sanity/dynamics.hpp"
#include "sanity/json.hpp"
#include "sanity/loudness.hpp"
#include "sanity/metrics.hpp"

#ifndef SANITY_VERSION
#define SANITY_VERSION "0.1.0"
#endif

namespace sanity {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// A stage wrapper: records the wall clock time and the RTF. The record is
// inserted *before* the body runs, so that metrics added by the stage can find
// their own stage while it is still executing.
template <typename Fn>
void run_stage(Report& r, const char* name, bool enabled, double audio_ms, Fn&& fn) {
    r.stages.push_back(StageTiming{});
    StageTiming& st = r.stages.back();
    st.name = name;
    st.enabled = enabled;
    if (enabled) {
        const auto t0 = Clock::now();
        fn();
        st.ms = ms_since(t0);
        st.rtf = audio_ms > 0 ? st.ms / audio_ms : 0.0;
    }
}

StageTiming* stage(Report& r, const char* name) {
    for (auto& s : r.stages)
        if (s.name == name) return &s;
    return nullptr;
}

void add_metric(Report& r, const char* name, const std::string& key, double value) {
    if (StageTiming* s = stage(r, name)) s->metrics.emplace_back(key, value);
}

void write_analysis(JsonWriter& j, const Analysis& a) {
    j.begin_object();
    j.member("sample_rate", a.sample_rate);
    j.member("channels", a.channels);
    j.member("duration_s", a.duration);
    j.member("integrated_lufs", a.integrated_lufs);
    j.member("loudness_range_lu", a.lra);
    j.member("true_peak_dbtp", a.true_peak_db);
    j.member("sample_peak_dbfs", a.sample_peak_db);
    j.member("max_momentary_lufs", a.max_momentary);
    j.member("max_short_term_lufs", a.max_short_term);
    j.member("noise_floor_db", a.noise_floor_db);
    j.member("noise_avg_db", a.noise_avg_db);
    j.member("noise_regions", a.noise_regions);
    j.member("hum_freq_hz", a.hum_freq);
    j.member("hum_level_db", a.hum_db);
    j.member("hum_present", a.hum_present);
    j.member("dc_offset", a.dc_offset);
    j.member("clipped_samples", static_cast<long long>(a.clipped_samples));
    j.member("rt60_s", a.rt60);
    j.member("speech_ratio", a.speech_ratio);
    j.member("silence_ratio", a.silence_ratio);
    j.member("sibilance_db", a.sibilance_db);
    j.member("active_speech_db", a.speech_activity_db);
    j.key("region_noise_db");
    j.begin_array();
    for (double v : a.region_noise_db) j.value_double_inline(v);
    j.end_array();
    j.end_object();
}

void write_params(JsonWriter& j, const Params& p) {
    j.begin_object();
    j.member("preset", std::string(p.preset == Preset::Talk ? "talk" : "music"));
    j.member("normalize", p.normalize);
    j.member("target_lufs", p.target_lufs);
    j.member("target_true_peak_db", p.target_true_peak_db);
    j.member("max_lra", p.max_lra);
    j.member("max_short_term", p.max_short_term);
    j.member("max_momentary", p.max_momentary);
    j.member("adaptive_leveler", p.adaptive_leveler);
    j.member("leveler_strength", p.leveler_strength);
    j.member("compress", p.compress);
    j.member("adaptive_highpass", p.adaptive_highpass);
    j.member("dehum", p.dehum);
    j.member("declick", p.declick);
    j.member("deess", p.deess);
    j.member("power_clean", p.power_clean);
    j.member("denoise_strength", p.denoise_strength);
    j.member("dereverb", p.dereverb);
    const char* f = "off";
    switch (p.filler) {
        case FillerMode::Cautious: f = "cautious"; break;
        case FillerMode::Balanced: f = "balanced"; break;
        case FillerMode::Aggressive: f = "aggressive"; break;
        case FillerMode::Off: break;
    }
    j.member("filler", std::string(f));
    j.member("out_sample_rate", p.out_sample_rate);
    j.member("out_channels", p.out_channels);
    j.end_object();
}

}  // namespace

std::string Report::to_json() const {
    JsonWriter j(2);
    j.begin_object();
    j.member("tool", std::string("podcast-sanity"));
    j.member("version", version);
    j.member("params_hash", params_hash);
    j.key("params");
    write_params(j, params);

    j.key("input");
    j.begin_object();
    j.member("frames", static_cast<long long>(frames_in));
    j.member("sample_rate", before.sample_rate);
    j.member("channels", before.channels);
    j.member("duration_s", before.duration);
    j.end_object();

    j.key("output");
    j.begin_object();
    j.member("frames", static_cast<long long>(frames_out));
    j.member("sample_rate", after.sample_rate);
    j.member("channels", after.channels);
    j.member("duration_s", after.duration);
    j.end_object();

    j.key("analysis");
    j.begin_object();
    j.key("before");
    write_analysis(j, before);
    j.key("after");
    write_analysis(j, after);
    j.end_object();

    j.key("fillers");
    j.begin_object();
    j.member("detected", static_cast<long long>(fillers.size()));
    j.member("removed", static_cast<long long>(fillers_removed));
    j.member("seconds_removed", filler_seconds_removed);
    j.key("items");
    j.begin_array();
    for (const auto& f : fillers) {
        j.begin_object();
        j.member("start_s", before.sample_rate > 0
                                ? static_cast<double>(f.start) / before.sample_rate
                                : 0.0);
        j.member("duration_ms", f.duration_ms);
        j.member("score", f.score);
        j.end_object();
    }
    j.end_array();
    j.end_object();

    j.key("stages");
    j.begin_array();
    for (const auto& s : stages) {
        j.begin_object();
        j.member("name", s.name);
        j.member("enabled", s.enabled);
        j.member("ms", s.ms);
        j.member("rtf", s.rtf);
        if (!s.metrics.empty()) {
            j.key("metrics");
            j.begin_object();
            for (const auto& kv : s.metrics) j.member(kv.first, kv.second);
            j.end_object();
        }
        j.end_object();
    }
    j.end_array();

    j.key("conformance");
    j.begin_object();
    j.member("loudness_ok", conformant_loudness);
    j.member("true_peak_ok", conformant_true_peak);
    j.member("ok", conformant);
    j.end_object();

    j.key("performance");
    j.begin_object();
    j.member("total_ms", total_ms);
    j.member("realtime_factor", rtf);
    j.end_object();

    j.key("warnings");
    j.begin_array();
    for (const auto& w : warnings) j.value_string(w);
    j.end_array();

    j.end_object();
    return j.str();
}

std::string Report::to_summary() const {
    char buf[2048];
    std::string s;
    s += "podcast-sanity " + version + "\n";
    std::snprintf(buf, sizeof(buf),
                  "input : %.2f s  %d Hz  %d ch  %zu frames\n"
                  "before: %.2f LUFS  LRA %.2f LU  TP %.2f dBTP  noise floor %.1f dB\n"
                  "after : %.2f LUFS  LRA %.2f LU  TP %.2f dBTP  noise floor %.1f dB\n",
                  before.duration, before.sample_rate, before.channels, frames_in,
                  before.integrated_lufs, before.lra, before.true_peak_db, before.noise_floor_db,
                  after.integrated_lufs, after.lra, after.true_peak_db, after.noise_floor_db);
    s += buf;
    std::snprintf(buf, sizeof(buf), "fillers: %zu detected, %zu removed (%.2f s)\n",
                  fillers.size(), fillers_removed, filler_seconds_removed);
    s += buf;
    s += "stages :";
    for (const auto& st : stages) {
        if (!st.enabled) continue;
        std::snprintf(buf, sizeof(buf), " %s(%.0f ms)", st.name.c_str(), st.ms);
        s += buf;
    }
    s += "\n";
    std::snprintf(buf, sizeof(buf), "total  : %.0f ms  RTF %.3f  %s\n", total_ms, rtf,
                  conformant ? "CONFORMANT" : "NOT CONFORMANT");
    s += buf;
    for (const auto& w : warnings) s += "warning: " + w + "\n";
    return s;
}

Report master(const AudioBuffer& in, const Params& p, AudioBuffer& out) {
    Report r;
    r.version = SANITY_VERSION;
    r.params = p;
    r.params_hash = params_hash(p);
    r.frames_in = in.frames();

    const int in_rate = in.sample_rate > 0 ? in.sample_rate : 48000;
    std::vector<float> mono = to_mono(in);
    const double audio_ms = 1000.0 * static_cast<double>(mono.size()) / in_rate;

    if (mono.size() < static_cast<std::size_t>(in_rate) / 20) {
        r.warnings.push_back("input shorter than 50 ms: nothing to process");
        run_stage(r, "analyze_in", true, audio_ms, [&] { r.before = analyze(in); });
        out = in;
        r.after = r.before;
        r.frames_out = out.frames();
        return r;
    }

    // Frame features are shared by the analysis and by every stage that follows,
    // so they are computed exactly once for the input signal.
    Segmenter seg(SegmenterConfig{in_rate, 10.0, 35.0});
    std::vector<FrameFeat> feats;
    seg.features(mono, feats);

    run_stage(r, "analyze_in", true, audio_ms, [&] {
        r.before = analyze(mono, in_rate, feats);
        apply_multichannel_loudness(r.before, in);
    });

    const double floor_hz = (p.preset == Preset::Talk) ? 55.0 : 30.0;
    const double denoise_strength = p.denoise_strength >= 0.0
                                        ? p.denoise_strength
                                        : (p.power_clean ? 1.6 : 0.0);

    // ---------------------------------------------------- 1. repair
    run_stage(r, "repair", true, audio_ms, [&] {
        const double dc = remove_dc(mono);
        std::size_t clicks = 0;
        if (p.declick) clicks = declick(mono, in_rate);
        add_metric(r, "repair", "dc_removed", dc);
        add_metric(r, "repair", "clicks_repaired", static_cast<double>(clicks));
    });

    // ---------------------------------------------------- 2. hum
    run_stage(r, "dehum", p.dehum, audio_ms, [&] {
        const HumInfo h = dehum(mono, in_rate, feats, r.before.noise_floor_db, true);
        add_metric(r, "dehum", "freq_hz", h.freq);
        add_metric(r, "dehum", "level_db", h.level_db);
        add_metric(r, "dehum", "harmonics", static_cast<double>(h.harmonics.size()));
        if (h.applied) r.warnings.push_back("mains hum at " + std::to_string(static_cast<int>(h.freq)) + " Hz notched");
    });

    // ---------------------------------------------------- 3. adaptive high pass
    run_stage(r, "highpass", p.adaptive_highpass, audio_ms, [&] {
        const HighpassInfo h = adaptive_highpass(mono, in_rate, feats, floor_hz, true);
        add_metric(r, "highpass", "cutoff_min_hz", h.cutoff_min);
        add_metric(r, "highpass", "cutoff_max_hz", h.cutoff_max);
        add_metric(r, "highpass", "cutoff_used_hz", h.cutoff_used);
    });

    // ---------------------------------------------------- 4. Filler Killer
    run_stage(r, "filler", p.filler != FillerMode::Off, audio_ms, [&] {
        std::vector<FillerCandidate> cands;
        r.fillers = detect_fillers(mono, in_rate, p.filler, &cands, &feats);
        std::size_t removed = 0;
        double seconds = 0.0;
        if (!r.fillers.empty()) {
            std::vector<float> cut = remove_fillers(mono, in_rate, r.fillers);
            seconds = static_cast<double>(mono.size() - cut.size()) / in_rate;
            removed = r.fillers.size();
            mono.swap(cut);
        }
        r.fillers_removed = removed;
        r.filler_seconds_removed = seconds;
        add_metric(r, "filler", "candidates", static_cast<double>(cands.size()));
        add_metric(r, "filler", "detected", static_cast<double>(r.fillers.size()));
        add_metric(r, "filler", "removed", static_cast<double>(removed));
        add_metric(r, "filler", "seconds_removed", seconds);
    });
    // The segment features no longer line up after the cut: recompute them.
    if (p.filler != FillerMode::Off && r.fillers_removed > 0) seg.features(mono, feats);

    // ---------------------------------------------------- 4. Power Clean: denoise
    const bool do_denoise = denoise_strength > 0.0;
    run_stage(r, "denoise", do_denoise, audio_ms, [&] {
        const DenoiseInfo d = denoise(mono, in_rate, feats, denoise_strength);
        add_metric(r, "denoise", "regions", static_cast<double>(d.regions));
        add_metric(r, "denoise", "noise_floor_db", d.noise_floor_db);
        add_metric(r, "denoise", "average_reduction_db", d.reduction_db);
        if (d.regions == 0) r.warnings.push_back("no silent region found: denoiser used minimum statistics");
    });

    // ---------------------------------------------------- 6. Power Clean: dereverb
    run_stage(r, "dereverb", p.dereverb, audio_ms, [&] {
        const DereverbInfo d = dereverb(mono, in_rate, feats, 0.6);
        add_metric(r, "dereverb", "rt60_before_s", d.rt60_before);
        add_metric(r, "dereverb", "average_reduction_db", d.reduction_db);
    });

    // ---------------------------------------------------- 7. de-ess
    run_stage(r, "deess", p.deess, audio_ms, [&] {
        const DeessInfo d = deess(mono, in_rate, 1.0);
        add_metric(r, "deess", "max_reduction_db", d.max_reduction_db);
        add_metric(r, "deess", "sibilance_before_db", d.sibilance_before_db);
    });

    // ---------------------------------------------------- 8. Adaptive Leveler
    run_stage(r, "leveler", p.adaptive_leveler, audio_ms, [&] {
        const LevelerInfo l = adaptive_leveler(mono, in_rate, feats, p.leveler_strength);
        add_metric(r, "leveler", "target_db", l.target_db);
        add_metric(r, "leveler", "spread_before_db", l.spread_before_db);
        add_metric(r, "leveler", "spread_after_db", l.spread_after_db);
        add_metric(r, "leveler", "max_boost_db", l.max_boost_db);
        add_metric(r, "leveler", "max_cut_db", l.max_cut_db);
    });

    // ---------------------------------------------------- 9. compressor
    run_stage(r, "compressor", p.compress, audio_ms, [&] {
        const double thr = r.before.speech_activity_db > -60.0 ? r.before.speech_activity_db - 6.0
                                                               : -18.0;
        const CompressorInfo c = compressor(mono, in_rate, thr, 3.0, 10.0, 200.0);
        add_metric(r, "compressor", "threshold_db", c.threshold_db);
        add_metric(r, "compressor", "ratio", c.ratio);
        add_metric(r, "compressor", "average_reduction_db", c.gain_reduction_db);
    });

    // ---------------------------------------------------- 10. loudness + true peak
    const int out_channels = p.out_channels > 0 ? p.out_channels : in.channels;
    double target = p.target_lufs;
    if (target == 0.0) target = out_channels >= 2 ? -16.0 : -19.0;
    // The chain works on one channel that is later copied to `out_channels`.
    // N identical channels read channel_sum_db(N) louder on a BS.1770 meter, so
    // the mono is normalised that much below the delivery target.
    const double mono_target = target - channel_sum_db(out_channels);
    run_stage(r, "normalize", p.normalize, audio_ms, [&] {
        const LoudnessTargetInfo n =
            normalize_loudness(mono, in_rate, mono_target, p.target_true_peak_db, p.max_short_term,
                               p.max_momentary);
        add_metric(r, "normalize", "mono_target_lufs", mono_target);
        add_metric(r, "normalize", "target_lufs", n.target_lufs);
        add_metric(r, "normalize", "achieved_lufs", n.achieved_lufs);
        add_metric(r, "normalize", "error_lu", n.error_lu);
        add_metric(r, "normalize", "gain_applied_db", n.gain_applied_db);
        add_metric(r, "normalize", "iterations", static_cast<double>(n.iterations));
        add_metric(r, "normalize", "loudness_after_limit", n.loudness_after_limit);
        add_metric(r, "normalize", "true_peak_before_db", n.true_peak_before_db);
        add_metric(r, "normalize", "limiter_reduction_db", n.limiter_reduction_db);
        add_metric(r, "normalize", "limiter_max_reduction_db", n.limiter_max_reduction_db);
        add_metric(r, "normalize", "peak_rounds", static_cast<double>(n.peak_rounds));
    });

    // ---------------------------------------------------- 11. output format
    const int out_rate = p.out_sample_rate > 0 ? p.out_sample_rate : in_rate;
    const bool out_changed = out_rate != in_rate || out_channels != in.channels;
    run_stage(r, "output", out_changed, audio_ms, [&] {
        if (out_rate != in_rate) {
            mono = resample(mono, in_rate, out_rate);
            add_metric(r, "output", "resampled_to_hz", static_cast<double>(out_rate));
        }
        add_metric(r, "output", "channels", static_cast<double>(out_channels));
    });
    out = mono_to_channels(out_rate, mono, out_channels);

    r.frames_out = out.frames();
    run_stage(r, "analyze_out", true, audio_ms, [&] { r.after = analyze(out); });

    // ---------------------------------------------------- conformance
    const double loud_tol = 1.0;
    if (std::abs(r.after.integrated_lufs - target) <= loud_tol) r.conformant_loudness = true;
    else
        r.warnings.push_back("integrated loudness off target by " +
                             std::to_string(r.after.integrated_lufs - target) + " LU");
    if (r.after.true_peak_db <= p.target_true_peak_db + 0.05) r.conformant_true_peak = true;
    else
        r.warnings.push_back("true peak above ceiling: " +
                             std::to_string(r.after.true_peak_db) + " dBTP");
    r.conformant = r.conformant_loudness && r.conformant_true_peak;
    if (in.channels > 1)
        r.warnings.push_back(
            "multichannel input was summed to mono for processing and mapped back to " +
            std::to_string(out_channels) + " channels");

    double total = 0.0;
    for (const auto& s : r.stages) total += s.ms;
    r.total_ms = total;
    r.rtf = audio_ms > 0 ? total / audio_ms : 0.0;
    return r;
}

}  // namespace sanity
