// podcast-sanity-render - headless renderer, analysis tool and corpus generator
//
// This is the same engine the desktop application drives, so everything the
// tests prove about this binary also holds for the GUI. The autonomous skill
// uses the same binary: --capabilities, --analyze-only and --auto-clean are its
// stable contract.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/auto_master.hpp"
#include "sanity/corpus.hpp"
#include "sanity/dsp.hpp"
#include "sanity/encode.hpp"
#include "sanity/json.hpp"
#include "sanity/loudness.hpp"
#include "sanity/master.hpp"
#include "sanity/metrics.hpp"

#ifndef SANITY_VERSION
#error "SANITY_VERSION must be defined by the build system"
#endif

using namespace sanity;

namespace {

constexpr int kSchemaVersion = 2;

void usage() {
    std::printf(
        "podcast-sanity-render %s\n"
        "\n"
        "Usage:\n"
        "  podcast-sanity-render IN.wav --out OUT.wav|OUT.mp3 [options]\n"
        "  podcast-sanity-render IN.wav --auto-clean --out OUT.wav [--report engine.json]\n"
        "  podcast-sanity-render IN.wav --analyze-only [--print-json]\n"
        "  podcast-sanity-render --capabilities --print-json\n"
        "  podcast-sanity-render --gen-corpus OUT.wav [--profile studio|home|rough]\n"
        "\n"
        "Standards:\n"
        "  --preset talk|music         Talk or Music processing chain (default talk)\n"
        "  --lufs DB                   Integrated loudness target, 0 = auto (-16 stereo, -19 mono)\n"
        "  --tp DB                     True peak ceiling (default -1.0)\n"
        "  --max-lra LU                Maximum loudness range, 0 = unconstrained\n"
        "  --max-s LUFS                Maximum short term loudness, 0 = off\n"
        "  --max-m LUFS                Maximum momentary loudness, 0 = off\n"
        "  --out-rate HZ               Resample the output\n"
        "  --channels N                Output channels (1 or 2)\n"
        "\n"
        "Autonomous:\n"
        "  --auto-clean                Choose restoration/leveling from the material\n"
        "                              and write a channel-preserving float WAV\n"
        "  --capabilities --print-json Print the machine-readable capability record\n"
        "\n"
        "Cleaning:\n"
        "  --power-clean               Denoiser + de-reverb at full strength\n"
        "  --denoise S                 Denoiser strength 0..2 (0 disables)\n"
        "  --dereverb                  De-reverb only\n"
        "  --no-highpass|--no-dehum|--no-deess|--no-declick|--no-leveler|--no-compress\n"
        "\n"
        "Filler Killer:\n"
        "  --filler off|cautious|balanced|aggressive   (default off)\n"
        "\n"
        "Output:\n"
        "  --float                     Write 32 bit float WAV (default 16 bit)\n"
        "  --report FILE.json          Write the quality report\n"
        "  --summary                   Print the human readable report\n"
        "  --print-json                Print the JSON report to stdout\n"
        "  --quiet                     Suppress progress lines\n"
        "\n"
        "Corpus (test harness):\n"
        "  --gen-corpus OUT.wav        Synthesise a known recording\n"
        "  --profile studio|home|rough Degradation profile (default home)\n"
        "  --seconds S                 Duration (default 20)\n"
        "  --female                    Female voice\n"
        "  --clean-out FILE.wav        Also write the noise free reference\n"
        "  --truth FILE.json           Write the ground truth (fillers, params)\n"
        "\n",
        SANITY_VERSION);
}

struct Args {
    std::string in;
    std::string out;
    std::string report;
    std::string truth;
    std::string clean_out;
    std::string gen_corpus;
    std::string profile = "home";
    double seconds = 20.0;
    bool female = false;
    bool analyze_only = false;
    bool auto_clean = false;
    bool capabilities = false;
    bool print_json = false;
    bool summary = false;
    bool quiet = false;
    bool float_out = false;
    Params params;
};

bool take_value(int argc, char** argv, int& i, const char* opt, std::string& out) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "%s requires a value\n", opt);
        return false;
    }
    out = argv[++i];
    return true;
}

