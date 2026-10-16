# podcast-sanity Review Fixes and Studio Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the defects found in the 2026-10-16 code review, make the end-to-end harness honest at every length and sample rate, and ship the features the owner asked for: distribution-ready export profiles marked "(recommended)", a background music bed with preview, a recording-studio look for the Qt interface, and a complete Windows MSI.

**Architecture:** The engine (`sanity_core`, C++20, no external DSP) stays a mono processing chain with a quality report; this plan adds true multichannel loudness measurement around it, a `music_bed` stage before normalisation, a `distribution` module that names delivery profiles, and a libsndfile-backed reader. The Qt6 GUI keeps its seven pages and `AppState` hub; it gains a dark "Studio" theme, painted meter/transport/on-air widgets, SVG navigation icons and a music bed card. Packaging stays CPack/WiX, driven by `packaging/build-windows.ps1`.

**Tech Stack:** C++20, CMake 4.4 (Python wheel on PATH), Visual Studio 2019 Build Tools, Qt 6.5.3 msvc2019_64 at `C:\Users\Voloirex\deps\qt`, vcpkg at `C:\Users\Voloirex\deps\podcast-vcpkg` (mp3lame, libsndfile with FLAC/Ogg/Opus/mpg123), WiX 3 at `C:\Users\Voloirex\deps\wix3`, the in-repo `tests/tinytest.hpp` harness, ctest.

**Spec:** this document. Section "Review findings" is the specification: every task cites the finding IDs it closes. There is no separate design document for the new features; their design is written into the tasks.

## Global Constraints

- Every artifact is English: code, comments, commit messages, docs. Italian only in user-facing GUI strings, which are already Italian (keep that convention, do not translate the GUI).
- No AI, model, vendor or assistant is ever named as author or co-author: no `Co-Authored-By`, no "Generated with" lines in commits or pull requests.
- Work on branch `review-2026-10-16` created from `master` after Task 0.1; one commit per task with the message given in the task; merge only through a pull request; never push to `master` directly. Git remotes stay SSH.
- Toolchain on this machine (capybara, Windows 11): configure and build with `packaging\build-windows.ps1` (full) or, for iteration, `cmake --build build-windows --config Release --parallel` then `ctest --test-dir build-windows -C Release --output-on-failure`. `rg` is not installed; use `Select-String` or the Grep tool. PowerShell 5.1: no `&&`.
- The ctest suites `unit`, `e2e`, `gui` (plus the ones this plan adds) must pass after every task. A task that changes the engine output must keep the `e2e` determinism check green.
- Nothing is read from or written to the network during tests or at runtime (the product promise is "local, offline"). Fonts and images are bundled resources.
- Do not touch `~/myshare/memoria`, machine instructions, or anything outside this repository.
- New identifiers use the existing style: `snake_case` functions, `PascalCase` types, `k` prefix for constants, 4-space indent, 100-column lines, `// podcast-sanity - <purpose>` header comment in new files.
- Ubuntu is the default typeface for anything this project publishes; the GUI bundles the Ubuntu font (Ubuntu Font Licence 1.0) in Phase 5.
- Linux CI (`.github/workflows/ci.yml`, jobs `linux` and `sanitizers`) is out of scope for verification in this plan: the owner reviews it on the Ubuntu machine. Keep it compiling (no Windows-only API without `#ifdef _WIN32`), nothing more.

## Review Focus

Five conditions a user will hit that no existing test exercises. Each line names the owning task, which adds the test.

1. A stereo export at the default target: the file reports -16 LUFS but a real meter reads -13 LUFS because dual-mono doubles the BS.1770 channel sum. Owner: Task 1.1.
2. A 44.1 kHz recording through the Filler Killer: precision drops to 0.5 and a word is cut, because the detector was calibrated at 48 kHz only. Owner: Task 2.1.
3. Material that needs heavy limiting (quiet speech with loud transients): the last normalisation round applies a gain-up after the limiter and leaves the true peak above the ceiling. Owner: Task 1.4.
4. A library containing a long session: listing the library allocates a zero-filled buffer the size of the whole episode for every row (1.4 GB for two hours). Owner: Task 1.5.
5. A crash recovery of a 44.1 kHz stereo take: the recovery path decodes it as 48 kHz mono, so the audio plays at the wrong speed. Owner: Task 2.6.

---

## Review findings (the specification)

Severity: **P1** data loss, memory safety or a wrong delivery; **P2** wrong behaviour a user will meet; **P3** quality, performance, doc drift.

### Engine

| ID | Sev | Where | Finding |
|----|-----|-------|---------|
| F01 | P1 | `src/pipeline.cpp:373-390`, `src/analyze.cpp` | Loudness is measured and normalised on the mono sum, then the mono is duplicated to `out_channels`. BS.1770 sums channel powers, so a dual-mono stereo file is 3.01 dB louder than reported. Verified: `--channels 2` output reports -16.0 LUFS, `ffmpeg -af ebur128` reads -13.0 LUFS. Stereo input is measured on the average of channels, also wrong. |
| F02 | P1 | `src/corpus.cpp:257-263` | The hesitation loop writes `voiced[start + i]` without the `pos + i < total` guard the word loop has. The loop condition only guarantees `pos + 0.5 s < total`, the pause plus vowel can add up to 0.9 s. Simulated on the exact random stream: 13 of 480 seed/length combinations write past the end of the vector (seed 11 = `studio` profile at 12 s, seed 8 at 12 s, seed 4 at 20 s, ...). Heap corruption in the test harness, the CLI `--gen-corpus`, the GUI self test and the GUI corpus recording mode. |
| F03 | P1 | `src/dsp.cpp:597-618` | `std::atomic<int> next` is declared inside the `try` block but the worker threads that capture it by reference are joined after the block. The atomic is destroyed while threads may still call `fetch_add`: use after scope on every multi-threaded feature pass. |
| F04 | P1 | `src/dynamics.cpp:322-339` | In the last of the four peak-control rounds, `apply_gain_db(x, err)` runs after the limiter and the loop exits. A positive `err` pushes the true peak back above the ceiling and nothing re-limits. |
| F05 | P2 | `src/pipeline.cpp:393-402` | Output resampling runs after the limiter. Resampling creates new inter-sample peaks and shifts loudness (measured: -19.19 LUFS at 44.1 kHz output from a -19.00 master). Resample before normalisation. |
| F06 | P2 | `src/analyze.cpp:85`, `src/pipeline.cpp:137-147` | `Analysis::channels` is always 1, so the report's `input.channels` and `output.channels` are 1 even for stereo (verified with `--channels 2`). |
| F07 | P2 | `src/pipeline.cpp:408-417`, `docs/STANDARDS.md` section 4 | `Params::max_lra`, `max_short_term`, `max_momentary` never enter `conformant`, although STANDARDS.md says `ok` includes them. `max_lra` is accepted by the CLI and the GUI and does nothing at all. |
| F08 | P2 | `src/encode.cpp:51-66` | MP3 export at a non-LAME rate (e.g. 96 kHz) downmixes to mono and then duplicates to stereo whatever the input was; channel count and stereo content are lost. |
| F09 | P2 | `src/encode.cpp:171-174` | `SFC_SET_COMPRESSION_LEVEL` takes a double in 0..1; `flac_compression = 5` is rejected silently. |
| F10 | P2 | `src/filler.cpp` | Detector thresholds were calibrated at 48 kHz. At 44.1 kHz the `rough` profile gives precision 0.5 and over-removal (`removed 0.690 s > 0.685 s`), e2e fails. |
| F11 | P2 | `tests/e2e_main.cpp:314,359` | The harness fails at the lengths TESTING.md says it was calibrated on: at 30 s `speech_preservation_db = 5.86 < 6.0`; at 45 s `rough` SNR gain `+1.76 < +2.0`. ctest only runs the 12 s default, so the claim "calibrated at 20, 30 and 45 s" is unverified. |
| F12 | P2 | `src/json.cpp`, `include/sanity/json.hpp:536-540` | Every scalar member is written as `"key": \n    value`: `comma()` returns early after a key but `newline()` still runs. Valid JSON, unreadable report. |
| F13 | P2 | `cli/render_main.cpp:106-175` | `argv[++i]` with no bound check: `podcast-sanity-render in.wav --out` crashes with 0xC0000005 (verified). |
| F14 | P2 | `cli/render_main.cpp:276` | The CLI only writes WAV. An `--out episode.mp3` silently produces a WAV with an `.mp3` extension. |
| F15 | P2 | `tests/test_main.cpp:405,419` | Unit tests write to `/tmp/...`, which on Windows resolves to `C:\tmp` and only works because that folder happens to exist here. |
| F16 | P3 | `src/dsp.cpp:300-315` | `Resampler::process` on an empty input indexes `in[-1]` (reflection formula with `n_in == 0`). Callers never pass empty today. |
| F17 | P3 | `src/clean.cpp:211-221` | Hum detection compares the mains tone with neighbours 3 to 12 Hz away using a Goertzel over the longest quiet region. A 200 ms region has 5 Hz resolution, so the 3 and 5 Hz probes sit inside the main lobe and the excess is underestimated. |
| F18 | P3 | `src/clean.cpp:284-293` | With fewer than 20 voiced frames (music preset, very short takes) the adaptive high pass is skipped entirely, not applied at `floor_hz`. Rumble stays. |
| F19 | P3 | `src/filler.cpp:62-64`, `src/pipeline.cpp:309` | `detect_fillers` recomputes the `Segmenter` features the pipeline already holds: the most expensive pass runs twice. |
| F20 | P3 | `src/metrics.cpp:209-238` | `score_intervals` accepts a detection that starts inside the truth interval and ends anywhere after it; `det.end + tol >= a` is always true. Unused by e2e (which has its own overlap scorer) but exported. |
| F21 | P3 | `src/encode.cpp:242,266-270` | `.opus` maps to Ogg Vorbis (wrong codec in an `.opus` file). WAV 24 bit without libsndfile silently writes float32 while the label says 24 bit. |
| F22 | P3 | `src/corpus.cpp:103`, `src/clean.cpp:506,524`, `src/dsp.cpp:661`, `include/sanity/dsp.hpp:293` | Dead expressions and members: `sqrt(1 - a1*r*r/4*0 + 0)`, `(void)atk`, `(void)after_sum`, `(void)peak_db`, unused `Stft::norm_`. |
| F23 | P3 | `src/io_wav.cpp` | Whole file is read into a byte vector and then into floats (two copies of a multi-GB episode); 12-bit or other odd depths decode as silence. |

### GUI

| ID | Sev | Where | Finding |
|----|-----|-------|---------|
| F24 | P1 | `gui/app_state.cpp:264-273` | `library_sessions()` allocates `seconds * rate * channels` zero floats per listed session only to carry a duration. Two hours stereo at 48 kHz is 1.4 GB per row; the library page calls it on every keystroke of the search box. |
| F25 | P2 | `gui/app_state.cpp:226-230` | `cancel_mastering()` sets `busy_ = false` but the worker result is still adopted when it arrives; DESIGN.md promises the opposite. |
| F26 | P2 | `gui/app_state.cpp:85-91` | `Worker::run_job` has no exception guard; `std::bad_alloc` on a long file terminates the process. |
| F27 | P2 | `gui/pages/settings_page.cpp:137-143`, `gui/mainwindow.cpp` | Changing the theme on the Settings page emits `theme_changed`, which nobody connects. The stylesheet is not reapplied until restart, while painted widgets switch colours immediately (half-themed window). |
| F28 | P2 | `gui/pages/record_page.cpp:218-233,400-412` | Crash recovery hardcodes 48000 Hz mono although `recording-live.json` stores the real rate and channel count, and `stop_recording()` decodes with the combo-box values rather than the rate the device actually gave `Capture`. |
| F29 | P2 | `gui/pages/library_page.cpp:168` | "Mostra cartella" runs `xdg-open`: does nothing on Windows. |
| F30 | P2 | `gui/pages/export_page.cpp:252-269` | Once a report exists, the channel and rate combo boxes are ignored: the file is written from the mastered buffer whatever the user selected afterwards. |
| F31 | P2 | `gui/pages/diagnostics_page.cpp:280-314` | Detached `std::thread` capturing `this`; closing the window during a harness run is a use after free. |
| F32 | P2 | `gui/app_state.cpp:375-380` | `load_session()` ignores `report.json` and sets `has_report = has_mastered` with an empty `Report`: Standards and Export pages show dashes and "not conformant" for a saved, conformant session. |
| F33 | P3 | `gui/pages/record_page.cpp:185-195,442-452,497-516` | Decorative controls: anti-hum, VAD split, monitoring and monitor device do nothing; the "True peak" meter shows the sample peak; the mic test "noise floor" is `peak - 40`. |
| F34 | P3 | `gui/mainwindow.cpp:33-41`, `gui/pages/record_page.cpp:81-98` | Navigation uses ASCII placeholders (`[ * ]`), transport uses Unicode glyphs that fall back to a different font on Windows. |
| F35 | P3 | `gui/pages/master_page.cpp:55-61,459-485,487-505` | "Carica file audio" only prints a hint; `show_candidates()` and `play()` run heavy work on the GUI thread. |
| F36 | P3 | `gui/pages/standards_page.cpp:257-260`, `gui/mainwindow.cpp:190-193` | File dialogs offer FLAC/Ogg/MP3 but only `wav_read` exists. |
| F37 | P3 | `gui/pages/settings_page.cpp:68-74,80` | "Formato di esportazione predefinito" is saved and never read by the Export page. |

### Build, packaging, docs

| ID | Sev | Where | Finding |
|----|-----|-------|---------|
| F38 | P2 | working tree | The Windows port (MSVC flags, codec detection, windeployqt, MSI, Unicode export test, `--list-audio-devices`) is uncommitted: 10 modified and 7 untracked files. Baseline it first. |
| F39 | P2 | `CMakeLists.txt:220`, `packaging/windows.rc.in` | `CPACK_PACKAGE_CONTACT "Voloire <dev@voloire.local>"` puts a fake e-mail into the MSI metadata; the policy is name only. |
| F40 | P3 | `build-windows/.../wix.log` | WiX ICE61 (same-version upgrade range), ICE69 (shortcut component), ICE90/91 (per-user directories) warnings. Harmless for a per-user MSI; document as accepted. |
| F41 | P3 | `CMakeLists.txt:176-179`, `packaging/deploy-windows.cmake.in` | Payload carries `opengl32sw.dll` (20 MB), TLS, network-information and generic plugins the app never loads. |
| F42 | P3 | `docs/TESTING.md`, `README.md`, `docs/DESIGN-podcast-sanity.md` | Counts are stale ("22 unit cases / 127 checks"; the Windows run is 20 cases / 162 checks without libebur128, 23 with). TESTING.md lists a JSON writer test that does not exist. DESIGN.md mentions a `wix_patch.xml` that does not exist. STANDARDS.md says true peak is measured "on the written output"; it is measured in memory before quantisation. |
| F43 | P3 | repo root | No `.gitattributes`; Git prints CRLF conversion warnings on every diff on Windows. |

### Verified state before this plan (capybara, 2026-10-16)

```
ctest --test-dir build-windows -C Release        unit 20 cases/162 checks PASS, e2e 8/42 PASS, gui PASS
sanity_e2e_tests --seconds 30                    FAIL speech_preservation_db 5.86 < 6
sanity_e2e_tests --seconds 45                    FAIL rough snr gain 13.89 < 14.12
sanity_e2e_tests --rate 44100                    FAIL rough precision 0.50, removed 0.690 s > 0.685 s
render --channels 2 (auto target)                report -16.0 LUFS; ffmpeg ebur128 -13.0 LUFS
render in.wav --out                              exit 0xC0000005
```

---

## How the executing agent works

1. Task 0.1 commits the working tree on `master`, then `git switch -c review-2026-10-16`.
2. Build once with `.\packaging\build-windows.ps1` (configures `build-windows`, builds, runs ctest, packs). For every later task use the incremental commands from Global Constraints. The GUI test writes screenshots when you pass `--selftest-shots`; use that to check UI tasks:
   `.\build-windows\Release\podcast-sanity.exe --selftest --selftest-seconds 8 --selftest-shots .\build-windows\shots`.
3. Tests live in `tests/test_main.cpp` (unit, `TT_CASE`) and `tests/e2e_main.cpp` (harness). `CHECK`, `CHECK_NEAR(a, b, tol)`, `CHECK_LE`, `CHECK_GE` are the only assertions. A new `TT_CASE` registers itself; nothing else to wire. New test helpers go in the anonymous namespace near the existing `sine()` / `noise()` helpers.
4. Commit after each task with the exact message given. Do not amend. Do not squash.
5. When a step's expected output differs from what you see, stop and report the difference; do not change a threshold to make a test pass unless the task says so.
6. Phases 1 and 2 are sequential (they touch the same files). Phases 3, 4, 5 and 6 are independent of each other once Phase 2 is done.

---

## Phase 0: Baseline

### Task 0.1: Commit the Windows port that is sitting in the working tree

Closes F38, F43.

**Files:**
- Create: `.gitattributes`
- Commit as-is: `.github/workflows/ci.yml`, `CMakeLists.txt`, `README.md`, `docs/TESTING.md`, `gui/main.cpp`, `packaging/README.md`, `src/corpus.cpp`, `src/encode.cpp`, `src/io_wav.cpp`, `tests/test_main.cpp`, `data/podcast-sanity.ico`, `packaging/THIRD-PARTY.md`, `packaging/build-windows.ps1`, `packaging/deploy-windows.cmake.in`, `packaging/generate-icon.py`, `packaging/test-windows-msi.ps1`, `packaging/windows.rc.in`, and this plan under `docs/superpowers/plans/`.

- [ ] **Step 1: Confirm the tree builds and tests pass as it stands**

Run: `ctest --test-dir build-windows -C Release --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 3`

- [ ] **Step 2: Add line-ending rules so Windows stops rewriting files**

```gitattributes
# Normalise to LF in the repository; Windows editors see LF too.
* text=auto eol=lf
*.ps1 text eol=crlf
*.bat text eol=crlf
*.ico binary
*.bmp binary
*.png binary
*.ttf binary
```

- [ ] **Step 3: Commit the baseline on master, then branch**

```bash
git add -A
git commit -m "Windows port: MSVC build, codec detection, windeployqt, per-user MSI, Unicode export test"
git switch -c review-2026-10-16
```

Expected: `git status` clean; `git log -1 --stat` lists the files above.

---

## Phase 1: P1 defects

### Task 1.1: True multichannel loudness and a correct stereo target

Closes F01, F06. Review Focus 1.

**Files:**
- Modify: `include/sanity/loudness.hpp`
- Modify: `src/loudness.cpp`
- Modify: `include/sanity/master.hpp` (Analysis gets a buffer overload)
- Modify: `src/analyze.cpp`
- Modify: `src/pipeline.cpp:246-428`
- Test: `tests/test_main.cpp`, `tests/e2e_main.cpp`

**Interfaces:**
- Produces: `LoudnessStats measure_loudness(const AudioBuffer& b);` `double integrated_lufs(const AudioBuffer& b);` `double channel_sum_db(int channels);` `Analysis analyze(const AudioBuffer& b);` `void apply_multichannel_loudness(Analysis& a, const AudioBuffer& b);`
- Later tasks (4.x music bed, 3.x distribution) rely on `analyze(const AudioBuffer&)` reporting the real multichannel loudness and `Analysis::channels` being the buffer's channel count.

- [ ] **Step 1: Write the failing unit tests**

Append to `tests/test_main.cpp` before `int main()`:

```cpp
// ------------------------------------------------------- multichannel loudness
TT_CASE("dual mono stereo measures 3.01 dB louder than the mono signal (BS.1770 channel sum)") {
    const int fs = 48000;
    const auto mono = sine(1000.0, 0.1, 5.0, fs);
    const AudioBuffer stereo = mono_to_channels(fs, mono, 2);
    const double l_mono = measure_loudness(mono, fs).integrated_lufs;
    const double l_stereo = measure_loudness(stereo).integrated_lufs;
    CHECK_NEAR(l_stereo - l_mono, 3.0103, 0.05);
    CHECK_NEAR(channel_sum_db(2), 3.0103, 1e-6);
    CHECK_NEAR(channel_sum_db(1), 0.0, 1e-9);
}

TT_CASE("a stereo file with one silent channel measures like the mono signal") {
    const int fs = 48000;
    const auto mono = sine(1000.0, 0.1, 5.0, fs);
    AudioBuffer b = AudioBuffer::zeros(fs, 2, mono.size());
    for (std::size_t i = 0; i < mono.size(); i++) b.data[2 * i] = mono[i];
    CHECK_NEAR(measure_loudness(b).integrated_lufs, measure_loudness(mono, fs).integrated_lufs, 0.05);
    CHECK_NEAR(measure_loudness(b).true_peak_db, true_peak_db(mono, fs), 1e-6);
}
```

- [ ] **Step 2: Run the unit tests to verify they fail to compile**

Run: `cmake --build build-windows --config Release --target sanity_tests`
Expected: errors: no matching overload for `measure_loudness(const AudioBuffer&)`, `channel_sum_db` undeclared.

- [ ] **Step 3: Add the multichannel measurement to the loudness module**

In `include/sanity/loudness.hpp` add `#include "sanity/audio.hpp"` and, after `measure_loudness(const std::vector<float>&, int)`:

```cpp
// BS.1770-4 for an interleaved buffer: every channel is K-weighted on its own,
// the block energies are summed over channels (weight 1.0 each; surround
// weights are out of scope) and gated once. The true peak is the maximum over
// channels.
LoudnessStats measure_loudness(const AudioBuffer& b);
double integrated_lufs(const AudioBuffer& b);

// 10 log10(N): how much louder N identical channels read than one of them.
double channel_sum_db(int channels);
```

In `src/loudness.cpp` (anonymous namespace) add two helpers and keep `kweight_prefix` as a wrapper over them:

```cpp
// K-weights one interleaved channel and ADDS its energy into `energy`, so one
// call per channel yields the BS.1770 channel sum.
void kweight_energy_add(const float* x, std::size_t frames, int stride, int sample_rate,
                        std::vector<double>& energy) {
    KFilter kf;
    kf.k = k_weighting_for(sample_rate);
    for (std::size_t i = 0; i < frames; i++) {
        const double y = kf.process(x[i * static_cast<std::size_t>(stride)]);
        energy[i] += y * y;
    }
}

// Turns per-sample energies into the prefix sum the gating functions expect
// (frames + 1 entries, prefix[0] == 0).
void prefix_from_energy(std::vector<double>& energy) {
    double acc = 0.0;
    for (double& e : energy) {
        const double v = e;
        e = acc;
        acc += v;
    }
    energy.push_back(acc);
}

void kweight_prefix(const std::vector<float>& mono, int sample_rate, std::vector<double>& prefix) {
    prefix.assign(mono.size(), 0.0);
    kweight_energy_add(mono.data(), mono.size(), 1, sample_rate, prefix);
    prefix_from_energy(prefix);
}
```

Move the body of `loudness_range()` after its K-weighting loop into `double loudness_range_from_prefix(const std::vector<double>& prefix, std::size_t n, int sample_rate)` (anonymous namespace; gating on energies, -20 LU relative gate, 10/95 percentiles unchanged) and let `loudness_range()` call `kweight_prefix` then that helper.

Then add the public functions:

