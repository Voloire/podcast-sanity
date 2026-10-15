// podcast-sanity-render - headless renderer, analysis tool and corpus generator
//
// This is the same engine the desktop application drives, so everything the
// tests prove about this binary also holds for the GUI.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/corpus.hpp"
#include "sanity/dsp.hpp"
#include "sanity/loudness.hpp"
#include "sanity/master.hpp"
#include "sanity/metrics.hpp"

#ifndef SANITY_VERSION
#define SANITY_VERSION "0.1.0"
#endif

using namespace sanity;

namespace {

void usage() {
    std::printf(
        "podcast-sanity-render %s\n"
        "\n"
        "Usage:\n"
        "  podcast-sanity-render IN.wav --out OUT.wav [options]\n"
        "  podcast-sanity-render --gen-corpus OUT.wav [--profile studio|home|rough]\n"
        "  podcast-sanity-render IN.wav --analyze-only\n"
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
    bool print_json = false;
    bool summary = false;
    bool quiet = false;
    bool float_out = false;
    Params params;
};

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; i++) {
        const std::string k = argv[i];
        auto next = [&](double& v) {
            if (i + 1 < argc) v = std::atof(argv[++i]);
        };
        auto nexti = [&](int& v) {
            if (i + 1 < argc) v = std::atoi(argv[++i]);
        };
        if (k == "--help" || k == "-h") {
            usage();
            std::exit(0);
        } else if (k == "--version" || k == "-V") {
            std::printf("podcast-sanity-render %s\n", SANITY_VERSION);
            std::exit(0);
        } else if (k == "--out")
            a.out = argv[++i];
        else if (k == "--report")
            a.report = argv[++i];
        else if (k == "--truth")
            a.truth = argv[++i];
        else if (k == "--clean-out")
            a.clean_out = argv[++i];
        else if (k == "--gen-corpus")
            a.gen_corpus = argv[++i];
        else if (k == "--profile")
            a.profile = argv[++i];
        else if (k == "--seconds")
            next(a.seconds);
        else if (k == "--female")
            a.female = true;
        else if (k == "--analyze-only")
            a.analyze_only = true;
        else if (k == "--print-json")
            a.print_json = true;
        else if (k == "--summary")
            a.summary = true;
        else if (k == "--quiet")
            a.quiet = true;
        else if (k == "--float")
            a.float_out = true;
        else if (k == "--preset") {
            const std::string v = argv[++i];
            a.params.preset = v == "music" ? Preset::Music : Preset::Talk;
        } else if (k == "--lufs")
            next(a.params.target_lufs);
        else if (k == "--tp")
            next(a.params.target_true_peak_db);
        else if (k == "--max-lra")
            next(a.params.max_lra);
        else if (k == "--max-s")
            next(a.params.max_short_term);
        else if (k == "--max-m")
            next(a.params.max_momentary);
        else if (k == "--out-rate")
            nexti(a.params.out_sample_rate);
        else if (k == "--channels")
            nexti(a.params.out_channels);
        else if (k == "--power-clean") {
            a.params.power_clean = true;
            a.params.dereverb = true;
        } else if (k == "--denoise")
            next(a.params.denoise_strength);
        else if (k == "--dereverb")
            a.params.dereverb = true;
        else if (k == "--no-highpass")
            a.params.adaptive_highpass = false;
        else if (k == "--no-dehum")
            a.params.dehum = false;
        else if (k == "--no-deess")
            a.params.deess = false;
        else if (k == "--no-declick")
            a.params.declick = false;
        else if (k == "--no-leveler")
            a.params.adaptive_leveler = false;
        else if (k == "--no-compress")
            a.params.compress = false;
        else if (k == "--no-normalize")
            a.params.normalize = false;
        else if (k == "--filler") {
            const std::string v = argv[++i];
            if (v == "cautious") a.params.filler = FillerMode::Cautious;
            else if (v == "balanced") a.params.filler = FillerMode::Balanced;
            else if (v == "aggressive") a.params.filler = FillerMode::Aggressive;
            else a.params.filler = FillerMode::Off;
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
    return true;
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
    if (!a.clean_out.empty())
        wav_write(a.clean_out, from_mono(48000, item.clean), true, &err);
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

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) {
        usage();
        return 2;
    }
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

    if (a.analyze_only) {
        const std::vector<float> mono = to_mono(in);
        const Analysis an = analyze(mono, in.sample_rate);
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
        std::printf("sibilance         %.2f dB\n", an.sibilance_db);
        std::printf("active_speech     %.2f dB\n", an.speech_activity_db);
        return 0;
    }

    if (a.out.empty()) {
        std::fprintf(stderr, "--out is required (or use --analyze-only)\n");
        return 2;
    }

    AudioBuffer out;
    const Report rep = master(in, a.params, out);

    if (!wav_write(a.out, out, a.float_out, &err)) {
        std::fprintf(stderr, "cannot write %s: %s\n", a.out.c_str(), err.c_str());
        return 1;
    }
    if (!a.report.empty()) {
        FILE* f = std::fopen(a.report.c_str(), "w");
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", a.report.c_str());
            return 1;
        }
        const std::string j = rep.to_json();
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    if (a.print_json) {
        const std::string j = rep.to_json();
        std::fwrite(j.data(), 1, j.size(), stdout);
    }
    if (a.summary || (!a.quiet && !a.print_json)) {
        const std::string s = rep.to_summary();
        std::fwrite(s.data(), 1, s.size(), stdout);
    }
    return rep.conformant ? 0 : 3;
}