bool parse_double_arg(int argc, char** argv, int& i, const char* opt, double& out) {
    std::string s;
    if (!take_value(argc, argv, i, opt, s)) return false;
    try {
        std::size_t pos = 0;
        const double v = std::stod(s, &pos);
        if (pos != s.size() || !std::isfinite(v)) {
            std::fprintf(stderr, "%s: not a finite number: %s\n", opt, s.c_str());
            return false;
        }
        out = v;
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s: not a number: %s\n", opt, s.c_str());
        return false;
    }
}

bool parse_int_arg(int argc, char** argv, int& i, const char* opt, int& out) {
    std::string s;
    if (!take_value(argc, argv, i, opt, s)) return false;
    try {
        std::size_t pos = 0;
        const long v = std::stol(s, &pos);
        if (pos != s.size() || v < -1000000 || v > 1000000) {
            std::fprintf(stderr, "%s: out of range: %s\n", opt, s.c_str());
            return false;
        }
        out = static_cast<int>(v);
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s: not an integer: %s\n", opt, s.c_str());
        return false;
    }
}

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; i++) {
        const std::string k = argv[i];
        if (k == "--help" || k == "-h") {
            usage();
            std::exit(0);
        } else if (k == "--version" || k == "-V") {
            std::printf("podcast-sanity-render %s\n", SANITY_VERSION);
            std::exit(0);
        } else if (k == "--out") {
            if (!take_value(argc, argv, i, "--out", a.out)) return false;
        } else if (k == "--report") {
            if (!take_value(argc, argv, i, "--report", a.report)) return false;
        } else if (k == "--truth") {
            if (!take_value(argc, argv, i, "--truth", a.truth)) return false;
        } else if (k == "--clean-out") {
            if (!take_value(argc, argv, i, "--clean-out", a.clean_out)) return false;
        } else if (k == "--gen-corpus") {
            if (!take_value(argc, argv, i, "--gen-corpus", a.gen_corpus)) return false;
        } else if (k == "--profile") {
            if (!take_value(argc, argv, i, "--profile", a.profile)) return false;
        } else if (k == "--seconds") {
            if (!parse_double_arg(argc, argv, i, "--seconds", a.seconds)) return false;
        } else if (k == "--female") {
            a.female = true;
        } else if (k == "--analyze-only") {
            a.analyze_only = true;
        } else if (k == "--auto-clean") {
            a.auto_clean = true;
        } else if (k == "--capabilities") {
            a.capabilities = true;
        } else if (k == "--print-json") {
            a.print_json = true;
        } else if (k == "--summary") {
            a.summary = true;
        } else if (k == "--quiet") {
            a.quiet = true;
        } else if (k == "--float") {
            a.float_out = true;
        } else if (k == "--preset") {
            std::string v;
            if (!take_value(argc, argv, i, "--preset", v)) return false;
            if (v != "talk" && v != "music") {
                std::fprintf(stderr, "--preset must be talk or music\n");
                return false;
            }
            a.params.preset = v == "music" ? Preset::Music : Preset::Talk;
        } else if (k == "--lufs") {
            if (!parse_double_arg(argc, argv, i, "--lufs", a.params.target_lufs)) return false;
        } else if (k == "--tp") {
            if (!parse_double_arg(argc, argv, i, "--tp", a.params.target_true_peak_db)) return false;
        } else if (k == "--max-lra") {
            if (!parse_double_arg(argc, argv, i, "--max-lra", a.params.max_lra)) return false;
        } else if (k == "--max-s") {
            if (!parse_double_arg(argc, argv, i, "--max-s", a.params.max_short_term)) return false;
        } else if (k == "--max-m") {
            if (!parse_double_arg(argc, argv, i, "--max-m", a.params.max_momentary)) return false;
        } else if (k == "--out-rate") {
            if (!parse_int_arg(argc, argv, i, "--out-rate", a.params.out_sample_rate)) return false;
        } else if (k == "--channels") {
            if (!parse_int_arg(argc, argv, i, "--channels", a.params.out_channels)) return false;
            if (a.params.out_channels != 1 && a.params.out_channels != 2) {
                std::fprintf(stderr, "--channels must be 1 or 2\n");
                return false;
            }
        } else if (k == "--power-clean") {
            a.params.power_clean = true;
            a.params.dereverb = true;
        } else if (k == "--denoise") {
            if (!parse_double_arg(argc, argv, i, "--denoise", a.params.denoise_strength)) return false;
        } else if (k == "--dereverb") {
            a.params.dereverb = true;
        } else if (k == "--no-highpass") {
            a.params.adaptive_highpass = false;
        } else if (k == "--no-dehum") {
            a.params.dehum = false;
        } else if (k == "--no-deess") {
            a.params.deess = false;
        } else if (k == "--no-declick") {
            a.params.declick = false;
        } else if (k == "--no-leveler") {
            a.params.adaptive_leveler = false;
        } else if (k == "--no-compress") {
            a.params.compress = false;
        } else if (k == "--no-normalize") {
            a.params.normalize = false;
        } else if (k == "--filler") {
            std::string v;
            if (!take_value(argc, argv, i, "--filler", v)) return false;
            if (v == "cautious") a.params.filler = FillerMode::Cautious;
            else if (v == "balanced") a.params.filler = FillerMode::Balanced;
            else if (v == "aggressive") a.params.filler = FillerMode::Aggressive;
            else if (v == "off") a.params.filler = FillerMode::Off;
            else {
                std::fprintf(stderr, "--filler must be off, cautious, balanced or aggressive\n");
                return false;
            }
        } else if (!k.empty() && k[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", k.c_str());
            return false;
        } else if (a.in.empty()) {
            a.in = k;
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", k.c_str());
            return false;
        }
    }
    if (a.seconds <= 0.0 || a.seconds > 86400.0 ||
        a.params.denoise_strength < -1.0 || a.params.denoise_strength > 2.0 ||
        (a.params.out_sample_rate != 0 && (a.params.out_sample_rate < 8000 || a.params.out_sample_rate > 192000)) ||
        (a.params.target_lufs != 0.0 && (a.params.target_lufs < -70.0 || a.params.target_lufs > -5.0)) ||
        a.params.target_true_peak_db < -30.0 || a.params.target_true_peak_db > 0.0 ||
        a.params.max_lra < 0.0 || a.params.max_lra > 50.0) {
        std::fprintf(stderr, "numeric argument outside its supported range\n");
        return false;
    }
    return true;
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
    j.member("music_ratio", a.music_ratio);
    j.member("silence_ratio", a.silence_ratio);
    j.member("sibilance_db", a.sibilance_db);
    j.member("active_speech_db", a.speech_activity_db);
    j.member("channel_imbalance_db", a.channel_imbalance_db);
    j.member("stereo_correlation", a.stereo_correlation);
    j.key("channel_rms_db");
    j.begin_array();
    for (double v : a.channel_rms_db) j.value_double_inline(v);
    j.end_array();
    j.end_object();
}