```cpp
double channel_sum_db(int channels) {
    return channels > 1 ? 10.0 * std::log10(static_cast<double>(channels)) : 0.0;
}

double integrated_lufs(const AudioBuffer& b) {
    const std::size_t n = b.frames();
    if (n == 0 || b.sample_rate <= 0 || b.channels <= 0) return -1e30;
    std::vector<double> energy(n, 0.0);
    for (int c = 0; c < b.channels; c++)
        kweight_energy_add(b.data.data() + c, n, b.channels, b.sample_rate, energy);
    prefix_from_energy(energy);
    return integrated_from_prefix(energy, n, b.sample_rate);
}

LoudnessStats measure_loudness(const AudioBuffer& b) {
    LoudnessStats s;
    const std::size_t n = b.frames();
    if (n == 0 || b.sample_rate <= 0 || b.channels <= 0) return s;
    if (b.channels == 1) return measure_loudness(b.data, b.sample_rate);

    std::vector<double> energy(n, 0.0);
    double peak = 0.0, dc = 0.0;
    long long clipped = 0;
    for (int c = 0; c < b.channels; c++) {
        kweight_energy_add(b.data.data() + c, n, b.channels, b.sample_rate, energy);
        for (std::size_t i = 0; i < n; i++) {
            const double x = b.data[i * static_cast<std::size_t>(b.channels) + c];
            peak = std::max(peak, std::fabs(x));
            dc += x;
            if (std::fabs(x) >= 0.9999) clipped++;
        }
    }
    prefix_from_energy(energy);
    const std::vector<double>& prefix = energy;
    s.dc_offset = dc / static_cast<double>(n * static_cast<std::size_t>(b.channels));
    s.clipped_samples = clipped;
    s.sample_peak_db = peak > 0 ? 20.0 * std::log10(peak) : -1e30;
    s.integrated_lufs = integrated_from_prefix(prefix, n, b.sample_rate);
    if (s.integrated_lufs < -1e29) return s;
    s.has_signal = true;

    const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(b.sample_rate) / 10);
    const std::size_t mw = static_cast<std::size_t>(b.sample_rate) * 4 / 10;
    const std::size_t sw = static_cast<std::size_t>(b.sample_rate) * 3;
    for (std::size_t a = 0; a + mw <= n; a += hop)
        s.max_momentary = std::max(s.max_momentary, lufs_from_ms(ms_range(prefix, a, a + mw)));
    for (std::size_t a = 0; a + sw <= n; a += hop) {
        const double l = lufs_from_ms(ms_range(prefix, a, a + sw));
        if (l <= -70.0) continue;
        s.max_short_term = std::max(s.max_short_term, l);
        s.min_short_term = std::min(s.min_short_term, l);
    }
    if (s.min_short_term > 1e29) s.min_short_term = s.integrated_lufs;

    std::vector<float> ch(n);
    double tp = -1e30;
    for (int c = 0; c < b.channels; c++) {
        for (std::size_t i = 0; i < n; i++)
            ch[i] = b.data[i * static_cast<std::size_t>(b.channels) + c];
        tp = std::max(tp, true_peak_db(ch, b.sample_rate));
    }
    s.true_peak_db = tp;
    s.lra = loudness_range_from_prefix(prefix, n, b.sample_rate);
    return s;
}
```

- [ ] **Step 4: Give `Analysis` a buffer overload and the real channel count**

In `include/sanity/master.hpp` after the two `analyze` declarations:

```cpp
// Measures an interleaved buffer: loudness, true peak and peak statistics are
// BS.1770 multichannel values; the content features (noise, hum, RT60, speech
// ratio) are measured on the mono sum, which is what the chain processes.
Analysis analyze(const AudioBuffer& b);
// Overwrites the loudness and peak fields of `a` with the multichannel
// measurement of `b` and sets a.channels. No-op for mono.
void apply_multichannel_loudness(Analysis& a, const AudioBuffer& b);
```

In `src/analyze.cpp`:

```cpp
void apply_multichannel_loudness(Analysis& a, const AudioBuffer& b) {
    a.channels = b.channels;
    if (b.channels <= 1) return;
    const LoudnessStats m = measure_loudness(b);
    a.integrated_lufs = m.integrated_lufs;
    a.lra = m.lra;
    a.true_peak_db = m.true_peak_db;
    a.sample_peak_db = m.sample_peak_db;
    a.max_momentary = m.max_momentary;
    a.max_short_term = m.max_short_term;
    a.dc_offset = m.dc_offset;
    a.clipped_samples = m.clipped_samples;
}

Analysis analyze(const AudioBuffer& b) {
    Analysis a = analyze(to_mono(b), b.sample_rate);
    apply_multichannel_loudness(a, b);
    return a;
}
```

- [ ] **Step 5: Normalise the mono to the channel-corrected target and measure the real output**

In `src/pipeline.cpp`:

1. After `const int out_channels = ...; double target = ...;` add
   ```cpp
   // The chain works on one channel that is later copied to `out_channels`.
   // N identical channels read channel_sum_db(N) louder on a BS.1770 meter, so
   // the mono is normalised that much below the delivery target.
   const double mono_target = target - channel_sum_db(out_channels);
   ```
   pass `mono_target` to `normalize_loudness(...)` and add `add_metric(r, "normalize", "mono_target_lufs", mono_target);`.
2. Replace `run_stage(r, "analyze_out", true, audio_ms, [&] { r.after = analyze(mono, out_rate); });` with `[&] { r.after = analyze(out); }`.
3. After `r.before = analyze(mono, in_rate, feats);` add `apply_multichannel_loudness(r.before, in);`.
4. In the short-input early return path use `r.before = analyze(in);`.
5. The conformance check keeps comparing `r.after.integrated_lufs` with `target` (now both are multichannel values).

- [ ] **Step 6: Add the e2e stereo scenario**

In `tests/e2e_main.cpp`, add a helper next to `print_scenario` and a case after the `music preset` case:

```cpp
void print_line(const char* name, double lufs, double tp) {
    std::printf("    %-8s lufs %.2f tp %.2f\n", name, lufs, tp);
}
```

```cpp
TT_CASE("e2e: a stereo delivery reads -16 LUFS on a multichannel BS.1770 meter") {
    if (!wanted("stereo")) return;
    Params p = base_params();
    p.out_channels = 2;  // target_lufs stays 0 = auto, which must pick -16 for stereo
    CorpusItem item = make_item("home", opts().seconds);
    const AudioBuffer in = buffer_of(item.degraded, item.sample_rate);
    AudioBuffer out;
    const Report rep = master(in, p, out);
    CHECK(out.channels == 2);
    CHECK(rep.after.channels == 2);
    const LoudnessStats real = measure_loudness(out);
    print_line("stereo", real.integrated_lufs, real.true_peak_db);
    CHECK(std::fabs(real.integrated_lufs - (-16.0)) <= 1.0);
    CHECK_LE(real.true_peak_db, -1.0 + 0.02);
    CHECK(std::fabs(rep.after.integrated_lufs - real.integrated_lufs) < 0.01);
    CHECK(rep.conformant);
}
```

- [ ] **Step 7: Build and run all three suites**

Run: `cmake --build build-windows --config Release --parallel` then `ctest --test-dir build-windows -C Release --output-on-failure`
Expected: unit adds 2 cases, e2e adds 1 case, all PASS. The mono e2e lines still print `lufs -19.00`.

- [ ] **Step 8: Cross-check with ffmpeg once by hand**

```powershell
.\build-windows\Release\podcast-sanity-render.exe --gen-corpus $env:TEMP\c.wav --profile home --seconds 20 --quiet
.\build-windows\Release\podcast-sanity-render.exe $env:TEMP\c.wav --out $env:TEMP\m2.wav --channels 2 --quiet
ffmpeg -nostats -hide_banner -i $env:TEMP\m2.wav -af ebur128=peak=true -f null - 2>&1 | Select-String 'I:'
```
Expected: `I: -16.0 LUFS` (tolerance 0.3). Put the measured value in the commit body.

- [ ] **Step 9: Commit**

```bash
git add include/sanity/loudness.hpp src/loudness.cpp include/sanity/master.hpp src/analyze.cpp src/pipeline.cpp tests/test_main.cpp tests/e2e_main.cpp
git commit -m "Loudness: measure multichannel buffers per BS.1770 and normalise the mono to the channel-corrected target"
```

### Task 1.2: Bound the corpus generator

Closes F02 and the first item of F22.

**Files:**
- Modify: `src/corpus.cpp:103, 241-270, 414-429`
- Test: `tests/test_main.cpp`

- [ ] **Step 1: Write the regression test**

Add `#include "sanity/corpus.hpp"` to the include block of `tests/test_main.cpp`, then:

```cpp
TT_CASE("corpus generator stays inside its buffers for every seed and length") {
    // Seeds 8 and 11 at 12 s and seed 4 at 20 s used to write past the end of
    // the voiced buffer (hesitation vowel not clamped to the total length).
    for (unsigned seed : {4u, 8u, 11u, 3u, 6u, 22u, 33u}) {
        for (double seconds : {8.0, 12.0, 20.0, 60.0}) {
            const CorpusItem it =
                make_corpus_talk(48000, seconds, degradation_rough(), seed, "bounds", false);
            const std::size_t total = static_cast<std::size_t>(seconds * 48000);
            CHECK(it.clean.size() == total);
            CHECK(it.degraded.size() == total);
            for (const Span& f : it.fillers) CHECK(f.end <= total && f.start < f.end);
            for (const Span& w : it.words) CHECK(w.end <= total && w.start <= w.end);
        }
    }
}
```

The heap write itself is only observable under a sanitizer (the Linux `sanitizers` CI job); this test pins the span contract that the fix below enforces.

- [ ] **Step 2: Run it**

Run: `cmake --build build-windows --config Release --target sanity_tests` then `.\build-windows\Release\sanity_tests.exe`
Expected: the case runs (it may pass, crash or corrupt silently); continue.

- [ ] **Step 3: Clamp the hesitation to the buffer**

In `make_corpus_talk`, the `make_filler` branch becomes:

```cpp
        if (make_filler) {
            // A hesitation is isolated between two pauses, the way a speaker
            // actually stops and searches for a word.
            pos += static_cast<std::size_t>((0.25 + 0.15 * rnd.uniform()) * sample_rate);
            const double dur = 0.30 + 0.20 * rnd.uniform();
            if (pos >= total) break;
            // Never write past the end of the take: the loop guard only
            // guarantees half a second of room.
            const std::size_t n = std::min(static_cast<std::size_t>(dur * sample_rate), total - pos);
            const std::size_t start = pos;
            ... (unchanged synthesis loop over i < n)
            item.fillers.push_back({start, start + n});
```

Guard the degradations: wrap the clicks loop in `if (x.size() >= 3)` and the dropouts loop in `if (x.size() >= 2)`. In `Resonator::process` replace the dead expression with `const double gain = 1.0 - r;`.

- [ ] **Step 4: Verify the harness material is unchanged**

Run: full ctest. Expected: all PASS; the e2e `home` line still prints `lufs -19.00 ... filler P 1.00 R 1.00 (1/1)` (seed 7 never overflowed, so the material is identical).

- [ ] **Step 5: Commit**

```bash
git add src/corpus.cpp tests/test_main.cpp
git commit -m "Corpus: clamp the hesitation vowel to the take length (heap overrun for 13 of 480 seed/length pairs)"
```

### Task 1.3: Fix the atomic lifetime in the feature thread pool

Closes F03.

**Files:**
- Modify: `src/dsp.cpp:594-623`

- [ ] **Step 1: Restructure so every object the threads touch outlives the join**

```cpp
    if (ntask <= 1) {
        work(0, nf);
    } else {
        std::atomic<int> next{0};
        std::vector<std::thread> pool;
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        const int nthreads = std::min<int>(ntask, static_cast<int>(hw));
        try {
            for (int t = 0; t < nthreads; t++) {
                pool.emplace_back([&] {
                    for (;;) {
                        const int i = next.fetch_add(1);
                        if (i >= ntask) break;
                        const int a = i * grain;
                        work(a, std::min(nf, a + grain));
                    }
                });
            }
        } catch (...) {
            // Thread creation failed part way: the threads that did start keep
            // draining `next`; whatever is left is done on this thread below.
        }
        for (auto& t : pool)
            if (t.joinable()) t.join();
        for (int i = next.fetch_add(1); i < ntask; i = next.fetch_add(1))
            work(i * grain, std::min(nf, i * grain + grain));
    }
```

- [ ] **Step 2: Run the suites**

Run: full ctest. Expected: PASS; the e2e determinism case stays green (grain unchanged).

- [ ] **Step 3: Commit**

```bash
git add src/dsp.cpp
git commit -m "Segmenter: keep the task counter alive until the worker threads are joined"
```

### Task 1.4: The limiter must have the last word

Closes F04. Review Focus 3.

**Files:**
- Modify: `src/dynamics.cpp:293-348`
- Test: `tests/test_main.cpp`

- [ ] **Step 1: Write the failing test**

Add `#include "sanity/dynamics.hpp"` to the include block, then:

```cpp
TT_CASE("normalize_loudness never leaves the true peak above the ceiling on transient heavy material") {
    const int fs = 48000;
    // Quiet noise with loud clicks: reaching -16 LUFS needs about +25 dB on the
    // noise, which the clicks cannot take, so every round limits hard and the
    // loop runs out of rounds.
    auto x = noise(0.01, static_cast<std::size_t>(fs) * 12, 5);
    for (std::size_t i = fs / 2; i + 3 < x.size(); i += fs / 3) {
        x[i] = 0.9f;
        x[i + 1] = -0.9f;
    }
    const LoudnessTargetInfo info = normalize_loudness(x, fs, -16.0, -1.0);
    CHECK(info.applied);
    CHECK_LE(true_peak_db(x, fs), -1.0 + 0.05);
    CHECK_LE(std::fabs(info.error_lu), 1.0);
}
```

- [ ] **Step 2: Run it**

Run: build `sanity_tests`, run it. Expected: the `true_peak_db` check FAILS or `info.peak_rounds == 4` with a gain-up last; note the printed peak.

- [ ] **Step 3: Make the final action a limit, never a gain-up**

Replace stage 2 of `normalize_loudness`:

```cpp
    info.true_peak_before_db = true_peak_db(x, sample_rate);
    bool gained_after_limit = false;
    for (int round = 0; round < 4; round++) {
        const LimiterInfo li = limiter(x, sample_rate, true_peak_ceiling_db, false);
        gained_after_limit = false;
        info.peak_rounds = round + 1;
        info.limiter_reduction_db = li.gain_reduction_db;
        info.limiter_max_reduction_db = li.max_reduction_db;
        const double tp = true_peak_db(x, sample_rate);
        if (tp > true_peak_ceiling_db) {
            const double fix = true_peak_ceiling_db - tp;
            apply_gain_db(x, fix);
            total_gain += fix;
        }
        info.loudness_after_limit = integrated_lufs(x, sample_rate);
        if (info.loudness_after_limit < -1e29) break;
        const double err = target_lufs - info.loudness_after_limit;
        if (std::fabs(err) < 0.05) break;
        apply_gain_db(x, err);
        total_gain += err;
        gained_after_limit = err > 0.0;
    }
    if (gained_after_limit) {
        // The loop ran out of rounds right after a gain-up: limit once more so
        // the ceiling holds, and report the residual loudness error honestly.
        const LimiterInfo li = limiter(x, sample_rate, true_peak_ceiling_db, false);
        info.limiter_reduction_db = li.gain_reduction_db;
        info.limiter_max_reduction_db = std::max(info.limiter_max_reduction_db, li.max_reduction_db);
        const double tp = true_peak_db(x, sample_rate);
        if (tp > true_peak_ceiling_db) {
            apply_gain_db(x, true_peak_ceiling_db - tp);
            total_gain += true_peak_ceiling_db - tp;
        }
        info.peak_rounds++;
    }
```

- [ ] **Step 4: Run the suites**

Run: full ctest. Expected: the new case PASS; e2e lines unchanged (the corpus never reaches the fourth round).

- [ ] **Step 5: Commit**

```bash
git add src/dynamics.cpp tests/test_main.cpp
git commit -m "Normalise: re-limit after a final gain-up so the true peak ceiling always holds"
```

### Task 1.5: Stop allocating whole episodes to list the library

Closes F24, F32. Review Focus 4.

**Files:**
- Modify: `gui/app_state.h:28-47`
- Modify: `gui/app_state.cpp:233-280, 343-386`

- [ ] **Step 1: Carry the duration as a number**

In `Session` add `double seconds_hint = 0.0;  // duration from session.json while the audio is not loaded` and change `duration()`:

```cpp
    double duration() const { return has_audio ? audio.duration() : seconds_hint; }
```

In `library_sessions()` replace the `in.value("seconds")` block with:

```cpp
        const QJsonObject in = o.value("input").toObject();
        s.audio.sample_rate = in.value("sample_rate").toInt(48000);
        s.audio.channels = in.value("channels").toInt(1);
        s.seconds_hint = in.value("seconds").toDouble(0.0);
        s.has_audio = false;  // the samples stay on disk until load_session()
```

- [ ] **Step 2: Rebuild the report when a session is loaded**

In `load_session()`, replace the `QFile rf(...)` block with:

```cpp
    s.params = default_params();
    s.has_report = false;
    if (s.has_mastered) {
        // The engine is the source of truth: the numeric report is re-derived
        // from the mastered audio; report.json on disk is for humans and tools.
        s.report = {};
        s.report.version = SANITY_VERSION;
        s.report.params = s.params;
        s.report.before = sanity::analyze(s.audio);
        s.report.after = sanity::analyze(s.mastered);
        const double target = s.params.target_lufs != 0.0
                                  ? s.params.target_lufs
                                  : (s.mastered.channels > 1 ? -16.0 : -19.0);
        s.report.conformant_loudness = std::fabs(s.report.after.integrated_lufs - target) <= 1.0;
        s.report.conformant_true_peak =
            s.report.after.true_peak_db <= s.params.target_true_peak_db + 0.05;
        s.report.conformant = s.report.conformant_loudness && s.report.conformant_true_peak;
        s.has_report = true;
    }
```

(`analyze(const AudioBuffer&)` comes from Task 1.1; add `#include "sanity/master.hpp"` if missing. `SANITY_VERSION` is already defined for the GUI target.)

- [ ] **Step 3: Verify by hand with a long fake session**

```powershell
New-Item -ItemType Directory -Force $env:USERPROFILE\PodcastSanity\long-7200 | Out-Null
@'
{ "name": "long", "created": "2026-01-01T00:00:00", "input": { "sample_rate": 48000, "channels": 2, "seconds": 7200.0 } }
'@ | Set-Content -Encoding utf8 $env:USERPROFILE\PodcastSanity\long-7200\session.json
.\build-windows\Release\podcast-sanity.exe
```
Open "Libreria / Sessioni": the row shows `120:00`; Task Manager private bytes stay below 300 MB (before the fix they jumped by 1.4 GB). Delete `long-7200` afterwards.

- [ ] **Step 4: Run the suites and commit**

Run: full ctest. Expected: PASS.

```bash
git add gui/app_state.h gui/app_state.cpp
git commit -m "Library: list sessions without allocating their audio; rebuild the report when a session is loaded"
```

### Task 1.6: CLI argument bounds and encoded output

Closes F13, F14.

**Files:**
- Modify: `cli/render_main.cpp`
- Modify: `CMakeLists.txt` (ctest entries after `add_test(NAME unit ...)`)

- [ ] **Step 1: Add the ctest expectations first**

```cmake
# CLI contract: a missing option value is an error, not a crash; --help lists
# the distribution switch added in Phase 3.
add_test(NAME cli_missing_value COMMAND podcast-sanity-render in.wav --out)
set_tests_properties(cli_missing_value PROPERTIES
  PASS_REGULAR_EXPRESSION "option --out needs a value")
add_test(NAME cli_help COMMAND podcast-sanity-render --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "--deliver")
```

- [ ] **Step 2: Run ctest -R cli**

Run: `cmake --build build-windows --config Release --parallel; ctest --test-dir build-windows -C Release -R cli --output-on-failure`
Expected: both FAIL (`cli_missing_value` crashes, `cli_help` has no `--deliver`).

- [ ] **Step 3: Bound the parser**

Replace the lambdas and every raw `argv[++i]` read in `parse()`:

```cpp
bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; i++) {
        const std::string k = argv[i];
        auto value = [&](std::string& v) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "option %s needs a value\n", k.c_str());
                return false;
            }
            v = argv[++i];
            return true;
        };
        auto next = [&](double& v) {
            std::string s;
            if (!value(s)) return false;
            v = std::atof(s.c_str());
            return true;
        };
        auto nexti = [&](int& v) {
            std::string s;
            if (!value(s)) return false;
            v = std::atoi(s.c_str());
            return true;
        };
        if (k == "--help" || k == "-h") {
            usage();
            std::exit(0);
        } else if (k == "--version" || k == "-V") {
            std::printf("podcast-sanity-render %s\n", SANITY_VERSION);
            std::exit(0);
        } else if (k == "--out") {
            if (!value(a.out)) return false;
        } else if (k == "--report") {
            if (!value(a.report)) return false;
        } else if (k == "--truth") {
            if (!value(a.truth)) return false;
        } else if (k == "--clean-out") {
            if (!value(a.clean_out)) return false;
        } else if (k == "--gen-corpus") {
            if (!value(a.gen_corpus)) return false;
        } else if (k == "--profile") {
            if (!value(a.profile)) return false;
        } else if (k == "--deliver") {
            if (!value(a.deliver)) return false;
        } else if (k == "--seconds") {
            if (!next(a.seconds)) return false;
        } else if (k == "--preset") {
            std::string v;
            if (!value(v)) return false;
            a.params.preset = v == "music" ? Preset::Music : Preset::Talk;
        } else if (k == "--filler") {
            std::string v;
            if (!value(v)) return false;
            if (v == "cautious") a.params.filler = FillerMode::Cautious;
            else if (v == "balanced") a.params.filler = FillerMode::Balanced;
            else if (v == "aggressive") a.params.filler = FillerMode::Aggressive;
            else a.params.filler = FillerMode::Off;
        } else if (k == "--lufs") {
            if (!next(a.params.target_lufs)) return false;
        } ...
```

Every numeric option (`--tp`, `--max-lra`, `--max-s`, `--max-m`, `--out-rate`, `--channels`, `--denoise`) follows the `if (!next(...)) return false;` / `nexti` form; flags without a value are unchanged. Add `std::string deliver;` to `Args` and these lines to `usage()` under "Output:" (the behaviour arrives in Task 3.2):

```
"  --deliver PROFILE           Apply a distribution profile (target, channels, format)\n"
"  --list-profiles             List the distribution profiles and exit\n"
"  --no-normalize              Skip loudness normalisation\n"
```

- [ ] **Step 4: Encode by extension**

Replace the `wav_write(a.out, out, a.float_out, &err)` call in `main()`:

```cpp
    ExportOptions eo = export_options_for_path(a.out);
    if (eo.format == ExportFormat::Wav16 && a.float_out) eo.format = ExportFormat::WavFloat32;
    if (!encoder_available(eo.format)) {
        std::fprintf(stderr, "cannot write %s: %s encoder is not available in this build\n",
                     a.out.c_str(), export_format_name(eo).c_str());
        return 1;
    }
    if (!encode_audio(a.out, out, eo, err)) {
        std::fprintf(stderr, "cannot write %s: %s\n", a.out.c_str(), err.c_str());
        return 1;
    }
```

Add `#include "sanity/encode.hpp"`.

- [ ] **Step 5: Run ctest -R cli and the full suite**

Expected: `cli_missing_value` PASS (exit 2 plus the message), `cli_help` PASS, everything else PASS.

- [ ] **Step 6: Commit**

```bash
git add cli/render_main.cpp CMakeLists.txt
git commit -m "CLI: reject options without a value instead of crashing; encode --out by extension"
```

---

## Phase 2: P2 defects and an honest harness

### Task 2.1: Rate-agnostic Filler Killer

Closes F10, F19. Review Focus 2.

**Files:**
- Modify: `include/sanity/master.hpp:787-790`
- Modify: `src/filler.cpp:54-66`
- Modify: `src/pipeline.cpp:306-326`
- Modify: `CMakeLists.txt` (ctest variant)

**Interfaces:**
- Produces: `std::vector<FillerHit> detect_fillers(const std::vector<float>& mono, int sample_rate, FillerMode mode, std::vector<FillerCandidate>* candidates = nullptr, const std::vector<FrameFeat>* feats = nullptr);` The optional `feats` are reused only when `sample_rate == 48000` and `feats->size() == mono.size() / 480`; otherwise they are ignored.

- [ ] **Step 1: Add the failing ctest variant**

```cmake
# The detector is calibrated at 48 kHz; this run proves 44.1 kHz material gets
# the same precision and recall.
add_test(NAME e2e_44k COMMAND sanity_e2e_tests --rate 44100)
set_tests_properties(e2e_44k PROPERTIES TIMEOUT 900)
```

Run: `cmake --build build-windows --config Release --parallel; ctest --test-dir build-windows -C Release -R e2e_44k --output-on-failure`
Expected: FAIL on `filler_precision=0.5` and `removed_seconds`.

- [ ] **Step 2: Detect on a 48 kHz copy and map the hits back**

In `src/filler.cpp`, `detect_fillers` becomes a thin wrapper around the existing body (renamed `detect_fillers_48k`, which takes `const std::vector<FrameFeat>& feats`):

```cpp
namespace {
constexpr int kDetectRate = 48000;

std::vector<FillerHit> detect_fillers_48k(const std::vector<float>& mono,
                                          const std::vector<FrameFeat>& feats, FillerMode mode,
                                          std::vector<FillerCandidate>* candidates) {
    // ... the current body from "std::vector<double> levels;" to "return merged;",
    // with every `sample_rate` replaced by kDetectRate and the Segmenter call removed.
}
}  // namespace

std::vector<FillerHit> detect_fillers(const std::vector<float>& mono, int sample_rate,
                                      FillerMode mode, std::vector<FillerCandidate>* candidates,
                                      const std::vector<FrameFeat>* feats) {
    std::vector<FillerHit> hits;
    if (candidates) candidates->clear();
    if (mode == FillerMode::Off || sample_rate <= 0 ||
        mono.size() < static_cast<std::size_t>(sample_rate) / 2)
        return hits;

    // The thresholds were calibrated at 48 kHz (docs/TESTING.md section 3).
    // Other rates are analysed on a resampled copy so the calibration holds,
    // and the hit positions are mapped back to the caller's rate.
    const bool native = sample_rate == kDetectRate;
    const std::vector<float>& work = native ? mono : resample(mono, sample_rate, kDetectRate);
    std::vector<FrameFeat> local;
    const std::vector<FrameFeat>* use = nullptr;
    if (native && feats && feats->size() == work.size() / static_cast<std::size_t>(segmenter_hop(kDetectRate)))
        use = feats;
    if (!use) {
        Segmenter seg(SegmenterConfig{kDetectRate, 10.0, 35.0});
        seg.features(work, local);
        use = &local;
    }
    if (use->size() < 20) return hits;
    hits = detect_fillers_48k(work, *use, mode, candidates);
    if (!native) {
        const double k = static_cast<double>(sample_rate) / kDetectRate;
        auto map = [&](std::size_t s) {
            return std::min(mono.size(), static_cast<std::size_t>(std::llround(s * k)));
        };
        for (FillerHit& h : hits) { h.start = map(h.start); h.end = map(h.end); }
        if (candidates)
            for (FillerCandidate& c : *candidates) { c.start = map(c.start); c.end = map(c.end); }
    }
    return hits;
}
```

`resample` returns a copy, so bind it to a local `std::vector<float> resampled;` when `!native` rather than a dangling reference: write `std::vector<float> resampled; if (!native) resampled = resample(mono, sample_rate, kDetectRate); const std::vector<float>& work = native ? mono : resampled;`.

Update the declaration in `master.hpp` with the new trailing parameter and its comment. In `src/pipeline.cpp` pass the shared features: `r.fillers = detect_fillers(mono, in_rate, p.filler, &cands, &feats);`.

- [ ] **Step 3: Run the suites**

Run: full ctest. Expected: `e2e` identical numbers (48 kHz path reuses features that are bit-identical to the ones it used to compute); `e2e_44k` prints `rough ... filler P 1.00 R 1.00` and PASSES. If `e2e_44k` still fails on another check (SNR, SI-SDR), stop and report the printed line; Task 2.8 decides thresholds, not this task.

- [ ] **Step 4: Commit**

```bash
git add include/sanity/master.hpp src/filler.cpp src/pipeline.cpp CMakeLists.txt
git commit -m "Filler Killer: detect on a 48 kHz copy at any input rate and reuse the pipeline features"
```

### Task 2.2: Resample before loudness normalisation

Closes F05.

**Files:**
- Modify: `src/pipeline.cpp:372-405`
- Test: `tests/e2e_main.cpp`

- [ ] **Step 1: Write the failing e2e case**

```cpp
TT_CASE("e2e: a 44.1 kHz delivery from 48 kHz material keeps the target and the ceiling") {
    if (!wanted("resample")) return;
    Params p = base_params();
    p.out_sample_rate = 44100;
    CorpusItem item = make_item("home", opts().seconds);
    const AudioBuffer in = buffer_of(item.degraded, item.sample_rate);
    AudioBuffer out;
    const Report rep = master(in, p, out);
    CHECK(out.sample_rate == 44100);
    const LoudnessStats real = measure_loudness(out);
    print_line("resample", real.integrated_lufs, real.true_peak_db);
    CHECK(std::fabs(real.integrated_lufs - (-19.0)) <= 0.1);  // the engine lands within 0.05 LU
    CHECK_LE(real.true_peak_db, -1.0 + 0.02);
    CHECK(rep.conformant);
}
```

Run `sanity_e2e_tests --scenario resample`. Expected: FAIL on the 0.1 LU tolerance (measured drift was 0.19 LU).

- [ ] **Step 2: Move the output stage before normalisation**

In `master()`, move the `// 11. output format` block (`out_rate`, `out_changed`, `run_stage(r, "output", ...)`) to just before `// 10. loudness + true peak`, and pass `out_rate` to `normalize_loudness` instead of `in_rate`. Keep `out = mono_to_channels(out_rate, mono, out_channels);` after the normalise stage. Renumber the comments (`10. output format`, `11. loudness + true peak`).

- [ ] **Step 3: Run the suites**

Run: full ctest. Expected: new case PASS with `lufs -19.00`; other e2e lines unchanged (they do not resample).

- [ ] **Step 4: Commit**

```bash
git add src/pipeline.cpp tests/e2e_main.cpp
git commit -m "Pipeline: resample before normalisation so the limiter sees the delivered rate"
```

### Task 2.3: Conformance honours the optional ceilings

Closes F07.

**Files:**
- Modify: `include/sanity/master.hpp:763-766`
- Modify: `src/pipeline.cpp:195-200, 407-417`
- Modify: `gui/pages/standards_page.cpp:244-246`
- Modify: `docs/STANDARDS.md` section 2 and 4
- Test: `tests/test_main.cpp`

- [ ] **Step 1: Write the failing unit test**

Add `#include "sanity/master.hpp"` to the include block, then:

```cpp
TT_CASE("conformance reports the optional LRA and short term ceilings") {
    const int fs = 48000;
    // Six 3 s blocks alternating -26 dB and -6 dB: LRA well above 5 LU.
    std::vector<float> x;
    for (int k = 0; k < 6; k++) {
        auto seg = sine(1000.0, (k % 2 == 0) ? 0.05 : 0.5, 3.0, fs);
        x.insert(x.end(), seg.begin(), seg.end());
    }
    Params p;
    p.filler = FillerMode::Off;
    p.adaptive_leveler = false;
    p.compress = false;
    p.max_lra = 5.0;
    AudioBuffer out;
    const Report r = master(from_mono(fs, x), p, out);
    CHECK(!r.conformant_lra);
    CHECK(!r.conformant);
    CHECK(r.conformant_loudness);
    bool mentioned = false;
    for (const std::string& w : r.warnings)
        if (w.find("loudness range") != std::string::npos) mentioned = true;
    CHECK(mentioned);
    Params q = p;
    q.max_lra = 0.0;
    const Report r2 = master(from_mono(fs, x), q, out);
    CHECK(r2.conformant_lra);
}
```

Run: build and run `sanity_tests`. Expected: compile error, `conformant_lra` missing.

- [ ] **Step 2: Add the three flags**

`Report` gains:

```cpp
    bool conformant_lra = true;         // max_lra == 0 or after.lra <= max_lra
    bool conformant_short_term = true;  // max_short_term == 0 or after.max_short_term <= limit + 0.1
    bool conformant_momentary = true;   // max_momentary == 0 or after.max_momentary <= limit + 0.1
```

In `master()` after the true-peak check:

```cpp
    if (p.max_lra > 0.0 && r.after.lra > p.max_lra) {
        r.conformant_lra = false;
        r.warnings.push_back("loudness range " + std::to_string(r.after.lra) + " LU exceeds the maximum " +
                             std::to_string(p.max_lra) + " LU (reported, not corrected)");
    }
    if (p.max_short_term < 0.0 && r.after.max_short_term > p.max_short_term + 0.1) {
        r.conformant_short_term = false;
        r.warnings.push_back("short term loudness above the configured maximum");
    }
    if (p.max_momentary < 0.0 && r.after.max_momentary > p.max_momentary + 0.1) {
        r.conformant_momentary = false;
        r.warnings.push_back("momentary loudness above the configured maximum");
    }
    r.conformant = r.conformant_loudness && r.conformant_true_peak && r.conformant_lra &&
                   r.conformant_short_term && r.conformant_momentary;
```

Note the sign: the ceilings are LUFS values, so "set" means `< 0.0` for short-term and momentary, `> 0.0` for LRA. Serialise them in `to_json()` under `conformance` as `lra_ok`, `short_term_ok`, `momentary_ok`. In `standards_page.cpp` replace the local `lra_ok` computation with `r.conformant_lra`.

- [ ] **Step 3: Document what is enforced and what is only reported**

In `docs/STANDARDS.md` section 2 replace the paragraph starting "Optional ceilings" with:

> Optional ceilings: `max_short_term` and `max_momentary` are **enforced** by a slow gain reduction inside the normalisation stage and then **reported**; `max_lra` is **reported only** (a loudness range reduction would be a second leveler pass and is not part of the chain). All three are 0 = unconstrained; when set, exceeding them makes `conformance.ok` false with the reason in `warnings`.

In section 4 add the three new keys to the JSON example.

- [ ] **Step 4: Run the suites and commit**

Run: full ctest. Expected: PASS.

```bash
git add include/sanity/master.hpp src/pipeline.cpp gui/pages/standards_page.cpp docs/STANDARDS.md tests/test_main.cpp
git commit -m "Conformance: include the LRA, short term and momentary ceilings in the verdict and the report"
```

### Task 2.4: Readable JSON

Closes F12, and the missing JSON test of F42.

**Files:**
- Modify: `include/sanity/json.hpp`
- Test: `tests/test_main.cpp`

- [ ] **Step 1: Write the failing test**

Add `#include "sanity/json.hpp"` to the include block, then:

```cpp
TT_CASE("json writer puts scalars on the key line, escapes strings and trims numbers") {
    JsonWriter j(2);
    j.begin_object();
    j.member("a", 1);
    j.member("b", std::string("x\"y\n"));
    j.member("c", 2.50);
    j.member("d", true);
    j.key("e");
    j.begin_array();
    j.value_double_inline(1.0);
    j.value_double_inline(2.25);
    j.end_array();
    j.key("f");
    j.begin_object();
    j.end_object();
    j.end_object();
    const std::string want =
        "\n{\n  \"a\": 1,\n  \"b\": \"x\\\"y\\n\",\n  \"c\": 2.5,\n  \"d\": true,\n"
        "  \"e\": [1.0,2.25],\n  \"f\": {}\n}";
    CHECK(j.str() == want);
    if (j.str() != want) std::printf("got:\n%s\n", j.str().c_str());
    JsonWriter n(2);
    n.begin_array();
    n.value_double_inline(1.0 / 0.0);
    n.end_array();
    CHECK(n.str() == "\n[null]");
}
```

Run it. Expected: FAIL (values currently land on their own line).

- [ ] **Step 2: Separate "comma and newline" from "after a key"**

In `json.hpp` replace `comma()` and the callers:

```cpp
    // Emits the separator before a new item. After a key nothing is emitted:
    // the value belongs on the same line as its key.
    void separator(bool newline_ok) {
        if (after_key_) {
            after_key_ = false;
            return;
        }
        if (!stack_.empty() && !stack_.back()) out_ += ",";
        if (!stack_.empty()) stack_.back() = false;
        if (newline_ok) newline();
    }
```

- `key()`: `separator(true); out_ += quote(k); out_ += ": "; after_key_ = true;`
- `value_string/value_double/value_int/value_bool/value_null/raw_value`: `separator(true); out_ += ...;`
- `value_double_inline`: `separator(false); out_ += number(v);`
- `open(char c)`: `separator(true); out_ += c; stack_.push_back(true); after_key_ = false;`
- `close()` unchanged. Delete `comma()`.

- [ ] **Step 3: Run the suites**

Run: full ctest. Expected: PASS. Open `build-windows\...\report.json` from a CLI run and confirm `"integrated_lufs": -19.0` sits on one line.

- [ ] **Step 4: Commit**

```bash
git add include/sanity/json.hpp tests/test_main.cpp
git commit -m "JSON: keep each scalar on its key line"
```

### Task 2.5: Encoder correctness

Closes F08, F09, F21.

**Files:**
- Modify: `include/sanity/encode.hpp`
- Modify: `src/encode.cpp`
- Modify: `include/sanity/audio.hpp`, `src/io_wav.cpp` (24-bit writer)
- Modify: `gui/pages/export_page.cpp:34-44` (Opus entry)
- Test: `tests/test_main.cpp`

**Interfaces:**
- Produces: `ExportFormat::Opus`; `bool wav_write_pcm24(const std::string& path, const AudioBuffer& in, std::string* err);`

- [ ] **Step 1: Write the failing tests**

```cpp
TT_CASE("mp3 export at 96 kHz keeps the channel count and resamples each channel") {
    if (!encoder_available(ExportFormat::Mp3)) return;
    namespace fs = std::filesystem;
    const auto path = fs::temp_directory_path() / "sanity-96k.mp3";
    AudioBuffer a = AudioBuffer::zeros(96000, 2, 96000 * 2);
    for (std::size_t i = 0; i < a.frames(); i++)
        a.data[2 * i] = static_cast<float>(0.4 * std::sin(kTwoPi * 440 * i / 96000.0));  // right stays silent
    ExportOptions o;
    o.format = ExportFormat::Mp3;
    std::string err;
    CHECK(encode_audio(path.string(), a, o, err));
#if SANITY_HAVE_SNDFILE
    SF_INFO info{};
    auto* f = sf_open(path.string().c_str(), SFM_READ, &info);
    CHECK(f != nullptr);
    if (f) {
        CHECK(info.channels == 2);
        CHECK(info.samplerate == 48000);
        std::vector<float> d(static_cast<std::size_t>(info.frames) * 2);
        const auto n = sf_readf_float(f, d.data(), info.frames);
        sf_close(f);
        double l = 0, r = 0;
        for (sf_count_t i = 0; i < n; i++) { l += d[2 * i] * d[2 * i]; r += d[2 * i + 1] * d[2 * i + 1]; }
        CHECK(l > 100);
        CHECK(r < l * 0.001);
    }
#endif
    std::error_code ec;
    fs::remove(path, ec);
}

TT_CASE("wav 24 bit is written as 24 bit even without libsndfile") {
    namespace fs = std::filesystem;
    const auto path = fs::temp_directory_path() / "sanity-24.wav";
    AudioBuffer a = AudioBuffer::zeros(48000, 1, 1000);
    for (std::size_t i = 0; i < a.frames(); i++) a.data[i] = static_cast<float>(std::sin(i * 0.05) * 0.7);
    std::string err;
    CHECK(wav_write_pcm24(path.string(), a, &err));
    AudioBuffer b;
    CHECK(wav_read(path.string(), b, err));
    CHECK(b.frames() == a.frames());
    double worst = 0.0;
    for (std::size_t i = 0; i < b.data.size(); i++)
        worst = std::max(worst, std::fabs(static_cast<double>(b.data[i] - a.data[i])));
    CHECK_LE(worst, 1.0 / 8388607.0 + 1e-7);
    std::error_code ec;
    fs::remove(path, ec);
}
```

Run: compile fails (`wav_write_pcm24` missing); the MP3 case fails on `info.channels == 2` / `samplerate` after it compiles.

- [ ] **Step 2: Resample per channel in the MP3 path**

Replace the `if (!lame_rate_ok(src.sample_rate)) { ... }` block in `encode_mp3`:

```cpp
    if (!lame_rate_ok(src.sample_rate)) {
        const int target = nearest_lame_rate(src.sample_rate);
        const std::size_t frames = src.frames();
        std::vector<std::vector<float>> chans(static_cast<std::size_t>(src.channels));
        for (int c = 0; c < src.channels; c++) {
            std::vector<float> one(frames);
            for (std::size_t i = 0; i < frames; i++)
                one[i] = src.data[i * static_cast<std::size_t>(src.channels) + c];
            chans[static_cast<std::size_t>(c)] = resample(one, src.sample_rate, target);
        }
        AudioBuffer rsb;
        rsb.sample_rate = target;
        rsb.channels = src.channels;
        rsb.resize_frames(chans[0].size());
        for (int c = 0; c < rsb.channels; c++)
            for (std::size_t i = 0; i < chans[0].size(); i++)
                rsb.data[i * static_cast<std::size_t>(rsb.channels) + c] = chans[static_cast<std::size_t>(c)][i];
        src = rsb;
    }
```

Keep the following "fold anything wider than 2 down" block.

- [ ] **Step 3: FLAC level, Opus, 24-bit WAV**

- `encode_sndfile`: `double level = std::clamp(opt.flac_compression, 0, 8) / 8.0;`
- `ExportFormat` gains `Opus`; `encode_sndfile` maps it to `SF_FORMAT_OGG | SF_FORMAT_OPUS` (guard with `#ifdef SF_FORMAT_OPUS`, else fall through to "unsupported format"); `encoder_available(Opus)` returns the result of `sf_format_check` on that format when `SANITY_HAVE_SNDFILE`, false otherwise; `export_extension(Opus) = ".opus"`, `export_format_name(Opus) = "Opus"`, `export_options_for_path(".opus") -> Opus`.
- `audio.hpp`/`io_wav.cpp`: add `bool wav_write_pcm24(const std::string& path, const AudioBuffer& in, std::string* err);` writing format tag 1, 24 bits per sample, three little-endian bytes per sample from `lround(clamp(v) * 8388607.0)`. Share the header writer with `wav_write` through a static helper `write_wav(path, in, bits, tag, body, err)`.
- `encode_audio`: `Wav24` without libsndfile calls `wav_write_pcm24` instead of the float fallback.
- `gui/pages/export_page.cpp`: add `{"Opus (Ogg)", sanity::ExportFormat::Opus, 0}` after the Vorbis entry; `encoder_available` already hides it when absent.

- [ ] **Step 4: Run the suites and commit**

Run: full ctest. Expected: PASS; the existing export test now also round-trips `.opus` if the vcpkg libsndfile has Opus (it does: `opus.dll` is in the payload).

```bash
git add include/sanity/encode.hpp src/encode.cpp include/sanity/audio.hpp src/io_wav.cpp gui/pages/export_page.cpp tests/test_main.cpp
git commit -m "Export: per-channel MP3 resampling, FLAC level scale, Opus format, native 24 bit WAV"
```

### Task 2.6: Recording recovery uses the real format

Closes F28. Review Focus 5.

**Files:**
- Modify: `gui/pages/record_page.cpp:213-233, 296-374, 391-429`

- [ ] **Step 1: Write the metadata after the device answered**

In `start_recording()` move the `live_meta_path()` JSON write after `capture_->start(...)` succeeded and use `capture_->sample_rate()` and `capture_->channels()` (the device may have refused the requested format). Store them in two new members `int live_rate_ = 48000; int live_channels_ = 1;` and set `sample_rate_of_live_ = live_rate_`.

- [ ] **Step 2: Decode with the stored format in both paths**

Add a file-scope helper in the anonymous namespace:

```cpp
// Reads the crash-safe take with the format recorded next to it. Falls back
// to 48 kHz mono for takes written before the metadata existed.
bool read_live_take(sanity::AudioBuffer& b) {
    int rate = 48000, ch = 1;
    QFile meta(live_meta_path());
    if (meta.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(meta.readAll()).object();
        rate = o.value("sample_rate").toInt(48000);
        ch = std::clamp(o.value("channels").toInt(1), 1, 2);
    }
    QFile f(live_pcm_path());
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray pcm = f.readAll();
    const int frames = pcm.size() / (2 * ch);
    if (frames <= 0) return false;
    b.sample_rate = rate;
    b.channels = ch;
    b.data.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(ch));
    const qint16* p = reinterpret_cast<const qint16*>(pcm.constData());
    for (int i = 0; i < frames * ch; i++)
        b.data[static_cast<std::size_t>(i)] = static_cast<float>(p[i] / 32768.0);
    return true;
}
```

Use it in the "Recupera" button lambda and in `stop_recording()` (which then no longer reads `rate_`/`channels_`). Add `#include <QJsonDocument>` and `#include <QJsonObject>`. In `on_chunk()` use `live_channels_` instead of `channels_->currentData()`.

- [ ] **Step 3: Verify by hand**

Set the Record page to 44100 Hz, Stereo, source "Corpus sintetico", REC for 5 s, STOP. The status bar must read `44100 Hz, 2 canale/i, 00:05`. Press REC again, then kill the process from Task Manager, restart, press "Recupera": the recovered take must also read 44100 Hz, 2 channels, and "Ascolta originale" on the Master page must play at normal pitch.

- [ ] **Step 4: Run the suites and commit**

```bash
git add gui/pages/record_page.cpp gui/pages/pages.h
git commit -m "Recorder: decode the crash-safe take with the format the device actually delivered"
```

### Task 2.7: AppState and page wiring fixes

Closes F25, F26, F27, F29, F30, F31, F37.

**Files:**
- Modify: `gui/app_state.h`, `gui/app_state.cpp`
- Modify: `gui/mainwindow.cpp:84-108`
- Modify: `gui/pages/library_page.cpp:165-169`
- Modify: `gui/pages/export_page.cpp:180-269`
- Modify: `gui/pages/diagnostics_page.cpp:269-315`, `gui/pages/pages.h:304-336`

- [ ] **Step 1: Cancellation and exceptions in the worker**