int print_capabilities() {
    JsonWriter j(2);
    j.begin_object();
    j.member("tool", std::string("podcast-sanity-render"));
    j.member("version", std::string(SANITY_VERSION));
    j.member("schema_version", kSchemaVersion);
    j.member("auto_clean", true);
    j.member("auto_policy_version", std::string(kAutoPolicyVersion));
    j.member("max_channels", 2);
    j.member("multichannel_loudness", true);
    j.member("linked_dynamics", true);
    j.key("codecs");
    j.begin_object();
    j.member("wav", true);
    j.member("mp3", encoder_available(ExportFormat::Mp3));
    j.member("flac", encoder_available(ExportFormat::Flac));
    j.member("ogg", encoder_available(ExportFormat::Ogg));
    j.end_object();
    j.end_object();
    const std::string out = j.str();
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fputc('\n', stdout);
    return 0;
}

int gen_corpus(const Args& a) {
    Degradations d = degradation_home();
    if (a.profile == "studio") d = degradation_studio();
    else if (a.profile == "rough") d = degradation_rough();
    const CorpusItem item = make_corpus_talk(48000, a.seconds, d, d.seed, a.profile, a.female);
    std::string err;
    if (!wav_write(a.gen_corpus, from_mono(48000, item.degraded), true, &err)) {
        std::fprintf(stderr, "cannot write %s: %s\n", a.gen_corpus.c_str(), err.c_str());
        return 1;
    }
    if (!a.clean_out.empty()) wav_write(a.clean_out, from_mono(48000, item.clean), true, &err);
    if (!a.truth.empty()) {
        FILE* f = std::fopen(a.truth.c_str(), "w");
        if (!f) return 1;
        std::fprintf(f, "{\n  \"profile\": \"%s\",\n  \"sample_rate\": 48000,\n  \"seconds\": %.3f,\n",
                     a.profile.c_str(), a.seconds);
        std::fprintf(f, "  \"noise_snr_db\": %.2f,\n  \"hum_freq\": %.1f,\n  \"hum_db\": %.2f,\n",
                     d.noise_snr_db, d.hum_freq, d.hum_db);
        std::fprintf(f, "  \"rt60\": %.3f,\n  \"clip_at\": %.3f,\n", d.rt60, d.clip_at);
        std::fprintf(f, "  \"fillers\": [\n");
        for (std::size_t i = 0; i < item.fillers.size(); i++)
            std::fprintf(f, "    {\"start_s\": %.4f, \"end_s\": %.4f}%s\n",
                         static_cast<double>(item.fillers[i].start) / 48000.0,
                         static_cast<double>(item.fillers[i].end) / 48000.0,
                         i + 1 < item.fillers.size() ? "," : "");
        std::fprintf(f, "  ]\n}\n");
        std::fclose(f);
    }
    if (!a.quiet)
        std::printf("wrote %s (%.1f s, %zu hesitations, profile %s)\n", a.gen_corpus.c_str(),
                    a.seconds, item.fillers.size(), a.profile.c_str());
    return 0;
}