`AppState` gets `int job_id_ = 0;` and `int cancelled_job_ = -1;`. `Worker::run_job(sanity::AudioBuffer in, sanity::Params p, int job)`:

```cpp
    void run_job(sanity::AudioBuffer in, sanity::Params p, int job) {
        try {
            sanity::AudioBuffer out;
            const sanity::Report rep = sanity::master(in, p, out);
            emit finished(true, rep, out, QString(), job);
        } catch (const std::exception& e) {
            emit finished(false, sanity::Report{}, sanity::AudioBuffer{},
                          QStringLiteral("Errore interno durante l'elaborazione: %1")
                              .arg(QString::fromUtf8(e.what())), job);
        } catch (...) {
            emit finished(false, sanity::Report{}, sanity::AudioBuffer{},
                          QStringLiteral("Errore interno durante l'elaborazione"), job);
        }
    }
```

Signal: `void finished(bool ok, sanity::Report rep, sanity::AudioBuffer out, QString error, int job);`. The receiving lambda ignores results whose `job == cancelled_job_`:

```cpp
        if (job == cancelled_job_) {
            emit status_message(QStringLiteral("Elaborazione annullata"));
            return;
        }
        busy_ = false;
        ...
```

`start_mastering` increments `job_id_` and passes `Q_ARG(int, job_id_)`; `cancel_mastering()` sets `cancelled_job_ = job_id_; busy_ = false; emit mastering_finished(false, QStringLiteral("Annullato"));`.

- [ ] **Step 2: Theme changes apply everywhere**

In `MainWindow` constructor: `connect(&AppState::get(), &AppState::theme_changed, this, &MainWindow::apply_theme);`. In `SettingsPage::apply_theme()` keep emitting `theme_changed` (it is a signal; call it as `emit AppState::get().theme_changed();`).

- [ ] **Step 3: Reveal folder on every platform**

`library_page.cpp`: replace the `QProcess::startDetached("xdg-open", ...)` line with `QDesktopServices::openUrl(QUrl::fromLocalFile(dir));` and include `<QDesktopServices>` and `<QUrl>`; drop `<QProcess>`.

- [ ] **Step 4: Export re-masters when the delivery format changed**

In `ExportPage::do_export()` replace `if (!s.has_report) {` with:

```cpp
    const int want_ch = channels_->currentData().toInt();
    const int want_rate = rate_->currentData().toInt();
    const bool stale = s.has_report && (s.report.params.out_channels != want_ch ||
                                        s.report.params.out_sample_rate != want_rate);
    if (!s.has_report || stale) {
```

and, inside, keep setting `p.out_channels` / `p.out_sample_rate` as today. In `refresh()` show the hint `"Il master in memoria e a %1 canali / %2 Hz: esportando verra rielaborato."` when `stale` is true (compute it the same way; factor a `bool ExportPage::master_is_stale() const` member declared in `pages.h`). Also read the saved default format on construction: `format_->setCurrentIndex(QSettings().value("export/format", 0).toInt())` mapped through the same index order as the Settings page list (`0 -> MP3 96`, `1 -> MP3 192`, `2 -> WAV 24`, `3 -> FLAC`): select the combo entry whose format and bitrate match.

- [ ] **Step 5: The diagnostics harness runs on a QThread owned by the page**

Replace the detached `std::thread` with `QThread::create` owned by the page:

```cpp
    auto* worker = QThread::create([this, seconds] { ... same body ... });
    worker->setParent(this);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    harness_thread_ = worker;
    worker->start();
```

Add `QThread* harness_thread_ = nullptr;` to `DiagnosticsPage` and a destructor that does `if (harness_thread_ && harness_thread_->isRunning()) harness_thread_->wait();`. Include `<QThread>` (already present).

- [ ] **Step 6: Run the suites, then verify cancel by hand**

Run: full ctest. Expected: PASS (selftest exercises start/finish). By hand: load a 10-minute file, press "Elabora", then Master menu > nothing cancels today, so add a "Annulla" ghost button next to the progress bar on the Master page calling `AppState::get().cancel_mastering()`; after pressing it the result card must read "Annullato" and the tiles must not update when the worker finishes.

- [ ] **Step 7: Commit**

```bash
git add gui/app_state.h gui/app_state.cpp gui/mainwindow.cpp gui/pages/library_page.cpp gui/pages/export_page.cpp gui/pages/diagnostics_page.cpp gui/pages/master_page.cpp gui/pages/pages.h
git commit -m "GUI: real cancellation, worker exception guard, live theme switch, portable reveal, export re-master, owned harness thread"
```

### Task 2.8: Tests that run where the docs say they run

Closes F11, F15, F42 (counts).

**Files:**
- Modify: `tests/test_main.cpp:399-427`
- Modify: `tests/e2e_main.cpp:314, 359`
- Modify: `CMakeLists.txt`
- Modify: `docs/TESTING.md`

- [ ] **Step 1: Temp paths**

In the two WAV round-trip cases replace `"/tmp/sanity_rt.wav"` and `"/tmp/sanity_rt16.wav"` with `(std::filesystem::temp_directory_path() / "sanity_rt.wav").string()` (and `_rt16`), and remove the files at the end of each case with `std::filesystem::remove(path, ec)`.

- [ ] **Step 2: Run the harness at the documented lengths and record the numbers**

```powershell
foreach ($s in 12,20,30,45) { "== $s s"; .\build-windows\Release\sanity_e2e_tests.exe --seconds $s | Select-String 'FAIL|PASS|lufs' }
.\build-windows\Release\sanity_e2e_tests.exe --rate 44100 | Select-String 'FAIL|PASS|lufs'
```

Paste the output into the commit body. Before this plan the failures were `speech_preservation_db 5.86` at 30 s and rough SNR gain `+1.76` at 45 s.

- [ ] **Step 3: Set thresholds with a documented margin**

In `tests/e2e_main.cpp`:
- line 314: `CHECK_GE(s.speech_preservation_db, 5.0);` with the comment `// measured 5.9 to 7.4 dB at 12/20/30/45 s; 5.0 keeps a 0.8 dB margin`
- line 359: `CHECK_GE(s.snr_after_db, s.snr_before_db + 1.5);` with the comment `// measured +1.8 to +4.0 dB; +1.5 keeps a 0.3 dB margin while still failing a denoiser that does nothing`

If Step 2 shows a value below these new thresholds, stop and report; do not lower further.

- [ ] **Step 4: Make ctest run the documented lengths**

```cmake
add_test(NAME e2e_30s COMMAND sanity_e2e_tests --seconds 30)
add_test(NAME e2e_45s COMMAND sanity_e2e_tests --seconds 45)
set_tests_properties(e2e_30s e2e_45s PROPERTIES TIMEOUT 900)
```

- [ ] **Step 5: Fix the documentation**

In `docs/TESTING.md`:
- table row `unit`: `20 cases / 162 checks without libebur128 (23 / 171 with it)`; after Phase 2 the numbers change again, so the executor writes the number ctest prints at the end of this task.
- add rows `e2e_30s`, `e2e_45s`, `e2e_44k` with their purpose.
- section 2.2: speech preservation `>= 5 dB`, SNR improvement `>= +1.5 dB`, with the measured table from Step 2.
- section 1: remove the "JSON writer's escaping" bullet or point it at the test added in Task 2.4 (`json writer puts scalars on the key line`).
- section 2.2 "True peak ... measured on the written output": change to "measured on the output buffer before quantisation; the 16-bit rounding adds less than 0.0001 dB".
Apply the same count corrections to `README.md` ("Verified, not asserted") and `docs/DESIGN-podcast-sanity.md` section 6; in section 5 delete the sentence about `wix_patch.xml`.

- [ ] **Step 6: Run everything and commit**

Run: `ctest --test-dir build-windows -C Release --output-on-failure` (now 8 tests: unit, e2e, e2e_30s, e2e_45s, e2e_44k, gui, cli_missing_value, cli_help). Expected: all PASS.

```bash
git add tests/test_main.cpp tests/e2e_main.cpp CMakeLists.txt docs/TESTING.md README.md docs/DESIGN-podcast-sanity.md
git commit -m "Tests: run the harness at 30 s, 45 s and 44.1 kHz in ctest; thresholds with documented margins; temp paths"
```

### Task 2.9: Engine hygiene

Closes F16, F17, F18, F20, F22 (remaining items).

**Files:**
- Modify: `src/dsp.cpp:300-315, 649-661`, `include/sanity/dsp.hpp:293`
- Modify: `src/clean.cpp:211-221, 283-298, 493-524`
- Modify: `src/metrics.cpp:209-238`
- Test: `tests/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
TT_CASE("resampler accepts an empty input") {
    const std::vector<float> empty;
    CHECK(resample(empty, 44100, 48000).empty());
    CHECK(oversample_4x(empty).empty());
    CHECK_NEAR(oversample_4x_peak(empty), 0.0, 1e-12);
}

TT_CASE("hum detection works from a short quiet region") {
    const int fs = 48000;
    // 3 s of noise with 50 Hz hum at -30 dB; the segmenter features mark only
    // the first 300 ms as non-speech, which is the only region dehum may use.
    auto x = noise(0.02, static_cast<std::size_t>(fs) * 3, 9);
    for (std::size_t i = 0; i < x.size(); i++)
        x[i] += static_cast<float>(0.0316 * std::sin(kTwoPi * 50.0 * i / fs));
    std::vector<FrameFeat> feats(x.size() / static_cast<std::size_t>(segmenter_hop(fs)));
    for (std::size_t i = 30; i < feats.size(); i++) feats[i].speech = true;
    const HumInfo h = dehum(x, fs, feats, -60.0, false);
    CHECK_NEAR(h.freq, 50.0, 1e-9);
    CHECK_GE(h.excess_db, 6.0);
}

TT_CASE("adaptive high pass applies the floor cutoff when no voice is measured") {
    const int fs = 48000;
    auto x = sine(30.0, 0.5, 1.0, fs);  // rumble only
    std::vector<FrameFeat> feats(x.size() / static_cast<std::size_t>(segmenter_hop(fs)));
    const HighpassInfo h = adaptive_highpass(x, fs, feats, 55.0, true);
    CHECK(h.applied);
    CHECK_NEAR(h.cutoff_used, 55.0, 1e-9);
    CHECK_LE(peak_abs(std::vector<float>(x.begin() + fs / 2, x.end())), 0.25);
}

TT_CASE("score_intervals rejects a detection that runs far past the truth") {
    const int fs = 48000;
    std::vector<Interval> truth = {{48000, 72000}};         // 1.0 s .. 1.5 s
    std::vector<Interval> good = {{47000, 73000}};          // within 50 ms
    std::vector<Interval> greedy = {{47000, 120000}};       // eats a whole second after
    CHECK(score_intervals(good, truth, fs, 50.0).true_positive == 1);
    CHECK(score_intervals(greedy, truth, fs, 50.0).true_positive == 0);
    CHECK(score_intervals(greedy, truth, fs, 50.0).false_positive == 1);
}
```

Add `#include "sanity/clean.hpp"` and `#include "sanity/metrics.hpp"`. Run: the resampler case crashes or asserts, the hum case fails on `excess_db`, the high pass case fails on `applied`, the interval case fails on the greedy check.

- [ ] **Step 2: Fix each**

- `Resampler::process` and `Resampler::peak`: `if (in.empty()) return {};` / `return 0.0;` at the top.
- `dehum`: compute the probe offsets from the region length: `const double res = static_cast<double>(sample_rate) / static_cast<double>(std::max<std::size_t>(1, b - a));` and probe at `{std::max(3.0, 2.0 * res), std::max(5.0, 3.0 * res), std::max(8.0, 4.0 * res), std::max(12.0, 6.0 * res)}` so every probe is at least two bins away from the tone.
- `adaptive_highpass`: remove the `cut <= floor_hz + 0.01` early return; apply the biquad whenever `apply` is true (the floor is the point of the stage). Keep `info.cutoff_used = cut`.
- `score_intervals`: a match requires `det.start + tol >= truth.start && det.end <= truth.end + tol && det.start <= truth.end && det.end >= truth.start` (both edges within tolerance, plus overlap). Replace the whole inner condition with that.
- Dead code: delete `(void)atk` and the `atk` variable, `after_sum`, `(void)peak_db` with `peak_db`, `(void)seg_hop` with `seg_hop`, and `Stft::norm_`.

- [ ] **Step 3: Run the suites**

Run: full ctest. Expected: PASS. The e2e SI-SDR lines may move by less than 0.1 dB because the high pass now always applies at the floor; the harness measures SI-SDR with the high pass off, so the checks are unaffected. If a `lufs` line changes, that is unexpected: stop and report.

- [ ] **Step 4: Commit**

```bash
git add src/dsp.cpp include/sanity/dsp.hpp src/clean.cpp src/metrics.cpp tests/test_main.cpp
git commit -m "Engine hygiene: empty resampler input, hum probes scaled to the region, high pass floor always applied, strict interval scoring, dead code"
```

### Task 2.10: Read FLAC, Ogg and MP3 where the dialogs offer them

Closes F36 (and feeds Task 4.x, which needs to open music files).

**Files:**
- Create: `src/io_sndfile.cpp`
- Modify: `include/sanity/audio.hpp`
- Modify: `CMakeLists.txt` (add the source)
- Modify: `gui/mainwindow.cpp:189-208`, `gui/pages/standards_page.cpp:256-270`, `cli/render_main.cpp` (use `audio_read`)
- Test: `tests/test_main.cpp`

**Interfaces:**
- Produces: `bool audio_read(const std::string& path, AudioBuffer& out, std::string& err);` reads WAV through `wav_read`, everything else through libsndfile when built with it; `std::string audio_read_filter();` returns the Qt file dialog filter matching what this build can read.

- [ ] **Step 1: Write the failing test**

```cpp
TT_CASE("audio_read opens every format the build can write") {
    namespace fs = std::filesystem;
    AudioBuffer a = AudioBuffer::zeros(48000, 1, 48000);
    for (std::size_t i = 0; i < a.frames(); i++) a.data[i] = static_cast<float>(0.3 * std::sin(kTwoPi * 220 * i / 48000.0));
    for (auto f : {ExportFormat::Wav16, ExportFormat::Flac, ExportFormat::Ogg, ExportFormat::Mp3}) {
        if (!encoder_available(f)) continue;
        const auto path = fs::temp_directory_path() / ("sanity-read" + export_extension(f));
        ExportOptions o;
        o.format = f;
        std::string err;
        CHECK(encode_audio(path.string(), a, o, err));
        AudioBuffer b;
        CHECK(audio_read(path.string(), b, err));
        CHECK(b.sample_rate == 48000);
        CHECK(b.channels == 1);
        CHECK(b.frames() >= 47000);  // lossy decoders may trim priming samples
        std::error_code ec;
        fs::remove(path, ec);
    }
}
```

- [ ] **Step 2: Implement**

`src/io_sndfile.cpp`:

```cpp
// podcast-sanity - compressed audio input through libsndfile
#include "sanity/audio.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#if SANITY_HAVE_SNDFILE
#include <sndfile.h>
#endif

namespace sanity {

namespace {
std::string lower_ext(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}
}  // namespace

bool audio_read(const std::string& path, AudioBuffer& out, std::string& err) {
    if (lower_ext(path) == ".wav") return wav_read(path, out, err);
#if SANITY_HAVE_SNDFILE
    SF_INFO info{};
#ifdef _WIN32
    SNDFILE* sf = sf_wchar_open(std::filesystem::u8path(path).c_str(), SFM_READ, &info);
#else
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &info);
#endif
    if (!sf) {
        err = sf_strerror(nullptr);
        return false;
    }
    if (info.channels <= 0 || info.samplerate <= 0 || info.frames < 0) {
        sf_close(sf);
        err = "unsupported stream";
        return false;
    }
    out.sample_rate = info.samplerate;
    out.channels = info.channels;
    out.resize_frames(static_cast<std::size_t>(info.frames));
    const sf_count_t got = sf_readf_float(sf, out.data.data(), info.frames);
    sf_close(sf);
    if (got < 0) {
        err = "decode failed";
        return false;
    }
    out.resize_frames(static_cast<std::size_t>(got));  // resize_frames zero-fills: copy first
    return true;
}
#else
    err = "this build reads WAV only";
    return false;
}
#endif

std::string audio_read_filter() {
#if SANITY_HAVE_SNDFILE
    return "Audio (*.wav *.flac *.ogg *.opus *.mp3 *.aiff);;Tutti i file (*)";
#else
    return "Audio (*.wav);;Tutti i file (*)";
#endif
}

}  // namespace sanity
```

Careful with the last `resize_frames`: it zero-fills. Use `out.data.resize(static_cast<std::size_t>(got) * static_cast<std::size_t>(out.channels));` instead. Declare both functions in `audio.hpp`. Add `src/io_sndfile.cpp` to `SANITY_CORE_SOURCES`. Replace `wav_read` with `audio_read` and the dialog filters with `QString::fromStdString(sanity::audio_read_filter())` in `MainWindow::open_audio`, `StandardsPage::measure_file`, `main.cpp` positional argument, and the CLI input.

- [ ] **Step 3: Run the suites and commit**

Run: full ctest. Expected: PASS.

```bash
git add src/io_sndfile.cpp include/sanity/audio.hpp CMakeLists.txt gui/mainwindow.cpp gui/main.cpp gui/pages/standards_page.cpp cli/render_main.cpp tests/test_main.cpp
git commit -m "Input: read FLAC, Ogg, Opus and MP3 through libsndfile wherever a file can be opened"
```

---

## Phase 3: Distribution profiles "(recommended)"

The owner's words: the core is recording plus post-processing, and the product must hand back a file that is ready for distribution under the industry standards, with the recommended choice marked "(recommended)" wherever a choice is offered. This phase names those choices once, in the engine, and makes the CLI, the Export page, the Standards page and the Settings page read from that single table.

### Task 3.1: The profile table in the engine

**Files:**
- Create: `include/sanity/distribution.hpp`
- Create: `src/distribution.cpp`
- Modify: `CMakeLists.txt` (add the source)
- Test: `tests/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct DistributionProfile {
      const char* id;            // stable, used by the CLI and QSettings: "podcast-voice"
      const char* name;          // English, for the CLI table
      bool recommended;          // exactly one profile is the recommended one
      double target_lufs;        // 0 = auto (-19 mono, -16 stereo)
      double true_peak_db;
      int channels;              // 0 = keep the input channel count
      int sample_rate;           // 0 = keep the input rate
      ExportFormat format;
      int mp3_bitrate_kbps;      // only for Mp3
      const char* note;          // one sentence: why this is right
  };
  const std::vector<DistributionProfile>& distribution_profiles();
  const DistributionProfile* find_distribution_profile(const std::string& id);
  const DistributionProfile& recommended_distribution_profile();
  // Writes the profile into the mastering parameters and the export options.
  void apply_distribution_profile(const DistributionProfile& d, Params& p, ExportOptions& o);
  // "Podcast voice: MP3 96 kbps mono, -19 LUFS, -1 dBTP (recommended)"
  std::string distribution_label(const DistributionProfile& d);
  ```

- [ ] **Step 1: Write the failing test**

Add `#include "sanity/distribution.hpp"`, then:

```cpp
TT_CASE("distribution profiles: unique ids, one recommended, the recommended one is the hosting envelope") {
    const auto& all = distribution_profiles();
    CHECK_GE(static_cast<int>(all.size()), 4);
    int recommended = 0;
    for (std::size_t i = 0; i < all.size(); i++) {
        if (all[i].recommended) recommended++;
        for (std::size_t j = i + 1; j < all.size(); j++)
            CHECK(std::string(all[i].id) != all[j].id);
    }
    CHECK(recommended == 1);
    const DistributionProfile& r = recommended_distribution_profile();
    CHECK(std::string(r.id) == "podcast-voice");
    CHECK(r.format == ExportFormat::Mp3);
    CHECK(r.mp3_bitrate_kbps == recommended_mp3_bitrate(1));
    CHECK(r.channels == 1);
    CHECK_NEAR(r.target_lufs, -19.0, 1e-9);
    CHECK_NEAR(r.true_peak_db, -1.0, 1e-9);
    Params p;
    ExportOptions o;
    apply_distribution_profile(r, p, o);
    CHECK(p.out_channels == 1);
    CHECK_NEAR(p.target_lufs, -19.0, 1e-9);
    CHECK(o.format == ExportFormat::Mp3);
    CHECK(o.mp3_bitrate_kbps == 96);
    CHECK(distribution_label(r).find("(recommended)") != std::string::npos);
    CHECK(find_distribution_profile("no-such-profile") == nullptr);
    CHECK(find_distribution_profile("broadcast-r128") != nullptr);
    CHECK_NEAR(find_distribution_profile("broadcast-r128")->target_lufs, -23.0, 1e-9);
}
```

- [ ] **Step 2: Implement**

`src/distribution.cpp`:

```cpp
// podcast-sanity - named delivery profiles
#include "sanity/distribution.hpp"

#include <cstdio>

namespace sanity {

const std::vector<DistributionProfile>& distribution_profiles() {
    static const std::vector<DistributionProfile> table = {
        {"podcast-voice", "Podcast voice", true, -19.0, -1.0, 1, 0, ExportFormat::Mp3, 96,
         "Apple Podcasts authoring practice for spoken word; hosts do not re-encode mono MP3 at or below 96 kbps."},
        {"podcast-stereo", "Podcast with music, stereo", false, -16.0, -1.0, 2, 0, ExportFormat::Mp3, 192,
         "Stereo delivery at -16 LUFS; hosts do not re-encode stereo MP3 at or below 192 kbps."},
        {"archive-wav24", "Archive master, WAV 24 bit", false, 0.0, -1.0, 0, 0, ExportFormat::Wav24, 0,
         "Lossless master at the automatic target (-19 mono / -16 stereo) to keep and re-export later."},
        {"lossless-flac", "Lossless delivery, FLAC 24 bit", false, 0.0, -1.0, 0, 0, ExportFormat::Flac, 0,
         "For hosts that accept FLAC; same loudness as the archive master, about half the size."},
        {"broadcast-r128", "Broadcast, EBU R128", false, -23.0, -1.0, 0, 48000, ExportFormat::Wav24, 0,
         "EBU R128: -23 LUFS, -1 dBTP, 48 kHz, for radio hand-over rather than podcast hosting."},
    };
    return table;
}

const DistributionProfile* find_distribution_profile(const std::string& id) {
    for (const auto& d : distribution_profiles())
        if (id == d.id) return &d;
    return nullptr;
}

const DistributionProfile& recommended_distribution_profile() {
    for (const auto& d : distribution_profiles())
        if (d.recommended) return d;
    return distribution_profiles().front();
}

void apply_distribution_profile(const DistributionProfile& d, Params& p, ExportOptions& o) {
    p.target_lufs = d.target_lufs;
    p.target_true_peak_db = d.true_peak_db;
    p.out_channels = d.channels;
    p.out_sample_rate = d.sample_rate;
    p.normalize = true;
    o.format = d.format;
    if (d.format == ExportFormat::Mp3) o.mp3_bitrate_kbps = d.mp3_bitrate_kbps;
}

std::string distribution_label(const DistributionProfile& d) {
    ExportOptions o;
    o.format = d.format;
    o.mp3_bitrate_kbps = d.mp3_bitrate_kbps;
    std::string s = std::string(d.name) + ": " + export_format_name(o);
    s += d.channels == 1 ? " mono" : (d.channels == 2 ? " stereo" : "");
    char buf[64];
    if (d.target_lufs != 0.0) {
        std::snprintf(buf, sizeof(buf), ", %.0f LUFS", d.target_lufs);
        s += buf;
    } else {
        s += ", auto LUFS";
    }
    std::snprintf(buf, sizeof(buf), ", %.0f dBTP", d.true_peak_db);
    s += buf;
    if (d.recommended) s += " (recommended)";
    return s;
}

}  // namespace sanity
```

The header includes `<string>`, `<vector>`, `"sanity/encode.hpp"`, `"sanity/master.hpp"`.

- [ ] **Step 3: Run, commit**

Run: full ctest. Expected: PASS.

```bash
git add include/sanity/distribution.hpp src/distribution.cpp CMakeLists.txt tests/test_main.cpp
git commit -m "Distribution: one table of delivery profiles with a single recommended entry"
```

### Task 3.2: The CLI delivers

**Files:**
- Modify: `cli/render_main.cpp`
- Modify: `CMakeLists.txt` (ctest)
- Modify: `tests/e2e_main.cpp`

- [ ] **Step 1: Add the ctest and e2e expectations**

```cmake
add_test(NAME cli_list_profiles COMMAND podcast-sanity-render --list-profiles)
set_tests_properties(cli_list_profiles PROPERTIES
  PASS_REGULAR_EXPRESSION "podcast-voice .*\\(recommended\\)")
```

```cpp
TT_CASE("e2e: the recommended delivery decodes within tolerance") {
    if (!wanted("deliver")) return;
    const DistributionProfile& d = recommended_distribution_profile();
    if (!encoder_available(d.format)) { std::printf("    deliver  skipped: no encoder\n"); return; }
    Params p = base_params();
    ExportOptions o;
    apply_distribution_profile(d, p, o);
    CorpusItem item = make_item("home", opts().seconds);
    AudioBuffer out;
    const Report rep = master(buffer_of(item.degraded, item.sample_rate), p, out);
    CHECK(rep.conformant);
    const auto path = std::filesystem::temp_directory_path() / "sanity-deliver.mp3";
    std::string err;
    CHECK(encode_audio(path.string(), out, o, err));
    AudioBuffer back;
    CHECK(audio_read(path.string(), back, err));
    const LoudnessStats m = measure_loudness(back);
    print_line("deliver", m.integrated_lufs, m.true_peak_db);
    CHECK(std::fabs(m.integrated_lufs - (-19.0)) <= 1.0);
    // MP3 decoding overshoots: the ceiling is checked on the master, the
    // decoded file only has to stay below full scale (documented in STANDARDS.md).
    CHECK_LE(m.true_peak_db, 0.0);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
```

Add `#include <filesystem>`, `#include "sanity/distribution.hpp"`, `#include "sanity/encode.hpp"` to `e2e_main.cpp`.

- [ ] **Step 2: Implement `--deliver` and `--list-profiles`**

In `cli/render_main.cpp`:
- `Args` gets `bool list_profiles = false;`; `parse()` handles `--list-profiles`.
- In `main()` before reading the input:
  ```cpp
    if (a.list_profiles) {
        for (const DistributionProfile& d : distribution_profiles())
            std::printf("%-16s %s\n    %s\n", d.id, distribution_label(d).c_str(), d.note);
        return 0;
    }
  ```
- After parsing and before `master()`:
  ```cpp
    ExportOptions eo = export_options_for_path(a.out);
    if (!a.deliver.empty()) {
        const DistributionProfile* d = find_distribution_profile(a.deliver);
        if (!d) {
            std::fprintf(stderr, "unknown profile %s (see --list-profiles)\n", a.deliver.c_str());
            return 2;
        }
        apply_distribution_profile(*d, a.params, eo);
        const std::string want = export_extension(eo.format);
        if (a.out.size() < want.size() ||
            a.out.compare(a.out.size() - want.size(), want.size(), want) != 0) {
            std::fprintf(stderr, "profile %s writes %s files: name the output accordingly\n",
                         d->id, want.c_str());
            return 2;
        }
    }
  ```
  and use this `eo` in the encoding block from Task 1.6 (delete the duplicate `export_options_for_path` call there).
- `usage()`: the two lines added in Task 1.6 already describe the switches; add the example `  podcast-sanity-render IN.wav --out episode.mp3 --deliver podcast-voice` under "Usage:".

- [ ] **Step 3: Run and commit**

Run: full ctest (now with `cli_list_profiles`). Expected: PASS; the e2e `deliver` line prints a loudness within 1 LU of -19.

```bash
git add cli/render_main.cpp CMakeLists.txt tests/e2e_main.cpp
git commit -m "CLI: --deliver applies a distribution profile and encodes the delivery file"
```

### Task 3.3: The GUI offers the profiles with the recommended one first

**Files:**
- Modify: `gui/pages/export_page.cpp`, `gui/pages/pages.h:236-264`
- Modify: `gui/pages/standards_page.cpp:34-44`
- Modify: `gui/pages/settings_page.cpp:68-80, 154-160`
- Modify: `gui/main.cpp:102-123` (self test exports the recommended profile)

- [ ] **Step 1: Export page: a profile selector drives the controls**

In `ExportPage` add `QComboBox* profile_ = nullptr; QLabel* profile_note_ = nullptr; bool applying_profile_ = false;`. At the top of `format_card_`, before the format row:

```cpp
        profile_ = new QComboBox();
        for (const sanity::DistributionProfile& d : sanity::distribution_profiles()) {
            QString label = QString::fromUtf8(d.name);
            if (d.recommended) label += QStringLiteral(" (recommended)");
            profile_->addItem(label, QString::fromUtf8(d.id));
        }
        profile_->addItem(QStringLiteral("Personalizzato"), QString());
        connect(profile_, &QComboBox::currentIndexChanged, this, [this] { apply_profile(); });
        format_card_->add_widget(make_row(QStringLiteral("Profilo di distribuzione"), profile_,
                                          QStringLiteral("Il profilo imposta formato, canali, "
                                                         "frequenza, loudness e true peak.")));
        profile_note_ = new QLabel();
        profile_note_->setObjectName("Hint");
        profile_note_->setWordWrap(true);
        format_card_->add_widget(profile_note_);
```

Add the method (declare in `pages.h`):

```cpp
void ExportPage::apply_profile() {
    const QString id = profile_->currentData().toString();
    const sanity::DistributionProfile* d = sanity::find_distribution_profile(id.toStdString());
    if (!d) {  // "Personalizzato": leave the controls as they are
        profile_note_->setText(QStringLiteral("Impostazioni manuali."));
        refresh();
        return;
    }
    applying_profile_ = true;
    sanity::Params p = AppState::get().session().params;
    sanity::ExportOptions o;
    sanity::apply_distribution_profile(*d, p, o);
    AppState::get().set_params(p);
    for (int i = 0; i < format_->count(); i++) {
        const QVariantList v = format_->itemData(i).toList();
        if (v.size() == 2 && v.at(1).toInt() == static_cast<int>(o.format) &&
            (o.format != sanity::ExportFormat::Mp3 || v.at(0).toInt() == o.mp3_bitrate_kbps)) {
            format_->setCurrentIndex(i);
            break;
        }
    }
    const int ch = d->channels > 0 ? d->channels : AppState::get().session().audio.channels;
    channels_->setCurrentIndex(ch >= 2 ? 1 : 0);
    if (d->sample_rate > 0) rate_->setCurrentIndex(d->sample_rate == 44100 ? 1 : 0);
    profile_note_->setText(QString::fromUtf8(d->note));
    QSettings().setValue("export/profile", id);
    applying_profile_ = false;
    refresh();
}
```

When the user touches `format_`, `channels_` or `rate_` directly and `!applying_profile_`, switch `profile_` to the "Personalizzato" entry (guard with `blockSignals`). On construction select the profile stored under `export/profile`, defaulting to the recommended id. The rate combo keeps "48000 Hz" first and "44100 Hz" second as today. Rename the format labels so the two envelope entries read `MP3 96 kbps mono (recommended)` and `MP3 192 kbps stereo`.

- [ ] **Step 2: Standards page: mark the recommended target**

In `kStandards` rename the first entry to `"Apple Podcasts - voce mono (recommended)"` and extend its note with ` E il profilo di distribuzione consigliato.`.

- [ ] **Step 3: Settings page: default profile instead of a dangling format list**

Replace `default_format_` (combo of four strings) with a combo of the profiles (same labels as the Export page, no "Personalizzato"), stored under `export/profile` as the id. Remove the `export/format` key. Keep the widget name `default_format_` to limit churn.

- [ ] **Step 4: Self test uses the recommended profile**

In `gui/main.cpp` `SelfTest::verify`, replace the hard-coded `ExportOptions` with:

```cpp
        sanity::Params unused = gui::AppState::get().session().params;
        sanity::ExportOptions o;
        sanity::apply_distribution_profile(sanity::recommended_distribution_profile(), unused, o);
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath(QStringLiteral("podcast-sanity-selftest") +
                                           QString::fromStdString(sanity::export_extension(o.format)));
```

and print `sanity::distribution_label(sanity::recommended_distribution_profile())` in the "wrote" line. `test-windows-msi.ps1` still matches `MP3` in the output.

- [ ] **Step 5: Run, look, commit**

Run: full ctest; then `podcast-sanity.exe --selftest --selftest-shots .\build-windows\shots` and open `mastered-4.png`: the Export page shows "Podcast voice (recommended)" selected and the note under it.

```bash
git add gui/pages/export_page.cpp gui/pages/pages.h gui/pages/standards_page.cpp gui/pages/settings_page.cpp gui/main.cpp
git commit -m "GUI: distribution profiles on the export page, recommended target marked, default profile in settings"
```

### Task 3.4: Document the delivery contract

**Files:**
- Modify: `docs/STANDARDS.md` section 3
- Modify: `README.md` ("Use" and "Command line")

- [ ] **Step 1: Write it**

In `docs/STANDARDS.md` section 3 add a table of the profiles (id, format, channels, loudness, true peak, when to use) generated from `--list-profiles` output, mark `podcast-voice` as recommended, and add the paragraph:

> MP3 decoding overshoots the encoded peaks by up to about 1 dB. The true peak ceiling is enforced on the master before encoding; the decoded MP3 is checked by the harness to stay below 0 dBTP. Hosts that re-encode do their own limiting; the mono 96 kbps envelope avoids that re-encode.

In `README.md` add `podcast-sanity-render episode.wav --out episode.mp3 --deliver podcast-voice` to the command line block and the sentence "The export page offers the same profiles; the recommended one is selected by default."

- [ ] **Step 2: Commit**

```bash
git add docs/STANDARDS.md README.md
git commit -m "Docs: delivery profiles and the MP3 overshoot note"
```

---

## Phase 4: Background music bed with preview

A music bed is a track mixed under the voice: audible in the pauses, pushed down while the speaker talks (ducking), faded in at the start and out at the end, optionally running on for a few seconds after the last word. It is rendered inside the engine so that the loudness target covers the mix and the quality report records it; the GUI adds a card to pick the music, set the levels and hear a 15-second preview before committing to a full master.

### Task 4.1: `music_bed` in the engine

**Files:**
- Create: `include/sanity/music_bed.hpp`
- Create: `src/music_bed.cpp`
- Modify: `include/sanity/master.hpp` (Params, master overload, params_hash)
- Modify: `src/analyze.cpp:156-174` (hash), `src/pipeline.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_main.cpp`, `tests/e2e_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct MusicBedOptions {
      bool enabled = false;
      double bed_level_db = -14.0;   // music level relative to the active speech level, in pauses
      double duck_db = -10.0;        // extra attenuation while speech is present
      double attack_ms = 80.0;       // how fast the bed goes down when speech starts
      double release_ms = 700.0;     // how fast it comes back after speech
      double hold_ms = 300.0;        // speech gaps shorter than this stay ducked
      double fade_in_s = 1.5;
      double fade_out_s = 3.0;
      double offset_s = 0.0;         // bed starts this many seconds into the episode
      double tail_s = 0.0;           // bed continues this long after the speech ends (extends the output)
      bool loop = true;              // repeat the music if it is shorter than the episode
  };
  struct MusicBedInfo {
      bool applied = false;
      double speech_level_db = -1e30;   // active speech level the bed was referenced to
      double bed_level_db = -1e30;      // absolute level of the bed in pauses
      double duck_ratio = 0.0;          // fraction of frames at the ducked level
      std::size_t frames_added = 0;     // tail
      std::size_t loops = 0;            // how many times the music was repeated
  };
  // Builds the bed alone, aligned to `speech` (mono at sample_rate, which the music
  // is resampled and mono-summed to). Length: speech.size() + tail frames.
  std::vector<float> render_music_bed(const std::vector<float>& speech, int sample_rate,
                                      const std::vector<FrameFeat>& feats, const AudioBuffer& music,
                                      const MusicBedOptions& o, MusicBedInfo* info);
  // Adds the bed to `speech` in place (extending it by the tail).
  MusicBedInfo mix_music_bed(std::vector<float>& speech, int sample_rate,
                             const std::vector<FrameFeat>& feats, const AudioBuffer& music,
                             const MusicBedOptions& o);
  ```
- `Params` gains `MusicBedOptions music_bed;`.
- `Report master(const AudioBuffer& in, const Params& p, AudioBuffer& out);` keeps working (no music); new `Report master(const AudioBuffer& in, const Params& p, const AudioBuffer* music, AudioBuffer& out);`.
- `std::string params_hash(const Params& p, const AudioBuffer* music = nullptr);` appends `|music:<frames>:<channels>:<rate>` when music is given.

- [ ] **Step 1: Write the failing unit test**

Add `#include "sanity/music_bed.hpp"`, then:

```cpp
namespace {
// A three note loop, 2 s, so looping is exercised on a 12 s episode.
AudioBuffer test_music(int fs) {
    AudioBuffer m = AudioBuffer::zeros(fs, 2, static_cast<std::size_t>(fs) * 2);
    for (std::size_t i = 0; i < m.frames(); i++) {
        const double t = static_cast<double>(i) / fs;
        const double v = 0.2 * (std::sin(kTwoPi * 220 * t) + 0.6 * std::sin(kTwoPi * 330 * t) +
                                0.4 * std::sin(kTwoPi * 440 * t));
        m.data[2 * i] = static_cast<float>(v);
        m.data[2 * i + 1] = static_cast<float>(0.8 * v);
    }
    return m;
}
double rms_db_over(const std::vector<float>& x, std::size_t a, std::size_t b) {
    double s = 0.0;
    for (std::size_t i = a; i < b && i < x.size(); i++) s += static_cast<double>(x[i]) * x[i];
    return b > a ? 10.0 * std::log10(s / static_cast<double>(b - a) + 1e-30) : -200.0;
}
}  // namespace

TT_CASE("music bed sits at the requested level in pauses and ducks under speech") {
    const int fs = 48000;
    const CorpusItem item = make_corpus_talk(fs, 12.0, degradation_studio(), 7, "bed", false);
    const std::vector<float>& speech = item.clean;
    Segmenter seg(SegmenterConfig{fs, 10.0, 35.0});
    std::vector<FrameFeat> feats;
    seg.features(speech, feats);
    MusicBedOptions o;
    o.enabled = true;
    o.bed_level_db = -14.0;
    o.duck_db = -10.0;
    o.fade_in_s = 0.5;
    o.fade_out_s = 0.5;
    o.tail_s = 2.0;
    MusicBedInfo info;
    const std::vector<float> bed = render_music_bed(speech, fs, feats, test_music(fs), o, &info);
    CHECK(info.applied);
    CHECK(bed.size() == speech.size() + static_cast<std::size_t>(2.0 * fs));
    CHECK(info.frames_added == static_cast<std::size_t>(2.0 * fs));
    CHECK_GE(static_cast<int>(info.loops), 5);
    // Level in a pause vs. under speech: find a 300 ms run of each from feats.
    const std::size_t hop = static_cast<std::size_t>(segmenter_hop(fs));
    auto find_run = [&](bool want_speech) -> std::size_t {
        std::size_t run = 0;
        for (std::size_t f = 100; f < feats.size(); f++) {  // skip the fade in
            run = feats[f].speech == want_speech ? run + 1 : 0;
            if (run == 60) return (f - 59) * hop;  // 600 ms run; return its start
        }
        return 0;
    };
    const std::size_t pause_at = find_run(false), speech_at = find_run(true);
    CHECK(pause_at > 0 && speech_at > 0);
    // Measure the middle 300 ms of each run, which is past the attack/release.
    const double in_pause = rms_db_over(bed, pause_at + 15 * hop, pause_at + 45 * hop);
    const double in_speech = rms_db_over(bed, speech_at + 15 * hop, speech_at + 45 * hop);
    CHECK_NEAR(in_pause, info.bed_level_db, 2.0);
    CHECK_LE(in_speech, in_pause - 10.0 + 2.0);
    CHECK_NEAR(info.bed_level_db, info.speech_level_db - 14.0, 0.5);
    // Fades: the first and the last sample region are quiet.
    CHECK_LE(rms_db_over(bed, 0, fs / 50), in_pause - 20.0);
    CHECK_LE(rms_db_over(bed, bed.size() - fs / 50, bed.size()), in_pause - 20.0);
    // The mix adds the bed and extends the speech by the tail.
    std::vector<float> mixed = speech;
    const MusicBedInfo mi = mix_music_bed(mixed, fs, feats, test_music(fs), o);
    CHECK(mi.applied);
    CHECK(mixed.size() == bed.size());
    CHECK_NEAR(mixed[pause_at + 20 * hop], speech[pause_at + 20 * hop] + bed[pause_at + 20 * hop], 1e-6);
}

TT_CASE("music bed is a no-op when disabled or when the music is empty") {
    const int fs = 48000;
    std::vector<float> speech(fs, 0.1f);
    std::vector<FrameFeat> feats(100);
    MusicBedOptions o;
    AudioBuffer none;
    CHECK(!mix_music_bed(speech, fs, feats, test_music(fs), o).applied);  // disabled
    o.enabled = true;
    CHECK(!mix_music_bed(speech, fs, feats, none, o).applied);           // nothing to mix
    CHECK(speech.size() == static_cast<std::size_t>(fs));
}
```

- [ ] **Step 2: Implement `src/music_bed.cpp`**

```cpp
// podcast-sanity - background music bed with speech-driven ducking
#include "sanity/music_bed.hpp"

#include <algorithm>
#include <cmath>

#include "sanity/dsp.hpp"
#include "sanity/metrics.hpp"

namespace sanity {

namespace {

double rms_db(const std::vector<float>& x) {
    if (x.empty()) return -1e30;
    double s = 0.0;
    for (float v : x) s += static_cast<double>(v) * v;
    return 10.0 * std::log10(s / static_cast<double>(x.size()) + 1e-30);
}

// Repeats `src` to `len` samples with a short equal-power crossfade at every
// seam so the loop point does not click. `loops` counts the repeats used.
std::vector<float> loop_to(const std::vector<float>& src, std::size_t len, int sample_rate,
                           std::size_t& loops) {
    std::vector<float> out;
    out.reserve(len);
    loops = 0;
    if (src.empty()) return out;
    const std::size_t xf = std::min<std::size_t>(src.size() / 4, static_cast<std::size_t>(0.05 * sample_rate));
    while (out.size() < len) {
        const std::size_t avail = std::min(xf, out.size());
        const std::size_t base = out.size() - avail;
        for (std::size_t k = 0; k < avail; k++) {
            const double t = (static_cast<double>(k) + 0.5) / static_cast<double>(avail);
            out[base + k] = static_cast<float>(out[base + k] * std::cos(t * kHalfPi) +
                                               src[k] * std::sin(t * kHalfPi));
        }
        out.insert(out.end(), src.begin() + static_cast<long>(avail), src.end());
        loops++;
    }
    out.resize(len);
    return out;
}

}  // namespace

std::vector<float> render_music_bed(const std::vector<float>& speech, int sample_rate,
                                    const std::vector<FrameFeat>& feats, const AudioBuffer& music,
                                    const MusicBedOptions& o, MusicBedInfo* info) {
    MusicBedInfo local;
    MusicBedInfo& inf = info ? *info : local;
    inf = MusicBedInfo{};
    if (!o.enabled || music.data.empty() || music.sample_rate <= 0 || speech.empty() || sample_rate <= 0)
        return {};

    // 1. Music to the speech rate, mono.
    std::vector<float> m = to_mono(music);
    if (music.sample_rate != sample_rate) m = resample(m, music.sample_rate, sample_rate);
    if (m.empty()) return {};

    // 2. Reference levels: the bed is set relative to how loud the speaker is.
    inf.speech_level_db = active_speech_level_db(speech, sample_rate);
    if (inf.speech_level_db < -100.0) inf.speech_level_db = rms_db(speech);
    const double music_db = rms_db(m);
    inf.bed_level_db = inf.speech_level_db + o.bed_level_db;
    const double gain = db_to_gain(inf.bed_level_db - music_db);

    // 3. Timeline: offset, loop or trim, tail.
    const std::size_t tail = static_cast<std::size_t>(std::max(0.0, o.tail_s) * sample_rate);
    const std::size_t offset = std::min(speech.size(), static_cast<std::size_t>(std::max(0.0, o.offset_s) * sample_rate));
    const std::size_t total = speech.size() + tail;
    const std::size_t need = total - offset;
    std::vector<float> body;
    if (o.loop) {
        body = loop_to(m, need, sample_rate, inf.loops);
    } else {
        body.assign(m.begin(), m.begin() + static_cast<long>(std::min(need, m.size())));
        body.resize(need, 0.0f);
        inf.loops = 1;
    }
    std::vector<float> bed(total, 0.0f);
    for (std::size_t i = 0; i < need; i++) bed[offset + i] = static_cast<float>(body[i] * gain);

    // 4. Fades (raised cosine) at the start of the bed and at the very end.
    const std::size_t fi = std::min(need, static_cast<std::size_t>(std::max(0.0, o.fade_in_s) * sample_rate));
    for (std::size_t i = 0; i < fi; i++)
        bed[offset + i] *= static_cast<float>(0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / static_cast<double>(fi)));
    const std::size_t fo = std::min(total, static_cast<std::size_t>(std::max(0.0, o.fade_out_s) * sample_rate));
    for (std::size_t i = 0; i < fo; i++)
        bed[total - 1 - i] *= static_cast<float>(0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / static_cast<double>(fo)));

    // 5. Ducking: per 10 ms frame, target gain is duck_db while speech is on
    //    (plus a hold), 0 dB otherwise; asymmetric smoothing; linear per sample.
    const int hop = segmenter_hop(sample_rate);
    const std::size_t nf = total / static_cast<std::size_t>(hop) + 1;
    std::vector<double> target(nf, 0.0);
    const int hold_frames = static_cast<int>(std::max(0.0, o.hold_ms) / 10.0);
    int hold = 0;
    std::size_t ducked = 0;
    for (std::size_t f = 0; f < nf; f++) {
        const bool talking = f < feats.size() && feats[f].speech;
        if (talking) hold = hold_frames;
        else if (hold > 0) hold--;
        target[f] = (talking || hold > 0) ? o.duck_db : 0.0;
        if (target[f] < 0.0) ducked++;
    }
    inf.duck_ratio = nf ? static_cast<double>(ducked) / static_cast<double>(nf) : 0.0;
    const double atk = 1.0 - std::exp(-10.0 / std::max(1.0, o.attack_ms));
    const double rel = 1.0 - std::exp(-10.0 / std::max(1.0, o.release_ms));
    std::vector<double> env(nf, 0.0);
    double cur = 0.0;
    for (std::size_t f = 0; f < nf; f++) {
        cur += (target[f] < cur ? atk : rel) * (target[f] - cur);
        env[f] = cur;
    }
    for (std::size_t i = 0; i < total; i++) {
        const std::size_t f = i / static_cast<std::size_t>(hop);
        const std::size_t g = std::min(nf - 1, f + 1);
        const double t = static_cast<double>(i % static_cast<std::size_t>(hop)) / hop;
        bed[i] = static_cast<float>(bed[i] * db_to_gain(env[f] * (1.0 - t) + env[g] * t));
    }

    inf.frames_added = tail;
    inf.applied = true;
    return bed;
}

MusicBedInfo mix_music_bed(std::vector<float>& speech, int sample_rate,
                           const std::vector<FrameFeat>& feats, const AudioBuffer& music,
                           const MusicBedOptions& o) {
    MusicBedInfo info;
    const std::vector<float> bed = render_music_bed(speech, sample_rate, feats, music, o, &info);
    if (!info.applied) return info;
    speech.resize(bed.size(), 0.0f);
    for (std::size_t i = 0; i < bed.size(); i++) speech[i] += bed[i];
    return info;
}

}  // namespace sanity
```