bool write_text_file(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return false;
    }
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    if (written != text.size()) {
        std::fprintf(stderr, "short write to %s\n", path.c_str());
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) {
        usage();
        return 2;
    }
    if (a.capabilities) return print_capabilities();
    if (!a.gen_corpus.empty()) return gen_corpus(a);
    if (a.in.empty()) {
        usage();
        return 2;
    }

    AudioBuffer in;
    std::string err;
    if (!wav_read(a.in, in, err)) {
        std::fprintf(stderr, "cannot read %s: %s\n", a.in.c_str(), err.c_str());
        return 1;
    }
    if (in.channels < 1 || in.channels > 2) {
        std::fprintf(stderr, "unsupported channel count: %d (mono or stereo only)\n", in.channels);
        return 2;
    }
    for (float sample : in.data) {
        if (!std::isfinite(sample)) {
            std::fprintf(stderr, "input contains non-finite PCM samples\n");
            return 2;
        }
    }

    if (a.analyze_only) {
        const Analysis an = analyze(in);
        if (a.print_json) {
            JsonWriter j(2);
            j.begin_object();
            j.member("tool", std::string("podcast-sanity-render"));
            j.member("version", std::string(SANITY_VERSION));
            j.member("schema_version", kSchemaVersion);
            j.key("analysis");
            write_analysis(j, an);
            j.end_object();
            const std::string out = j.str();
            std::fwrite(out.data(), 1, out.size(), stdout);
            std::fputc('\n', stdout);
        } else {
            std::printf("sample_rate       %d\n", an.sample_rate);
            std::printf("channels          %d\n", an.channels);
            std::printf("integrated_lufs   %.2f\n", an.integrated_lufs);
            std::printf("loudness_range    %.2f LU\n", an.lra);
            std::printf("true_peak         %.2f dBTP\n", an.true_peak_db);
            std::printf("sample_peak       %.2f dBFS\n", an.sample_peak_db);
            std::printf("max_momentary     %.2f LUFS\n", an.max_momentary);
            std::printf("max_short_term    %.2f LUFS\n", an.max_short_term);
            std::printf("noise_floor       %.2f dB\n", an.noise_floor_db);
            std::printf("noise_avg         %.2f dB\n", an.noise_avg_db);
            std::printf("noise_regions     %d\n", an.noise_regions);
            std::printf("hum               %.1f Hz at %.1f dB (%s)\n", an.hum_freq, an.hum_db,
                        an.hum_present ? "present" : "absent");
            std::printf("dc_offset         %.6f\n", an.dc_offset);
            std::printf("clipped_samples   %lld\n", static_cast<long long>(an.clipped_samples));
            std::printf("rt60              %.3f s\n", an.rt60);
            std::printf("speech_ratio      %.3f\n", an.speech_ratio);
            std::printf("music_ratio       %.3f\n", an.music_ratio);
            std::printf("sibilance         %.2f dB\n", an.sibilance_db);
            std::printf("active_speech     %.2f dB\n", an.speech_activity_db);
            std::printf("channel_imbalance %.2f dB\n", an.channel_imbalance_db);
            std::printf("stereo_correlation %.3f\n", an.stereo_correlation);
        }
        return 0;
    }

    if (a.out.empty()) {
        std::fprintf(stderr, "--out is required (or use --analyze-only)\n");
        return 2;
    }
    if (!known_audio_extension(a.out)) {
        std::fprintf(stderr, "unsupported output extension: %s\n", a.out.c_str());
        return 2;
    }

    if (a.auto_clean) {
        // The autonomous path is channel preserving and lossless by contract;
        // delivery normalisation and MP3 encoding happen in the runner.
        ExportOptions probe = export_options_for_path(a.out);
        if (probe.format != ExportFormat::Wav16) {
            std::fprintf(stderr, "--auto-clean writes a float WAV; use a .wav destination\n");
            return 2;
        }
        AudioBuffer out;
        const AutoReport rep = auto_clean(in, out);
        if (rep.full_file_speech_envelope_db < -12.0 ||
            rep.full_file_speech_band_db < -2.0 ||
            rep.full_file_stereo_imbalance_change_db > 0.5 ||
            rep.full_file_new_clipped > 0) {
            std::fprintf(stderr, "full-file preservation failed after bounded fallback\n");
            return 3;
        }
        for (float sample : out.data) {
            if (!std::isfinite(sample)) {
                std::fprintf(stderr, "non-finite restoration output\n");
                return 3;
            }
        }
        if (!wav_write(a.out, out, true, &err)) {
            std::fprintf(stderr, "cannot write %s: %s\n", a.out.c_str(), err.c_str());
            return 1;
        }
        const std::string j = rep.to_json();
        if (!a.report.empty() && !write_text_file(a.report, j)) return 1;
        if (a.print_json) {
            std::fwrite(j.data(), 1, j.size(), stdout);
            std::fputc('\n', stdout);
        } else if (!a.quiet) {
            std::printf("auto-clean: profile %s (confidence %.2f), %d channels, %.2f s -> %s\n",
                        rep.chosen.profile.c_str(), rep.chosen.confidence, out.channels,
                        out.duration(), a.out.c_str());
        }
        return 0;
    }

    AudioBuffer out;
    const Report rep = master(in, a.params, out);
    ExportOptions opt = export_options_for_path(a.out);
    if (a.float_out && opt.format == ExportFormat::Wav16) opt.format = ExportFormat::WavFloat32;
    opt.mp3_bitrate_kbps = recommended_mp3_bitrate(out.channels);
    if (!encode_audio(a.out, out, opt, err)) {
        std::fprintf(stderr, "cannot write %s: %s\n", a.out.c_str(), err.c_str());
        return 1;
    }
    const std::string j = rep.to_json();
    if (!a.report.empty() && !write_text_file(a.report, j)) return 1;
    if (a.print_json) {
        std::fwrite(j.data(), 1, j.size(), stdout);
        std::fputc('\n', stdout);
    }
    if (a.summary || (!a.quiet && !a.print_json)) {
        const std::string s = rep.to_summary();
        std::fwrite(s.data(), 1, s.size(), stdout);
    }
    return rep.conformant ? 0 : 3;
}