Add `src/music_bed.cpp` to `SANITY_CORE_SOURCES`. The header includes `<cstddef>`, `<vector>`, `"sanity/audio.hpp"`, `"sanity/dsp.hpp"`.

- [ ] **Step 3: Wire it into `Params`, `params_hash` and the pipeline**

- `master.hpp`: `#include "sanity/music_bed.hpp"`; in `Params` after the Filler Killer block: `// --- music bed ---  MusicBedOptions music_bed;`; add the `master` overload and the `params_hash` default argument.
- `analyze.cpp` `params_hash`: append to the format string `|%d|%.2f|%.2f|%.1f|%.1f|%.1f|%.2f|%.2f|%.2f|%.2f|%d` with `music_bed.enabled, bed_level_db, duck_db, attack_ms, release_ms, hold_ms, fade_in_s, fade_out_s, offset_s, tail_s, loop`, and when `music` is non-null append `|music:%zu:%d:%d` with frames, channels, sample_rate before hashing. Bump the prefix `v1|` to `v2|`.
- `pipeline.cpp`: the old `master(in, p, out)` forwards to `master(in, p, nullptr, out)`. In the new one, after the compressor stage and before the output stage:

```cpp
    // ---------------------------------------------------- music bed
    const bool do_bed = p.music_bed.enabled && music && !music->data.empty();
    run_stage(r, "music_bed", do_bed, audio_ms, [&] {
        const MusicBedInfo b = mix_music_bed(mono, in_rate, feats, *music, p.music_bed);
        add_metric(r, "music_bed", "speech_level_db", b.speech_level_db);
        add_metric(r, "music_bed", "bed_level_db", b.bed_level_db);
        add_metric(r, "music_bed", "duck_ratio", b.duck_ratio);
        add_metric(r, "music_bed", "loops", static_cast<double>(b.loops));
        add_metric(r, "music_bed", "tail_s", static_cast<double>(b.frames_added) / in_rate);
    });
```

`r.params_hash = params_hash(p, music);`. `write_params()` serialises the `music_bed` fields under a nested `"music_bed"` object. The `feats` used for ducking are the pipeline's (post-filler recompute), which is what the test does too.

- [ ] **Step 4: e2e scenario**

```cpp
TT_CASE("e2e: a music bed keeps the delivery conformant and the filler cut intact") {
    if (!wanted("bed")) return;
    Params p = base_params();
    p.music_bed.enabled = true;
    p.music_bed.tail_s = 2.0;
    CorpusItem item = make_item("home", opts().seconds);
    const AudioBuffer in = buffer_of(item.degraded, item.sample_rate);
    AudioBuffer music = AudioBuffer::zeros(44100, 2, 44100 * 3);   // different rate on purpose
    for (std::size_t i = 0; i < music.frames(); i++) {
        const float v = static_cast<float>(0.2 * std::sin(kTwoPi * 261.6 * i / 44100.0));
        music.data[2 * i] = v;
        music.data[2 * i + 1] = v;
    }
    AudioBuffer out, out_plain;
    const Report rep = master(in, p, &music, out);
    Params plain = p;
    plain.music_bed.enabled = false;
    const Report rep_plain = master(in, plain, &music, out_plain);
    print_line("bed", rep.after.integrated_lufs, rep.after.true_peak_db);
    CHECK(rep.conformant);
    CHECK(out.frames() == out_plain.frames() + static_cast<std::size_t>(2.0 * item.sample_rate));
    CHECK(rep.fillers_removed == rep_plain.fillers_removed);
    bool staged = false;
    for (const StageTiming& s : rep.stages) if (s.name == "music_bed" && s.enabled) staged = true;
    CHECK(staged);
    CHECK(rep.params_hash != rep_plain.params_hash);
    AudioBuffer out2;
    const Report rep2 = master(in, p, &music, out2);
    CHECK(out2.data == out.data);
}
```

- [ ] **Step 5: Run and commit**

Run: full ctest. Expected: PASS; existing e2e `params_hash` values change (prefix `v2`), which is fine because the determinism check compares two runs of the same build.

```bash
git add include/sanity/music_bed.hpp src/music_bed.cpp include/sanity/master.hpp src/analyze.cpp src/pipeline.cpp CMakeLists.txt tests/test_main.cpp tests/e2e_main.cpp
git commit -m "Engine: music bed stage with speech-driven ducking, fades, loop and tail"
```

### Task 4.2: CLI switches for the bed

**Files:**
- Modify: `cli/render_main.cpp`

- [ ] **Step 1: Options**

`Args` gets `std::string music;`. `parse()` handles `--music FILE` (sets `a.music` and `a.params.music_bed.enabled = true`), `--music-level DB`, `--music-duck DB`, `--music-tail S`, `--music-offset S`, `--music-fade-in S`, `--music-fade-out S`, `--music-no-loop`. `usage()` documents them under a new "Music bed:" heading. In `main()`, when `a.music` is set, read it with `audio_read` (error exit 1 on failure) and call `master(in, a.params, &music, out)`.

- [ ] **Step 2: ctest**

```cmake
add_test(NAME cli_music_bed
  COMMAND ${CMAKE_COMMAND} -E env SANITY_TMP=${CMAKE_CURRENT_BINARY_DIR}
          powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_music_bed.ps1
          $<TARGET_FILE:podcast-sanity-render>)
```

with `tests/cli_music_bed.ps1`:

```powershell
param([string]$Render)
$ErrorActionPreference = 'Stop'
$t = Join-Path $env:SANITY_TMP 'cli-music'
New-Item -ItemType Directory -Force $t | Out-Null
& $Render --gen-corpus "$t\voice.wav" --profile studio --seconds 10 --quiet
& $Render --gen-corpus "$t\bed.wav" --profile studio --seconds 4 --quiet   # any audio works as a bed
& $Render "$t\voice.wav" --out "$t\mix.wav" --music "$t\bed.wav" --music-tail 1 --report "$t\mix.json" --quiet
if ($LASTEXITCODE -ne 0) { throw "render failed: $LASTEXITCODE" }
$j = Get-Content "$t\mix.json" -Raw | ConvertFrom-Json
if (-not ($j.stages | Where-Object { $_.name -eq 'music_bed' -and $_.enabled })) { throw 'music_bed stage missing' }
if ($j.output.frames -ne $j.input.frames + 48000) { throw "tail not added: $($j.output.frames) vs $($j.input.frames)" }
'OK music bed'
```

Guard the test with `if(WIN32)` in CMake (PowerShell script); the Linux job is reviewed separately. `set_tests_properties(cli_music_bed PROPERTIES PASS_REGULAR_EXPRESSION "OK music bed")`.

- [ ] **Step 3: Run, commit**

```bash
git add cli/render_main.cpp CMakeLists.txt tests/cli_music_bed.ps1
git commit -m "CLI: --music and the bed options"
```

### Task 4.3: The music bed card with preview

**Files:**
- Modify: `gui/app_state.h`, `gui/app_state.cpp` (music in the session, worker signature)
- Modify: `gui/pages/pages.h:119-176`, `gui/pages/master_page.cpp`

**Interfaces:**
- `Session` gains `QString music_path; std::shared_ptr<const sanity::AudioBuffer> music; bool has_music = false;` persisted in `session.json` as `"music": {"path": ..., "enabled": ..., "bed_level_db": ..., "duck_db": ..., "fade_in_s": ..., "fade_out_s": ..., "offset_s": ..., "tail_s": ..., "loop": ...}`.
- `AppState::set_music(const QString& path, QString* error)` loads through `audio_read`; `AppState::clear_music()`.
- `Worker::run_job(sanity::AudioBuffer in, sanity::Params p, std::shared_ptr<const sanity::AudioBuffer> music, int job)` calls `master(in, p, music.get(), out)`. Register the metatype: `Q_DECLARE_METATYPE(std::shared_ptr<const sanity::AudioBuffer>)` and `qRegisterMetaType<std::shared_ptr<const sanity::AudioBuffer>>("std::shared_ptr<const sanity::AudioBuffer>")`.

- [ ] **Step 1: State**

Implement the three `Session` fields, `set_music`/`clear_music` (emit a new signal `music_changed()`), the JSON persistence in `save_session`/`load_session`/`library_sessions` (path only in the listing), and the worker signature. `load_session` re-reads the music file from `music_path` if it still exists; otherwise `has_music = false` and a status message "Musica di sottofondo non trovata: <path>".

- [ ] **Step 2: The card**

On the Master page, after the Filler Killer card, add `music_card_` ("Sottofondo musicale", "Musica sotto la voce: abbassata mentre parli, in primo piano nelle pause, con dissolvenze."). Controls (members in `MasterPage`):

- `QPushButton* music_pick_` "Scegli musica..." → `QFileDialog::getOpenFileName` with `sanity::audio_read_filter()` → `AppState::get().set_music(path, &err)`; `QLabel* music_info_` (objectName `Mono`) shows `describe_audio(*session.music)` or the error.
- `QCheckBox* music_enabled_` "Attivo".
- `QSlider* music_level_` range -30..-6 (dB, default -14) with `QLabel* music_level_label_`; hint "Livello della musica nelle pause, rispetto alla voce."
- `QSlider* music_duck_` range -24..0 (default -10); hint "Quanto scende mentre parli."
- `QDoubleSpinBox* music_fade_in_` 0..10 s (1.5), `music_fade_out_` 0..10 s (3.0), `music_offset_` 0..60 s (0), `music_tail_` 0..30 s (0).
- `QCheckBox* music_loop_` "Ripeti se piu corta dell'episodio" (checked).
- `QPushButton* music_preview_` "Anteprima 15 s", `QPushButton* music_preview_stop_` "Stop".

Every control calls `sync_params()`, which now also fills `p.music_bed` from the widgets. `load_params()` sets the widgets from `p.music_bed`.

- [ ] **Step 3: Preview**

```cpp
void MasterPage::preview_music() {
    const Session& s = AppState::get().session();
    if (!s.has_audio || !s.has_music) {
        AppState::get().status_message(QStringLiteral("Carica un audio e una musica per l'anteprima"));
        return;
    }
    sync_params();
    const sanity::Params p = s.params;
    // The excerpt starts at the first second with speech so the preview shows
    // the ducking, not just the intro fade.
    std::vector<float> mono = sanity::to_mono(s.audio);
    const int fs = s.audio.sample_rate;
    sanity::Segmenter seg(sanity::SegmenterConfig{fs, 10.0, 35.0});
    std::vector<sanity::FrameFeat> feats;
    seg.features(mono, feats);
    std::size_t first = 0;
    for (std::size_t f = 0; f < feats.size(); f++) if (feats[f].speech) { first = f; break; }
    const std::size_t hop = static_cast<std::size_t>(sanity::segmenter_hop(fs));
    const std::size_t start = first > 100 ? (first - 100) * hop : 0;  // one second of lead-in
    const std::size_t len = std::min(mono.size() - start, static_cast<std::size_t>(15) * fs);
    std::vector<float> excerpt(mono.begin() + static_cast<long>(start), mono.begin() + static_cast<long>(start + len));
    std::vector<sanity::FrameFeat> ef(feats.begin() + static_cast<long>(start / hop),
                                      feats.begin() + static_cast<long>(std::min(feats.size(), (start + len) / hop)));
    auto music = s.music;
    music_preview_->setEnabled(false);
    music_card_->set_state(State::Busy, QStringLiteral("Anteprima"));
    auto* worker = QThread::create([this, excerpt, ef, music, p, fs]() mutable {
        sanity::MusicBedOptions o = p.music_bed;
        o.enabled = true;
        o.tail_s = std::min(o.tail_s, 3.0);
        sanity::mix_music_bed(excerpt, fs, ef, *music, o);
        sanity::normalize_loudness(excerpt, fs, -19.0, -1.0);  // listen at delivery level
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath(QStringLiteral("sanity-preview-bed.wav"));
        std::string err;
        const bool ok = sanity::wav_write(path.toStdString(), sanity::from_mono(fs, excerpt), true, &err);
        QMetaObject::invokeMethod(this, [this, ok, path, err] {
            music_preview_->setEnabled(true);
            music_card_->set_state(ok ? State::Ready : State::Error,
                                   ok ? QStringLiteral("In riproduzione") : QStringLiteral("Errore"));
            if (ok) play_file(path);
            else AppState::get().status_message(QString::fromStdString(err));
        }, Qt::QueuedConnection);
    });
    worker->setParent(this);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
```

Refactor `MasterPage::play(const QString& which)` so the QMediaPlayer part becomes `void MasterPage::play_file(const QString& path)` and `void MasterPage::stop_playback()` (the "Stop" button). `#include "sanity/music_bed.hpp"`, `"sanity/dynamics.hpp"`, `<QThread>`.

- [ ] **Step 4: Report and waveform**

`show_report()` adds a line to `verdict_` when the `music_bed` stage ran: "Sottofondo: livello %1 dB nelle pause, ducking su %2 % del tempo, coda %3 s." from the stage metrics. `build_stage_table()` already lists the stage.

- [ ] **Step 5: Self test covers the card**

In `gui/main.cpp` `SelfTest::run_mastering`, before `start_mastering`, when no music is loaded synthesise a 3 s stereo chord like the e2e test, store it with a new `AppState::set_music_buffer(std::shared_ptr<const AudioBuffer>, const QString& label)` (used by the file loader too), set `params.music_bed.enabled = true; tail_s = 1.0;`, and after mastering assert in `verify()` that `r.stages` contains an enabled `music_bed` stage, else `exit(7)` with `selftest: music bed stage missing`. The export and the loudness contract checks stay as they are (the bed is inside the target).

- [ ] **Step 6: Run, look, commit**

Run: full ctest (gui now exercises the bed). Then screenshots: `mastered-1.png` shows the music card with its chip "Attivo". By hand: pick a music file (any MP3/FLAC/WAV), press "Anteprima 15 s" and hear the music dip when the voice starts.

```bash
git add gui/app_state.h gui/app_state.cpp gui/pages/pages.h gui/pages/master_page.cpp gui/main.cpp
git commit -m "GUI: music bed card with level, ducking, fades, tail and a 15 second preview"
```

### Task 4.4: Document the bed

**Files:**
- Modify: `docs/DESIGN-podcast-sanity.md` section 2.1 (signal path) and a new 2.8
- Modify: `docs/TESTING.md` section 2.2 (new check row)
- Modify: `README.md` feature list

- [ ] **Step 1: Write it**

Signal path gains `music_bed` between `compressor` and `output`. New section 2.8 "Music bed (`src/music_bed.cpp`)": the reference level (active speech level), the ducking envelope (10 ms frames from the segmenter, hold, asymmetric smoothing), loop seams, fades, tail, why it sits before normalisation (the delivery loudness covers the mix) and after the Filler Killer (the cut is made on speech alone). TESTING.md gets the row "Music bed | conformant, frames = plain + tail, same filler count, deterministic". README lists "Background music bed with ducking and a 15 s preview".

- [ ] **Step 2: Commit**

```bash
git add docs/DESIGN-podcast-sanity.md docs/TESTING.md README.md
git commit -m "Docs: music bed stage"
```

---

## Phase 5: Recording-studio look

The owner's words: add interface elements that recall a podcasting recording studio; the eye wants its share. The plan keeps the seven pages and every control where it is, and changes what the user sees: a dark "Studio" theme with brushed-metal panels and an amber accent, an analogue VU meter and an "ON AIR" sign on the recording page, painted transport buttons, LED indicators on every status chip, a waveform with a time ruler and dB grid, painted navigation icons, and the Ubuntu typeface. Everything is drawn with QPainter and QSS: no images except the font files, no network.

Verification for every task in this phase is the screenshot set: `podcast-sanity.exe --selftest --selftest-seconds 8 --selftest-shots .\build-windows\shots` writes `page-0.png` to `page-6.png` and `mastered-1/2/4/6.png`. Task 5.1 adds a ctest that produces them on every run.

### Task 5.1: Theme "Studio", bundled Ubuntu font, screenshot test

Closes F34 (part), enables the rest of the phase.

**Files:**
- Modify: `gui/theme.h`, `gui/theme.cpp`
- Modify: `gui/main.cpp:224-227` (default theme, font loading)
- Modify: `gui/pages/settings_page.cpp:37-47` (third theme entry)
- Modify: `gui/mainwindow.cpp:238-250` (toggle cycles three themes)
- Create: `data/fonts/Ubuntu-Regular.ttf`, `data/fonts/Ubuntu-Medium.ttf`, `data/fonts/Ubuntu-Bold.ttf`, `data/fonts/UbuntuMono-Regular.ttf`, `data/fonts/UFL-1.0.txt`
- Modify: `CMakeLists.txt` (resource list, ctest `gui_shots`), `packaging/THIRD-PARTY.md`

**Interfaces:**
- `enum class ThemeMode { Light, Dark, Studio };` `Tokens` gains `QColor panel_hi, panel_lo, vu_face, vu_needle, led_off, rec, glow;`.
- `QString ui_family();` returns `"Ubuntu"` when the bundled font loaded, otherwise the first available of the current list.

- [ ] **Step 1: Add the screenshot test so every UI task has a check**

```cmake
    if(WIN32)
      add_test(NAME gui_shots COMMAND podcast-sanity --selftest --selftest-seconds 8
               --selftest-shots "${CMAKE_CURRENT_BINARY_DIR}/shots")
      set_tests_properties(gui_shots PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "selftest: OK")
    endif()
```

Run: `ctest -C Release -R gui_shots`. Expected: PASS and 11 PNG files in `build-windows\shots`.

- [ ] **Step 2: Fetch the Ubuntu font family once and bundle it**

Download the official package `ubuntu-font-family-0.83.zip` from the Ubuntu design site (https://design.ubuntu.com/font), unzip into a scratch folder, copy `Ubuntu-R.ttf` as `data/fonts/Ubuntu-Regular.ttf`, `Ubuntu-M.ttf` as `Ubuntu-Medium.ttf`, `Ubuntu-B.ttf` as `Ubuntu-Bold.ttf`, `UbuntuMono-R.ttf` as `UbuntuMono-Regular.ttf`, and `LICENCE.txt` as `data/fonts/UFL-1.0.txt`. Add to `CMakeLists.txt`:

```cmake
    qt_add_resources(podcast-sanity app_fonts PREFIX "/fonts" BASE data/fonts
      FILES data/fonts/Ubuntu-Regular.ttf data/fonts/Ubuntu-Medium.ttf
            data/fonts/Ubuntu-Bold.ttf data/fonts/UbuntuMono-Regular.ttf)
    install(FILES data/fonts/UFL-1.0.txt DESTINATION licenses/ubuntu-font)
```

In `packaging/THIRD-PARTY.md` add: "The Ubuntu font family is embedded under the Ubuntu Font Licence 1.0 (licenses/ubuntu-font/UFL-1.0.txt)."

In `gui/main.cpp` before the style is set:

```cpp
    for (const char* f : {":/fonts/Ubuntu-Regular.ttf", ":/fonts/Ubuntu-Medium.ttf",
                          ":/fonts/Ubuntu-Bold.ttf", ":/fonts/UbuntuMono-Regular.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(f));
    QApplication::setFont(QFont(gui::ui_family(), 10));
```

- [ ] **Step 3: Tokens and the Studio palette**

`theme.h`:

```cpp
enum class ThemeMode { Light, Dark, Studio };

struct Tokens {
    QColor bg, bg_alt, card, card_alt, sidebar, border;
    QColor text, text_dim, text_faint;
    QColor accent, accent_soft, accent_text;
    QColor success, warning, danger;
    QColor meter_low, meter_mid, meter_high;
    // Studio furniture: brushed panel gradient ends, VU face and needle, LED off
    // colour, the record red, and a translucent glow for lit elements.
    QColor panel_hi, panel_lo, vu_face, vu_needle, led_off, rec, glow;
};
QString ui_family();
```

`theme.cpp` `make_studio()`:

```cpp
Tokens make_studio() {
    Tokens t;
    t.bg = QColor("#0E1012");
    t.bg_alt = QColor("#15181B");
    t.card = QColor("#1A1E22");
    t.card_alt = QColor("#14171A");
    t.sidebar = QColor("#0B0D0F");
    t.border = QColor("#2A3036");
    t.text = QColor("#E6E3DA");
    t.text_dim = QColor("#A4A79E");
    t.text_faint = QColor("#6C716A");
    t.accent = QColor("#F2A33C");       // amber, the colour of a lit VU scale
    t.accent_soft = QColor("#3A2C14");
    t.accent_text = QColor("#141414");
    t.success = QColor("#46D37A");
    t.warning = QColor("#F2C14E");
    t.danger = QColor("#E0463A");
    t.meter_low = QColor("#46D37A");
    t.meter_mid = QColor("#F2C14E");
    t.meter_high = QColor("#E0463A");
    t.panel_hi = QColor("#262C31");
    t.panel_lo = QColor("#15191C");
    t.vu_face = QColor("#F4EAD2");
    t.vu_needle = QColor("#2B2B2B");
    t.led_off = QColor("#3A3F44");
    t.rec = QColor("#E0463A");
    t.glow = QColor(242, 163, 60, 90);
    return t;
}
```

Fill the same seven new fields for Light (`panel_hi #F7F5EE`, `panel_lo #E7E3D6`, `vu_face #FFFDF6`, `vu_needle #2B302A`, `led_off #C9C4B6`, `rec #C0453A`, `glow (124,144,130,70)`) and Dark (`panel_hi #262C27`, `panel_lo #1B201C`, `vu_face #E9ECE2`, `vu_needle #181C19`, `led_off #333B34`, `rec #D2503F`, `glow (156,184,162,80)`). `tokens()` returns the studio set for `ThemeMode::Studio`.

`ui_family()`:

```cpp
QString ui_family() {
    static const QString family = [] {
        const QStringList have = QFontDatabase::families();
        for (const QString& w : {QStringLiteral("Ubuntu"), QStringLiteral("Inter"),
                                 QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans")})
            if (have.contains(w)) return w;
        return QString("sans-serif");
    }();
    return family;
}
```

`mono_family()` puts `"Ubuntu Mono"` first in its list.

- [ ] **Step 4: Stylesheet additions**

In `style_sheet()` replace the `font-family` line with `font-family: "%18";` and pass `ui_family()` as the 18th argument. Append these rules (new arguments `%19` = `panel_hi`, `%20` = `panel_lo`):

```css
QFrame#Sidebar {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %19, stop:1 %7);
    border: none; border-right: 1px solid %6;
}
QFrame#CardHeader {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %19, stop:1 %20);
    border: none; border-bottom: 1px solid %6;
    border-top-left-radius: 12px; border-top-right-radius: 12px;
}
QPushButton#Nav:checked { background: %8; color: %3; font-weight: 600; border-left: 3px solid %10; padding-left: 9px; }
QLabel#PageTitle { font-size: 24px; font-weight: 700; letter-spacing: 0.5px; background: transparent; }
QLabel#BrandTitle { font-size: 17px; font-weight: 700; letter-spacing: 1px; text-transform: uppercase; background: transparent; }
QLabel#Mono, QPlainTextEdit, QTextEdit { font-family: "%9"; }
```

`QPushButton#Rec` keeps its red; the painted transport buttons of Task 5.3 do not use QSS for their glyphs.

- [ ] **Step 5: Three themes everywhere**

- `main.cpp`: `const int theme = st.value("ui/theme", 2).toInt();` and map `0 Light, 1 Dark, 2 Studio` (Studio is the default for new installs).
- `settings_page.cpp`: add `theme_->addItem(QStringLiteral("Studio (scuro, ambra)"), 2);` and make it the default index; `apply_theme()` maps the three values.
- `mainwindow.cpp` `toggle_theme()`: cycle `Light -> Dark -> Studio -> Light`; button text "Tema: chiaro / scuro / studio" showing the current one.

- [ ] **Step 6: Run, look, commit**

Run: full ctest; open `build-windows\shots\page-0.png`: dark charcoal background, amber accents, the brand in Ubuntu. Light and Dark still render (switch in Settings, no restart needed since Task 2.7).

```bash
git add gui/theme.h gui/theme.cpp gui/main.cpp gui/pages/settings_page.cpp gui/mainwindow.cpp data/fonts CMakeLists.txt packaging/THIRD-PARTY.md
git commit -m "GUI: Studio theme (charcoal and amber), bundled Ubuntu font, screenshot test"
```

### Task 5.2: Studio widgets: VU meter, ON AIR sign, LED

**Files:**
- Create: `gui/studio_widgets.h`, `gui/studio_widgets.cpp`
- Modify: `CMakeLists.txt` (add the two files to the GUI target)

**Interfaces:**
```cpp
namespace gui {
// Analogue meter: a needle over a cream face with a black scale and a red
// zone above 0. Values arrive in dBFS (or LUFS); `set_reference` says which
// dBFS reads 0 on the scale (-18 dBFS = 0 VU is the usual studio alignment).
class VuMeter : public QWidget {
    Q_OBJECT
public:
    explicit VuMeter(const QString& caption, QWidget* parent = nullptr);
    void set_scale(double min_units, double max_units, const QString& unit);  // default -20..+3 "VU"
    void set_ticks(const std::vector<double>& units);                         // labelled ticks
    void set_reference(double dbfs_at_zero);                                   // default -18
    void set_value_dbfs(double dbfs);          // needle target, 300 ms ballistics
    void set_peak_dbfs(double dbfs);           // lights the peak LED above -1 dBFS for 1 s
    void set_target(double units);             // optional amber marker (e.g. the loudness target)
    void reset();
    QSize sizeHint() const override { return QSize(260, 150); }
    QSize minimumSizeHint() const override { return QSize(180, 110); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void tick();
    QString caption_, unit_ = QStringLiteral("VU");
    double min_ = -20.0, max_ = 3.0, reference_ = -18.0;
    double target_ = -200.0, shown_ = -200.0, has_target_ = false;
    double marker_ = 0.0; bool has_marker_ = false;
    std::vector<double> ticks_ = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    QTimer* timer_ = nullptr;
    qint64 peak_lit_until_ = 0;
};

// A rectangular studio sign. Lit: red field, white letters, glow. Unlit: dim.
class OnAirSign : public QWidget {
    Q_OBJECT
public:
    explicit OnAirSign(QWidget* parent = nullptr);
    void set_on(bool on);
    void set_text(const QString& text);  // default "ON AIR"
    QSize sizeHint() const override { return QSize(132, 44); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    bool on_ = false;
    QString text_ = QStringLiteral("ON AIR");
};

// A 12 px LED coloured by State; Busy pulses.
class LedIndicator : public QWidget {
    Q_OBJECT
public:
    explicit LedIndicator(QWidget* parent = nullptr);
    void set_state(State s);
    QSize sizeHint() const override { return QSize(14, 14); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    State state_ = State::Idle;
    QTimer* pulse_ = nullptr;
    double phase_ = 0.0;
};
}  // namespace gui
```

- [ ] **Step 1: Implement the VU meter**

```cpp
// podcast-sanity GUI - studio furniture: VU meter, on-air sign, LEDs
#include "studio_widgets.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "theme.h"

namespace gui {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSweepDeg = 100.0;  // total needle travel
}  // namespace

VuMeter::VuMeter(const QString& caption, QWidget* parent) : QWidget(parent), caption_(caption) {
    setMinimumHeight(110);
    timer_ = new QTimer(this);
    timer_->setInterval(30);
    connect(timer_, &QTimer::timeout, this, &VuMeter::tick);
}

void VuMeter::set_scale(double min_units, double max_units, const QString& unit) {
    min_ = min_units; max_ = max_units; unit_ = unit; update();
}
void VuMeter::set_ticks(const std::vector<double>& units) { ticks_ = units; update(); }
void VuMeter::set_reference(double dbfs_at_zero) { reference_ = dbfs_at_zero; update(); }
void VuMeter::set_value_dbfs(double dbfs) {
    target_ = std::clamp(dbfs - reference_, min_ - 1.0, max_ + 1.0);
    has_target_ = true;
    if (shown_ < -100.0) shown_ = min_;
    if (!timer_->isActive()) timer_->start();
}
void VuMeter::set_peak_dbfs(double dbfs) {
    if (dbfs > -1.0) {
        peak_lit_until_ = QDateTime::currentMSecsSinceEpoch() + 1000;
        update();
    }
}
void VuMeter::set_target(double units) { marker_ = units; has_marker_ = true; update(); }
void VuMeter::reset() { has_target_ = false; shown_ = min_; peak_lit_until_ = 0; update(); }

void VuMeter::tick() {
    // VU ballistics: about 300 ms to 99 %, same up and down.
    const double a = 1.0 - std::exp(-30.0 / 100.0);
    shown_ += a * ((has_target_ ? target_ : min_) - shown_);
    if (std::fabs(shown_ - (has_target_ ? target_ : min_)) < 0.02 &&
        QDateTime::currentMSecsSinceEpoch() > peak_lit_until_)
        timer_->stop();
    update();
}

void VuMeter::paintEvent(QPaintEvent*) {
    const Tokens& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect().adjusted(1, 1, -1, -1);

    // Bezel: brushed panel with a rounded cream face inset.
    QPainterPath bezel;
    bezel.addRoundedRect(r, 10, 10);
    QLinearGradient metal(r.topLeft(), r.bottomLeft());
    metal.setColorAt(0.0, t.panel_hi);
    metal.setColorAt(1.0, t.panel_lo);
    p.fillPath(bezel, metal);
    p.setPen(QPen(t.border, 1));
    p.drawPath(bezel);
    const QRectF face = r.adjusted(10, 10, -10, -28);
    QPainterPath fp;
    fp.addRoundedRect(face, 6, 6);
    p.fillPath(fp, t.vu_face);

    // Geometry: the pivot sits below the face so the needle sweeps a shallow arc.
    const QPointF pivot(face.center().x(), face.bottom() + face.height() * 0.55);
    const double radius = face.height() * 1.25;
    const double a0 = 270.0 - kSweepDeg / 2.0;  // degrees, clockwise from +x with y down
    auto angle_of = [&](double units) {
        const double f = std::clamp((units - min_) / (max_ - min_), 0.0, 1.0);
        return a0 + f * kSweepDeg;
    };
    auto at = [&](double deg, double rad) {
        const double ra = deg * kPi / 180.0;
        return QPointF(pivot.x() + rad * std::cos(ra), pivot.y() + rad * std::sin(ra));
    };
    auto arc = [&](double from_units, double to_units, double rad, const QPen& pen) {
        QPolygonF poly;
        const double d0 = angle_of(from_units), d1 = angle_of(to_units);
        for (double d = d0; d <= d1 + 1e-9; d += 1.0) poly << at(d, rad);
        p.setPen(pen);
        p.drawPolyline(poly);
    };
    p.setClipPath(fp);
    arc(min_, std::min(max_, 0.0), radius, QPen(t.vu_needle, 2));
    if (max_ > 0.0) arc(0.0, max_, radius, QPen(t.danger, 4));
    QFont small = font();
    small.setPointSizeF(std::max(6.5, font().pointSizeF() - 2.5));
    p.setFont(small);
    for (double u : ticks_) {
        if (u < min_ || u > max_) continue;
        const double d = angle_of(u);
        const bool major = std::fabs(u - std::round(u)) < 1e-9 && (static_cast<int>(std::round(u)) % 5 == 0 || u >= -3.0);
        p.setPen(QPen(u > 0 ? t.danger : t.vu_needle, major ? 2 : 1));
        p.drawLine(at(d, radius - (major ? 10 : 6)), at(d, radius));
        if (major) {
            const QString label = u > 0 ? QStringLiteral("+%1").arg(u) : QString::number(u);
            const QPointF c = at(d, radius - 20);
            p.drawText(QRectF(c.x() - 16, c.y() - 8, 32, 16), Qt::AlignCenter, label);
        }
    }
    if (has_marker_) {
        p.setPen(QPen(t.accent, 2, Qt::DashLine));
        p.drawLine(at(angle_of(marker_), radius - 14), at(angle_of(marker_), radius + 6));
    }
    // Needle with a soft shadow, then the pivot cap.
    const double nd = angle_of(has_target_ ? shown_ : min_);
    p.setPen(QPen(QColor(0, 0, 0, 60), 4));
    p.drawLine(pivot + QPointF(1.5, 1.5), at(nd, radius + 4) + QPointF(1.5, 1.5));
    p.setPen(QPen(t.vu_needle, 2));
    p.drawLine(pivot, at(nd, radius + 4));
    p.setClipping(false);
    p.setBrush(t.vu_needle);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(pivot.x(), face.bottom() - 2), 5, 5);

    // Unit, caption, peak LED.
    p.setPen(t.vu_needle);
    QFont unit = font();
    unit.setBold(true);
    p.setFont(unit);
    p.drawText(face.adjusted(0, face.height() * 0.42, 0, 0), Qt::AlignHCenter | Qt::AlignTop, unit_);
    p.setFont(font());
    p.setPen(t.text_dim);
    p.drawText(QRectF(r.left() + 12, r.bottom() - 22, r.width() - 24, 18), Qt::AlignLeft | Qt::AlignVCenter, caption_);
    const bool lit = QDateTime::currentMSecsSinceEpoch() < peak_lit_until_;
    const QPointF led(r.right() - 18, r.bottom() - 13);
    if (lit) {
        p.setBrush(t.glow);
        p.drawEllipse(led, 9, 9);
    }
    p.setBrush(lit ? t.danger : t.led_off);
    p.drawEllipse(led, 5, 5);
    p.setPen(t.text_faint);
    p.drawText(QRectF(led.x() - 60, led.y() - 8, 50, 16), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("PEAK"));
}
```

- [ ] **Step 2: Implement the sign and the LED**

```cpp
OnAirSign::OnAirSign(QWidget* parent) : QWidget(parent) { setMinimumSize(110, 36); }
void OnAirSign::set_on(bool on) { on_ = on; update(); }
void OnAirSign::set_text(const QString& text) { text_ = text; update(); }

void OnAirSign::paintEvent(QPaintEvent*) {
    const Tokens& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect().adjusted(4, 4, -4, -4);
    if (on_) {
        QPainterPath glow;
        glow.addRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
        p.fillPath(glow, QColor(t.rec.red(), t.rec.green(), t.rec.blue(), 70));
    }
    QPainterPath body;
    body.addRoundedRect(r, 7, 7);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (on_) {
        g.setColorAt(0.0, t.rec.lighter(115));
        g.setColorAt(1.0, t.rec.darker(120));
    } else {
        g.setColorAt(0.0, t.panel_hi);
        g.setColorAt(1.0, t.panel_lo);
    }
    p.fillPath(body, g);
    p.setPen(QPen(on_ ? t.rec.darker(140) : t.border, 1));
    p.drawPath(body);
    QFont f = font();
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    f.setPointSizeF(font().pointSizeF() + 2.0);
    p.setFont(f);
    p.setPen(on_ ? QColor("#FFF6F2") : t.text_faint);
    p.drawText(r, Qt::AlignCenter, text_);
}

LedIndicator::LedIndicator(QWidget* parent) : QWidget(parent) {
    setFixedSize(14, 14);
    pulse_ = new QTimer(this);
    pulse_->setInterval(40);
    connect(pulse_, &QTimer::timeout, this, [this] {
        phase_ = std::fmod(phase_ + 0.08, 2.0 * kPi);
        update();
    });
}
void LedIndicator::set_state(State s) {
    state_ = s;
    if (s == State::Busy) pulse_->start();
    else pulse_->stop();
    update();
}
void LedIndicator::paintEvent(QPaintEvent*) {
    const Tokens& t = tokens();
    QColor c = t.led_off;
    switch (state_) {
        case State::Idle: c = t.led_off; break;
        case State::Busy: c = t.accent; break;
        case State::Ready: c = t.success; break;
        case State::Warning: c = t.warning; break;
        case State::Error: c = t.danger; break;
    }
    if (state_ == State::Busy) c.setAlphaF(0.55 + 0.45 * (0.5 + 0.5 * std::sin(phase_)));
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    if (state_ != State::Idle) {
        QColor glow = c;
        glow.setAlpha(70);
        p.setBrush(glow);
        p.drawEllipse(rect().center(), 7, 7);
    }
    p.setBrush(c);
    p.drawEllipse(rect().center(), 4, 4);
    p.setBrush(QColor(255, 255, 255, state_ == State::Idle ? 40 : 110));
    p.drawEllipse(QPointF(rect().center().x() - 1.3, rect().center().y() - 1.3), 1.2, 1.2);
}

}  // namespace gui
```

- [ ] **Step 3: Compile, commit**

Add `gui/studio_widgets.h gui/studio_widgets.cpp` to the GUI target. Build. Expected: compiles; nothing uses the widgets yet.

```bash
git add gui/studio_widgets.h gui/studio_widgets.cpp CMakeLists.txt
git commit -m "GUI: VU meter, ON AIR sign and LED indicator widgets"
```

### Task 5.3: The recording page becomes a desk

Closes F33 (meter honesty), F34 (transport glyphs).

**Files:**
- Modify: `gui/widgets.h`, `gui/widgets.cpp` (TransportButton, Chip with LED, Card header LED)
- Modify: `gui/pages/record_page.cpp`, `gui/pages/pages.h:53-116`
- Modify: `gui/mainwindow.cpp` (ON AIR in the sidebar, LED in the status bar)

**Interfaces:**
```cpp
class TransportButton : public QPushButton {
    Q_OBJECT
public:
    enum class Glyph { Record, Pause, Play, Stop, Marker };
    TransportButton(Glyph g, const QString& text, QWidget* parent = nullptr);
    void set_glyph(Glyph g);
    void set_lit(bool on);   // Record: red ring glows while recording
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Glyph glyph_;
    bool lit_ = false;
};
```

- [ ] **Step 1: Painted transport buttons**

In `widgets.cpp`:

```cpp
TransportButton::TransportButton(Glyph g, const QString& text, QWidget* parent)
    : QPushButton(text, parent), glyph_(g) {
    setObjectName(g == Glyph::Record ? "Rec" : "Transport");
    setMinimumHeight(g == Glyph::Record ? 52 : 40);
    setCursor(Qt::PointingHandCursor);
}
void TransportButton::set_glyph(Glyph g) { glyph_ = g; update(); }
void TransportButton::set_lit(bool on) { lit_ = on; update(); }

void TransportButton::paintEvent(QPaintEvent* e) {
    QPushButton::paintEvent(e);  // QSS background, border, text
    const Tokens& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Glyph box at the left edge, vertically centred; the text has a leading
    // space in the label so it does not collide.
    const double s = std::min(18.0, height() * 0.42);
    const QRectF g(14.0, height() / 2.0 - s / 2.0, s, s);
    const QColor ink = glyph_ == Glyph::Record ? QColor("#FFFFFF") : t.text;
    p.setPen(Qt::NoPen);
    switch (glyph_) {
        case Glyph::Record:
            if (lit_) {
                p.setBrush(QColor(255, 255, 255, 90));
                p.drawEllipse(g.center(), s * 0.7, s * 0.7);
            }
            p.setBrush(ink);
            p.drawEllipse(g.center(), s * 0.42, s * 0.42);
            break;
        case Glyph::Pause:
            p.setBrush(ink);
            p.drawRoundedRect(QRectF(g.left() + s * 0.15, g.top(), s * 0.26, s), 2, 2);
            p.drawRoundedRect(QRectF(g.right() - s * 0.41, g.top(), s * 0.26, s), 2, 2);
            break;
        case Glyph::Play: {
            QPolygonF tri;
            tri << QPointF(g.left() + s * 0.2, g.top()) << QPointF(g.right(), g.center().y())
                << QPointF(g.left() + s * 0.2, g.bottom());
            p.setBrush(ink);
            p.drawPolygon(tri);
            break;
        }
        case Glyph::Stop:
            p.setBrush(ink);
            p.drawRoundedRect(g.adjusted(s * 0.1, s * 0.1, -s * 0.1, -s * 0.1), 2, 2);
            break;
        case Glyph::Marker: {
            p.setPen(QPen(ink, 2));
            p.drawLine(QPointF(g.left() + 2, g.top()), QPointF(g.left() + 2, g.bottom()));
            QPolygonF flag;
            flag << QPointF(g.left() + 2, g.top()) << QPointF(g.right(), g.top() + s * 0.25)
                 << QPointF(g.left() + 2, g.top() + s * 0.5);
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawPolygon(flag);
            break;
        }
    }
}
```

Add to the QSS: `QPushButton#Rec, QPushButton#Transport { text-align: left; padding-left: 40px; }`.

- [ ] **Step 2: Chips and card headers get an LED**

`Card` gains a `LedIndicator* led_` placed before `chip_` in the header; `Card::set_state` forwards the state to both. `MainWindow::build_statusbar` adds a `LedIndicator` before `state_chip_` and updates it wherever `state_chip_->set_state` is called (wrap both in a small `void MainWindow::set_global_state(State, const QString&)`).

- [ ] **Step 3: Rebuild the recording page top**

In `RecordPage` replace the four `QPushButton`s with `TransportButton`s (`Glyph::Record " REC"`, `Glyph::Pause " PAUSA"`, `Glyph::Stop " STOP"`, `Glyph::Marker " Marker (M)"`), drop the Unicode glyphs from the labels, and call `rec_->set_lit(true)` in `start_recording()` and `set_lit(false)` in `stop_recording()`. Pause/resume swaps the glyph with `set_glyph(Glyph::Play)` / `Glyph::Pause`.

Replace the two `LevelMeter`s with a meter strip:

```cpp
        auto* meter_host = new QWidget();
        auto* meters = new QHBoxLayout(meter_host);
        meters->setContentsMargins(0, 0, 0, 0);
        meters->setSpacing(14);
        vu_ = new VuMeter(QStringLiteral("Ingresso  (0 VU = -18 dBFS)"), meter_host);
        meters->addWidget(vu_, 1);
        auto* bars = new QVBoxLayout();
        bars->setSpacing(6);
        bars->addWidget(new QLabel(QStringLiteral("Picco campione")));
        in_meter_ = new LevelMeter(meter_host);
        in_meter_->set_range(kMeterMin, kMeterMax);
        in_meter_->set_target(-6.0);
        bars->addWidget(in_meter_);
        bars->addWidget(new QLabel(QStringLiteral("True peak (4x)")));
        tp_meter_ = new LevelMeter(meter_host);
        tp_meter_->set_range(kMeterMin, kMeterMax);
        tp_meter_->set_target(-1.0);
        bars->addWidget(tp_meter_);
        bars->addStretch(1);
        meters->addLayout(bars, 1);
        transport_->add_widget(meter_host);
```

Add `VuMeter* vu_ = nullptr;` to `RecordPage`. `on_level()` becomes honest: `vu_->set_value_dbfs(rms_db); vu_->set_peak_dbfs(peak_db); in_meter_->set_value(peak_db); in_meter_->set_peak(peak_db); tp_meter_->set_value(true_peak_db); tp_meter_->set_true_peak(true_peak_db);` where `true_peak_db` is a new fourth argument of `Capture::meter`, computed in `Capture::emit_meter()` as `static_cast<float>(sanity::gain_to_db(sanity::oversample_4x_peak(meter_buf_)))` on the rolling window (1.5 s at 48 kHz is cheap: the peak search skips most windows).

Remove the controls that do nothing (`anti_hum_`, `auto_split_`, `monitor_`, `monitor_dev_`) and their rows; the "Anti-ronzio" option already lives on the Master page as "Anti-ronzio di rete". Replace the invented noise floor in `run_mic_test()` with the measured one: during the three seconds keep the minimum `rms_db` seen in `on_level` (`mic_test_floor_db_`), and print `Rumore di fondo misurato %3 dB` from it.

- [ ] **Step 4: ON AIR in the sidebar**

In `MainWindow::build_sidebar()` after the tagline add `on_air_ = new OnAirSign(side); v->addWidget(on_air_, 0, Qt::AlignLeft);` (member `OnAirSign* on_air_`). `RecordPage` emits two new signals `recording_started()` and `recording_stopped()` from `start_recording()` / `stop_recording()`; `MainWindow::build_pages()` keeps the `RecordPage*` and connects them to `on_air_->set_on(true/false)`. Also set the window title to `"podcast-sanity  ON AIR"` while recording.

- [ ] **Step 5: Run, look, commit**

Run: full ctest; `page-0.png` shows the VU meter with its needle at rest, the two bar meters, the painted REC/PAUSA/STOP/Marker buttons and the dark ON AIR sign in the sidebar. By hand: REC with the corpus source: the needle moves, the sign lights red, the PEAK LED flashes on loud syllables.

```bash
git add gui/widgets.h gui/widgets.cpp gui/audio_capture.h gui/audio_capture.cpp gui/pages/record_page.cpp gui/pages/pages.h gui/mainwindow.h gui/mainwindow.cpp
git commit -m "GUI: studio recording desk: VU meter, true peak bar, painted transport, ON AIR sign, LEDs on every chip"
```

### Task 5.4: Painted navigation icons and the loudness meter on the master page

Closes F34 (navigation), F35 (load button).

**Files:**
- Modify: `gui/widgets.h`, `gui/widgets.cpp` (NavButton paints an icon)
- Modify: `gui/mainwindow.cpp:27-41, 133-139`
- Modify: `gui/pages/master_page.cpp`, `gui/pages/pages.h`

- [ ] **Step 1: NavButton with a painted icon**

```cpp
enum class NavIcon { Mic, Sliders, Gauge, Library, Upload, Gear, Stethoscope };
class NavButton : public QPushButton {
    Q_OBJECT
public:
    NavButton(NavIcon icon, const QString& text, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    NavIcon icon_;
};
```

```cpp
NavButton::NavButton(NavIcon icon, const QString& text, QWidget* parent)
    : QPushButton(parent), icon_(icon) {
    setObjectName("Nav");
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setText(QStringLiteral("      %1").arg(text));  // room for the icon
    setMinimumHeight(38);
}

void NavButton::paintEvent(QPaintEvent* e) {
    QPushButton::paintEvent(e);
    const Tokens& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = isChecked() ? t.accent : t.text_dim;
    p.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const QRectF b(14, height() / 2.0 - 9, 18, 18);  // 18 px icon box
    switch (icon_) {
        case NavIcon::Mic:
            p.drawRoundedRect(QRectF(b.center().x() - 3.5, b.top(), 7, 11), 3.5, 3.5);
            p.drawArc(QRectF(b.center().x() - 6.5, b.top() + 4, 13, 11), 180 * 16, 180 * 16);
            p.drawLine(QPointF(b.center().x(), b.top() + 15), QPointF(b.center().x(), b.bottom()));
            break;
        case NavIcon::Sliders:
            for (int i = 0; i < 3; i++) {
                const double y = b.top() + 3 + i * 6;
                p.drawLine(QPointF(b.left(), y), QPointF(b.right(), y));
                const double x = b.left() + (i == 1 ? 12 : (i == 0 ? 5 : 9));
                p.setBrush(ink);
                p.drawEllipse(QPointF(x, y), 2.2, 2.2);
                p.setBrush(Qt::NoBrush);
            }
            break;
        case NavIcon::Gauge:
            p.drawArc(b.adjusted(1, 3, -1, 1), 20 * 16, 140 * 16);
            p.drawLine(b.center() + QPointF(0, 2), b.center() + QPointF(5, -4));
            p.setBrush(ink);
            p.drawEllipse(b.center() + QPointF(0, 2), 1.6, 1.6);
            break;
        case NavIcon::Library:
            p.drawRoundedRect(QRectF(b.left() + 1, b.top() + 2, 4, 14), 1, 1);
            p.drawRoundedRect(QRectF(b.left() + 7, b.top() + 2, 4, 14), 1, 1);
            p.drawLine(QPointF(b.left() + 13, b.top() + 3), QPointF(b.right(), b.bottom() - 1));
            break;
        case NavIcon::Upload:
            p.drawLine(QPointF(b.center().x(), b.top() + 1), QPointF(b.center().x(), b.bottom() - 5));
            p.drawPolyline(QPolygonF() << QPointF(b.center().x() - 5, b.top() + 6) << QPointF(b.center().x(), b.top() + 1)
                                       << QPointF(b.center().x() + 5, b.top() + 6));
            p.drawPolyline(QPolygonF() << QPointF(b.left(), b.bottom() - 5) << QPointF(b.left(), b.bottom())
                                       << QPointF(b.right(), b.bottom()) << QPointF(b.right(), b.bottom() - 5));
            break;
        case NavIcon::Gear:
            for (int i = 0; i < 8; i++) {
                const double a = i * kPi / 4.0;
                p.drawLine(b.center() + QPointF(6 * std::cos(a), 6 * std::sin(a)),
                           b.center() + QPointF(8.5 * std::cos(a), 8.5 * std::sin(a)));
            }
            p.drawEllipse(b.center(), 5.5, 5.5);
            p.drawEllipse(b.center(), 2.0, 2.0);
            break;
        case NavIcon::Stethoscope:
            p.drawArc(QRectF(b.left() + 1, b.top() + 1, 10, 12), 180 * 16, 180 * 16);
            p.drawLine(QPointF(b.left() + 1, b.top() + 1), QPointF(b.left() + 1, b.top() + 7));
            p.drawLine(QPointF(b.left() + 11, b.top() + 1), QPointF(b.left() + 11, b.top() + 7));
            p.drawLine(QPointF(b.left() + 6, b.top() + 13), QPointF(b.left() + 6, b.bottom() - 2));
            p.drawEllipse(QPointF(b.right() - 2, b.bottom() - 2), 2.2, 2.2);
            break;
    }
}
```

`kPi` is in the anonymous namespace of `widgets.cpp` (add it). In `mainwindow.cpp` `kNav` becomes `{NavIcon, const char*}` pairs in the same order and the ASCII glyphs disappear.

- [ ] **Step 2: Loudness meter and a real load button on the master page**

- Replace the "Carica file audio..." hint button with a real file dialog: `const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Apri un audio"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation), QString::fromStdString(sanity::audio_read_filter()));` then `audio_read` and `AppState::get().set_audio(b, path, "file")` exactly as `MainWindow::open_audio` does; move that logic into `AppState::open_audio_file(const QString& path, QString* error)` and call it from both places.
- In the result card add two `VuMeter`s side by side above the tiles: `lufs_before_ = new VuMeter("Loudness prima")`, `lufs_after_ = new VuMeter("Loudness dopo")`, both `set_scale(-30, -10, "LUFS")`, `set_reference(0.0)`, `set_ticks({-30,-27,-24,-21,-19,-16,-14,-12,-10})`; `show_report()` calls `set_value_dbfs(r.before.integrated_lufs)` / `after`, and `set_target(target)` with the target from `r.params` (0 means auto: `r.after.channels > 1 ? -16 : -19`). Remove the `(void)target` dead lines.
- `show_candidates()` moves its `detect_fillers` call onto a `QThread::create` worker like the preview, posting the rows back with `QMetaObject::invokeMethod`.

- [ ] **Step 3: Run, look, commit**

Run: full ctest; `page-1.png` and `mastered-1.png` show the two loudness meters with the needle on -19 after mastering and the amber target marker; the sidebar shows seven painted icons with the active one in amber.

```bash
git add gui/widgets.h gui/widgets.cpp gui/mainwindow.cpp gui/app_state.h gui/app_state.cpp gui/pages/master_page.cpp gui/pages/pages.h
git commit -m "GUI: painted navigation icons, loudness meters on the master page, real load button"
```

### Task 5.5: Waveform with ruler and grid

**Files:**
- Modify: `gui/widgets.cpp:332-383`

- [ ] **Step 1: Draw the ruler and the grid before the envelope**

Inside `WaveformView::paintEvent` after the background fill:

```cpp
    // Studio grid: dB lines at -6, -12, -24 (both polarities) and a time ruler.
    const QRectF plot = r.adjusted(0, 0, 0, -16);
    const double mid = plot.center().y();
    const double scale = plot.height() * 0.46;
    p.setPen(QPen(t.border, 1, Qt::DotLine));
    for (double db : {-6.0, -12.0, -24.0}) {
        const double a = std::pow(10.0, db / 20.0) * scale;
        p.drawLine(QPointF(plot.left(), mid - a), QPointF(plot.right(), mid - a));
        p.drawLine(QPointF(plot.left(), mid + a), QPointF(plot.right(), mid + a));
        p.setPen(QPen(t.text_faint, 1));
        p.drawText(QRectF(plot.left() + 4, mid - a - 7, 40, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1 dB").arg(db));
        p.setPen(QPen(t.border, 1, Qt::DotLine));
    }
    if (duration_s_ > 0.0) {
        const double step = duration_s_ > 600 ? 60 : (duration_s_ > 120 ? 30 : (duration_s_ > 30 ? 10 : 5));
        p.setPen(QPen(t.text_faint, 1));
        for (double s = 0.0; s <= duration_s_; s += step) {
            const double x = plot.left() + (s / duration_s_) * plot.width();
            p.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
            p.drawText(QRectF(x - 24, plot.bottom() + 3, 48, 13), Qt::AlignHCenter | Qt::AlignTop,
                       fmt_seconds(s));
        }
    }
```

and use `plot` (not `r`) for the envelope, markers and playhead below. The envelope pen becomes `QPen(t.accent, 1)` with a second, dimmer pass `QPen(t.accent_soft, 1)` drawn first at 1.15x amplitude for a soft glow.

- [ ] **Step 2: Run, look, commit**

Run: full ctest; `mastered-1.png` shows the ruler and the dB grid behind the amber waveform.

```bash
git add gui/widgets.cpp
git commit -m "GUI: waveform with time ruler and dB grid"
```

---

## Phase 6: A complete Windows MSI

Closes F39, F40, F41, and the owner's request for an installer ready for distribution. The current MSI already installs per user with shortcuts, the Qt runtime, codecs, the VC++ runtime and licences. This phase makes it complete: branded dialogs, correct publisher metadata, a launch checkbox at the end, a trimmed payload, an uninstall and upgrade test, and a signing hook that is used as soon as a certificate exists.

### Task 6.1: Metadata, payload trimming, accepted ICE warnings

**Files:**
- Modify: `CMakeLists.txt` (project version, CPack block, windeployqt flags)
- Modify: `packaging/deploy-windows.cmake.in`
- Modify: `packaging/windows.rc.in`
- Modify: `data/io.github.voloire.podcast-sanity.metainfo.xml`

- [ ] **Step 1: Version and publisher**

- `project(podcast-sanity VERSION 0.2.0 LANGUAGES CXX)`.
- `set(CPACK_PACKAGE_CONTACT "Voloire")` (no e-mail), `set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Voloire/podcast-sanity")` (capital V, the real account), and in the WIX block:
  ```cmake
  set(CPACK_WIX_PROPERTY_ARPHELPLINK "https://github.com/Voloire/podcast-sanity/issues")
  set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://github.com/Voloire/podcast-sanity")
  set(CPACK_WIX_PROPERTY_ARPNOMODIFY "1")
  set(CPACK_WIX_PROPERTY_ARPCOMMENTS "Record and master a podcast locally, offline")
  # Accepted ICE warnings for a per-user install generated by CPack: ICE61 (same
  # version reinstall allowed), ICE69 (shortcut component layout CPack emits),
  # ICE90/ICE91 (per-user directories, by design).
  set(CPACK_WIX_LIGHT_EXTRA_FLAGS "-sice:ICE61" "-sice:ICE69" "-sice:ICE90" "-sice:ICE91")
  ```
- `packaging/windows.rc.in`: add `VALUE "LegalCopyright", "Franco Geraci (Voloire). GPL-3.0-or-later"` and `VALUE "Comments", "Record and master a podcast locally, offline"`.
- `metainfo.xml`: add `<release version="0.2.0" date="2026-10-XX">` (the executor fills the date of the release commit) with a one-paragraph description: multichannel loudness, music bed, distribution profiles, studio interface, complete MSI.

- [ ] **Step 2: Trim the payload**

Both `windeployqt` invocations (the POST_BUILD one in `CMakeLists.txt` and `deploy-windows.cmake.in`) get `--no-opengl-sw --skip-plugin-types tls,networkinformation,generic,qmltooling` after `--no-compiler-runtime`. Build the MSI and check that `opengl32sw.dll` and the `tls/` folder are gone from `build-windows\packages\_CPack_Packages\win64\WIX\podcast-sanity-0.2.0-win64\bin` and the MSI shrank by about 21 MB.

- [ ] **Step 3: Full build and MSI test**

Run: `.\packaging\build-windows.ps1` then `.\packaging\test-windows-msi.ps1 -Msi .\build-windows\packages\podcast-sanity-0.2.0-win64.msi`
Expected: both succeed; the installed app passes its self test with the trimmed runtime.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt packaging/deploy-windows.cmake.in packaging/windows.rc.in data/io.github.voloire.podcast-sanity.metainfo.xml
git commit -m "Packaging: version 0.2.0, publisher metadata without e-mail, trimmed Qt payload, accepted ICE warnings"
```

### Task 6.2: Branded installer dialogs and a launch checkbox

**Files:**
- Modify: `packaging/generate-icon.py` (also writes the two bitmaps)
- Create: `data/wix-banner.bmp` (493x58), `data/wix-dialog.bmp` (493x312)
- Create: `packaging/wix_patch.xml`
- Modify: `CMakeLists.txt` (patch file, extensions, properties)

- [ ] **Step 1: Bitmaps from the studio palette**

Append to `generate-icon.py`:

```python
# WiX UI bitmaps: the banner strip (493x58) and the welcome/finish side panel
# (493x312), in the Studio palette, with the Ubuntu font bundled in data/fonts.
from PIL import ImageFont
root = Path(__file__).resolve().parents[1]
def ubuntu(size, weight="Bold"):
    try:
        return ImageFont.truetype(str(root / f"data/fonts/Ubuntu-{weight}.ttf"), size)
    except OSError:
        return ImageFont.load_default()
charcoal, amber, cream = (14, 16, 18), (242, 163, 60), (230, 227, 218)
banner = Image.new("RGB", (493, 58), charcoal)
d = ImageDraw.Draw(banner)
d.rectangle((0, 54, 493, 58), fill=amber)
d.text((16, 14), "PODCAST SANITY", font=ubuntu(22), fill=cream)
d.text((238, 22), "record  -  clean  -  publish", font=ubuntu(13, "Regular"), fill=(164, 167, 158))
banner.save(root / "data/wix-banner.bmp")
dialog = Image.new("RGB", (493, 312), charcoal)
d = ImageDraw.Draw(dialog)
d.rectangle((0, 0, 164, 312), fill=(21, 24, 27))
for x, y, h in ((28, 150, 40), (46, 128, 84), (64, 108, 124), (82, 136, 68), (100, 116, 108), (118, 144, 52), (136, 132, 76)):
    d.rounded_rectangle((x, y, x + 10, y + h), radius=5, fill=amber)
d.text((184, 40), "Podcast Sanity", font=ubuntu(28), fill=cream)
d.text((184, 84), "Record a podcast and master it locally.", font=ubuntu(14, "Regular"), fill=(164, 167, 158))
d.text((184, 106), "No upload, no cloud, no telemetry.", font=ubuntu(14, "Regular"), fill=(164, 167, 158))
dialog.save(root / "data/wix-dialog.bmp")
```

Run `python packaging\generate-icon.py` (Pillow is installed on this machine for the icon already; if not, `python -m pip install pillow`). Check both files in; `.gitattributes` already marks `*.bmp` binary. `CMakeLists.txt` already references them when present.

- [ ] **Step 2: Launch checkbox on the finish page**

`packaging/wix_patch.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<CPackWiXPatch>
  <!-- "Launch Podcast Sanity" checkbox on the final dialog of the stock
       WixUI_InstallDir UI that CPack generates. WixShellExec runs the file
       without elevation, which is right for a per-user install. -->
  <CPackWiXFragment Id="#PRODUCT">
    <Property Id="WixShellExecTarget" Value="[#CM_FP_bin.podcast_sanity.exe]" />
    <CustomAction Id="LaunchApplication" BinaryKey="WixCA" DllEntry="WixShellExec" Impersonate="yes" />
    <UI>
      <Publish Dialog="ExitDialog" Control="Finish" Event="DoAction" Value="LaunchApplication">
        WIXUI_EXITDIALOGOPTIONALCHECKBOX = 1 and NOT Installed
      </Publish>
    </UI>
  </CPackWiXFragment>
</CPackWiXPatch>
```

`CMakeLists.txt` WIX block:

```cmake
set(CPACK_WIX_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/wix_patch.xml")
set(CPACK_WIX_EXTENSIONS WixUIExtension WixUtilExtension)
set(CPACK_WIX_PROPERTY_WIXUI_EXITDIALOGOPTIONALCHECKBOXTEXT "Launch Podcast Sanity")
set(CPACK_WIX_PROPERTY_WIXUI_EXITDIALOGOPTIONALCHECKBOX "1")
```

(Replace the existing `if(EXISTS .../wix_patch.xml)` guard with the unconditional set; the file now exists.) The file id `CM_FP_bin.podcast_sanity.exe` is the one CPack already emits (see `files.wxs` in the last build log).

- [ ] **Step 3: Build and install interactively once**

Run `.\packaging\build-windows.ps1`, then double-click the MSI: the banner and the side panel show the studio artwork, the last page has the "Launch Podcast Sanity" checkbox ticked, Finish starts the app. Then `msiexec /x podcast-sanity-0.2.0-win64.msi /qn` to remove it (the automated test reinstalls).

- [ ] **Step 4: Commit**

```bash
git add packaging/generate-icon.py data/wix-banner.bmp data/wix-dialog.bmp packaging/wix_patch.xml CMakeLists.txt
git commit -m "Installer: studio artwork on the WiX dialogs and a launch checkbox"
```

### Task 6.3: Uninstall, upgrade and signing in the scripts

**Files:**
- Modify: `packaging/test-windows-msi.ps1`
- Modify: `packaging/build-windows.ps1`
- Modify: `.github/workflows/ci.yml` (Windows job only: pass `-Uninstall`)
- Modify: `packaging/README.md`

- [ ] **Step 1: Registry check, uninstall switch, upgrade test**

`test-windows-msi.ps1` gains `[switch]$Uninstall` and `[string]$PreviousMsi = ''`. After the GUI check:

```powershell
    $arp = Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' |
        ForEach-Object { Get-ItemProperty $_.PSPath } |
        Where-Object { $_.DisplayName -eq 'Podcast Sanity' }
    if (-not $arp) { throw 'No Apps & features entry named "Podcast Sanity"' }
    if (@($arp).Count -ne 1) { throw "Expected one Apps & features entry, found $(@($arp).Count)" }
    Write-Host "Apps & features: $($arp.DisplayName) $($arp.DisplayVersion) by $($arp.Publisher)"
    if ($arp.Publisher -match '@') { throw "Publisher must not contain an e-mail: $($arp.Publisher)" }
```

Before the install, when `$PreviousMsi` is given: install it first (`msiexec /i ... /qn`), then install `$Msi`; the single-entry assertion above proves the upgrade replaced it (same `CPACK_WIX_UPGRADE_GUID`). At the end, when `-Uninstall`: `Start-Process msiexec.exe -ArgumentList "/x `"$Msi`" /qn /norestart /L*v `"$LogDir\uninstall.log`"" -Wait` and assert `-not (Test-Path "$appDir\podcast-sanity.exe")` and that the ARP entry is gone.

- [ ] **Step 2: Signing hook**

`build-windows.ps1` gains `[string]$SignThumbprint = ''` and `[string]$TimestampUrl = 'http://timestamp.digicert.com'`. After the build and before ctest:

```powershell
function Sign-File([string]$Path) {
    if (-not $SignThumbprint) { return }
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $signtool) { throw 'signtool.exe not found: install the Windows SDK signing tools' }
    & $signtool.FullName sign /sha1 $SignThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $Path
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $Path" }
}
Sign-File "$BuildDir\Release\podcast-sanity.exe"
Sign-File "$BuildDir\Release\podcast-sanity-render.exe"
```

and after cpack: `Get-ChildItem "$BuildDir\packages\*.msi" | ForEach-Object { Sign-File $_.FullName }`. When no thumbprint is given print `Write-Warning 'Unsigned build: Windows SmartScreen will warn on first launch. Pass -SignThumbprint to sign.'`.

- [ ] **Step 3: CI and README**

In `ci.yml` Windows job: `./packaging/test-windows-msi.ps1 -Msi (...) -Uninstall`. In `packaging/README.md` Windows section: document `-SignThumbprint`, `-Uninstall`, `-PreviousMsi`, the launch checkbox, the trimmed payload, and the accepted ICE warnings with their reasons.

- [ ] **Step 4: Run the whole Windows release path**

```powershell
.\packaging\build-windows.ps1
.\packaging\test-windows-msi.ps1 -Msi .\build-windows\packages\podcast-sanity-0.2.0-win64.msi -PreviousMsi .\build-windows\packages-0.1.0\podcast-sanity-0.1.0-win64.msi -Uninstall
```

(Copy the 0.1.0 MSI produced before Task 6.1 to `build-windows\packages-0.1.0\` beforehand.) Expected: `MSI verified`, one Apps & features entry at 0.2.0, uninstall leaves no files.

- [ ] **Step 5: Commit**

```bash
git add packaging/test-windows-msi.ps1 packaging/build-windows.ps1 .github/workflows/ci.yml packaging/README.md
git commit -m "Packaging: uninstall and upgrade test, Apps & features check, signing hook"
```

---

## Phase 7: Documentation, release notes, pull request

### Task 7.1: Documentation sweep

**Files:**
- Modify: `README.md`, `docs/DESIGN-podcast-sanity.md`, `docs/TESTING.md`, `docs/STANDARDS.md`, `packaging/README.md`
- Create: `CHANGELOG.md`

- [ ] **Step 1: Make every statement true again**

Walk the four documents against the code after Phase 6 and fix: the test counts (copy from the final ctest output), the stage list (music_bed), the GUI description (three themes, Studio default, VU meters, ON AIR), the Windows section (0.2.0, signing, uninstall), the "Known limitations" list (keep "stereo imaging is not processed"; remove "cancellation ... only prevents the result from being adopted" since Task 2.7 made it true and precise; add "code signing requires a certificate the project does not have yet"). Add `CHANGELOG.md` with a `0.2.0` section listing the user-visible changes by phase and a `0.1.0` line.

- [ ] **Step 2: Commit**

```bash
git add README.md docs/DESIGN-podcast-sanity.md docs/TESTING.md docs/STANDARDS.md packaging/README.md CHANGELOG.md
git commit -m "Docs: 0.2.0 state of the engine, GUI, tests and packaging"
```

### Task 7.2: Pull request

- [ ] **Step 1: Final verification**

```powershell
.\packaging\build-windows.ps1
.\packaging\test-windows-msi.ps1 -Msi .\build-windows\packages\podcast-sanity-0.2.0-win64.msi -Uninstall
git status --short      # must be empty
```

- [ ] **Step 2: Push and open the pull request**

```bash
git push -u origin review-2026-10-16
gh pr create --base master --head review-2026-10-16 --title "Review fixes, distribution profiles, music bed, studio interface, complete MSI (0.2.0)" --body-file docs/superpowers/plans/pr-body-2026-10-16.md
```

Write `pr-body-2026-10-16.md` first: a table of finding IDs to commits, the ctest summary line, the ffmpeg cross-check number from Task 1.1, the MSI size before/after, and the "Decisions for the owner" list below with what was assumed. No AI attribution anywhere in the body. Do not merge: the Linux CI jobs are reviewed by the owner on the Ubuntu machine, and the PR waits for that.

---

## Decisions for the owner (assumptions the plan makes)

1. **Baseline commit (Task 0.1):** the uncommitted Windows port is committed to `master` as-is before branching. It builds and passes; the alternative is to carry it as the first commit of the branch.
2. **Default theme:** new installs start in "Studio"; existing settings keep their choice.
3. **Ubuntu font bundled** in the app under the Ubuntu Font Licence 1.0, consistent with the project's font rule for publications.
4. **Harness thresholds:** speech preservation `>= 5 dB` and SNR gain `>= +1.5 dB` (measured 5.86 and 1.76 at the worst length). The alternative is to tune the denoiser and the filler crossfade, which is DSP work this plan does not assign to an executor.
5. **Version 0.2.0** for this release.
6. **Code signing:** the hook is in place; a certificate (OV or EV, in the owner's name) is needed to use it. Until then the MSI is unsigned and SmartScreen warns on first launch.
7. **Stereo processing stays mono-sum** (known limitation): this plan makes the measurement and the target correct for dual-mono output; a true stereo or mid/side chain is a separate project.
8. **No `.wav` file association** in the MSI: taking over `.wav` from the system player would be hostile; "Open with" is available through the Start menu entry.
