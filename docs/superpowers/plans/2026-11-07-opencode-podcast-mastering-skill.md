# podcast-sanity 0.2: autonomous podcast mastering for OpenCode

Status: implementation plan, not implemented or validated.
Owner: Franco Geraci.
Prepared: 2026-11-07.
Inspected baseline: `master`, commit `2d34317` in `Voloire/podcast-sanity`.

## 1. Outcome and execution boundary

Build a new, headless way to use this application through an OpenCode V2 skill named
`podcast-master`. Given a local audio file, the skill autonomously analyzes it, chooses
appropriate restoration and leveling settings, masters it for podcast distribution, and
returns a real MP3 beside the original:

```text
/recordings/Episode 07.take2.wav
/recordings/Episode 07.take2_processed.mp3
```

The user's request is the authorization to process that file. Do not ask them to choose
denoising strengths, compressor settings, a preset, or an encoding bitrate. Ask only when
the input is missing/ambiguous, channel selection is ambiguous, or the destination already
exists. The original is never modified. Preserve speech, timing, pauses, and stereo content.

"Maximum cleanup" means the best measured cleanup that preserves the recording, not every
effect at its maximum value. Automatic choices are bounded, reproducible DSP decisions, not
an unsubstantiated claim that the language model listened to the file or found a global optimum.

Deliver the plan's implementation in this repository. Reuse `sanity_core`; do not replace it
with a prompt that improvises an FFmpeg command on every invocation. The skill and its runner
are the new user interface. The existing desktop app remains buildable; redesigning its GUI,
recording controls, or installer UI is outside this work. No cloud service, account, API key,
ASR, generative voice repair, publishing, or background daemon is needed.

Support Ubuntu x86-64 and Windows x64, including paths with spaces and Unicode. Local files
stay on the execution machine. AWS, SSH transfer, and processing on another host are not implicit.

## 2. Verified starting point and mandatory gaps

These observations come from source inspection, not a fresh test run:

| Location | Current behavior | Consequence for this release |
|---|---|---|
| `cli/render_main.cpp` | Reads with `wav_read`, writes with `wav_write` unconditionally | `--out x.mp3` currently creates WAV bytes. Correct format dispatch; test codec, not existence. |
| Same file, parser | Several unchecked `argv[++i]`; permissive numeric parsing | Reject missing, invalid, non-finite, and out-of-range values without crashes. |
| Same file, analysis | `--analyze-only` prints human text, ignores JSON intent | Add a versioned machine-readable analysis/capabilities contract. |
| `src/pipeline.cpp` | `to_mono(in)` then `mono_to_channels(...)` | Genuine stereo is lost; anti-phase material can disappear. New path must preserve channels. |
| `src/analyze.cpp` | `Analysis::channels` is always 1; `music_ratio` always 0 | Neither field supports autonomous stereo/music decisions today. |
| `src/pipeline.cpp` | Default denoise is off; Power Clean chooses 1.6; de-reverb is hard-coded at 0.6 | Power Clean is not an adaptive optimizer. Expose actual effective parameters and select them from evidence. |
| Same file | Resampling follows normalization; conformance concerns in-memory mono | Delivery validation must measure the final decoded MP3 with correct channel weighting. |
| `src/encode.cpp` | LAME exists but stereo pointers are taken from an interleaved buffer as if planar | Fix/deinterleave before using that encoder; independent decode must catch channel corruption. |
| Same file | Unsupported MP3 sample-rate fallback folds to mono and duplicates | Resample per channel explicitly before encoding; never use that fallback in the skill path. |
| `CMakeLists.txt` | Codecs optional; core version can fall back to `0.1.0`; Qt optional | Skill capability checks must fail if requirements are absent; propagate one version to all targets. |
| `.github/workflows/ci.yml` | Windows codec smoke test checks only file existence | Replace with real codec/channel/duration assertions using ffprobe and full decode. |
| `docs/STANDARDS.md` | Attributes -19 mono to Apple and claims pre-encode compliance is sufficient | Update the claims and distinguish delivery profiles from measurement standards. |

The earlier `2026-10-16-review-fixes-and-studio-release.md` is a historical plan, not proof its
fixes landed. Recheck its F02/F03/F04/F13/F16/F23 findings in the actual baseline: corpus bounds,
thread-captured atomic lifetime, limiter ordering, CLI parsing, empty resampling, and WAV
allocation/validation are directly relevant. Fix reproducible blockers in the paths used here.
Do not execute that entire plan or assume its machine/toolchain state is current.

## 3. User-visible contract

### Files and formats

- Accept local WAV, MP3, FLAC, M4A/AAC, Ogg/Vorbis, and Opus when the installed decoder supports
  them. Probe actual content. Reject corrupt, empty, non-finite PCM and unsupported inputs clearly.
- One mono or stereo audio stream is automatic. Multiple audio streams or more than two channels
  require explicit selection/mapping rather than an arbitrary downmix. Ignore attached cover art.
- Keep mono mono and stereo stereo, including dual-mono. Do not infer permission to downmix.
- Keep 44.1/48 kHz; convert other rates to 48 kHz once before mastering, preserving channels.
- Output filename is `input.parent / (input.stem + "_processed.mp3")`, removing only the final
  extension. Preserve spelling, spaces, Unicode, and internal dots. `take_processed.wav` becomes
  `take_processed_processed.mp3`; do not guess that it was already processed.
- Existing destination: fail with a collision result and let the skill ask before `--overwrite`.
  Do not silently invent a number or overwrite. Guard against source/destination aliasing.
- Keep all intermediate audio in a per-run work directory. Publish the MP3 only after validation,
  using an atomic same-filesystem operation with a no-clobber default. Check source checksum
  before/after. On interruption, do not leave a partial `_processed.mp3` or remove the user's files.
- Return one MP3 as the user deliverable. Store the JSON diagnostic report in the application cache,
  not as an unsolicited sibling file; support an explicit report export for debugging.

### Delivery profile

Use named profile `podcast` for the new skill:

| Property | Decision |
|---|---|
| Integrated loudness | -16 LUFS, tolerance +/-1 LU, on the decoded final file, mono or stereo |
| True peak | At most -1.0 dBTP on the decoded final file, every channel |
| MP3 | LAME CBR, 128 kbps mono / 256 kbps stereo, high-quality encoding |
| PCM working representation | Floating point; no lossy intermediate encoding |
| LRA | Diagnostic and leveling input, not a falsely universal podcast compliance limit |
| Content editing | Filler removal, silence trimming, word removal, and time stretching off |

Rationale: Apple's current audio guidance recommends around -16 LKFS +/-1 and a -1 dB true-peak
ceiling, with mono MP3 up to 128 kbps and stereo up to 256 kbps. BS.1770 specifies measurement,
not a universal podcast distribution target. The existing -19 mono convention may remain as an
explicit `speech-mono` delivery profile and as legacy CLI behavior, but is not to be described as
Apple's current stated mono requirement. Do not promise a host will never re-encode the file.

Use -2 dBTP as the initial pre-encode working ceiling for codec headroom; the final -1 dBTP
requirement is checked after encoding. Do not silently relax either loudness tolerance or the
final peak ceiling when a difficult recording cannot satisfy both.

### Completion response

The OpenCode response is short: link or absolute path to the output, duration, measured loudness
and true peak, and one sentence on the main automatic choices. Surface a meaningful residual
problem such as unrecoverable clipping. Do not dump the full report. Never report success based
only on a subprocess exiting zero or a file existing.

## 4. Architecture and proposed interfaces

All interfaces below are new work, not commands already supported by the baseline.

```text
OpenCode -> SKILL.md -> Python runner
                         |
                         +-- ffprobe: input metadata and actual codecs
                         +-- ffmpeg: local decode/resample to float WAV
                         +-- C++ renderer: measurements, candidate selection, restoration,
                         |                shared-channel leveling, cleaned lossless master
                         +-- ffmpeg: two-pass loudness/true-peak finishing and LAME MP3 encoding
                         +-- independent decode + final loudness/peak/structure validation
                         +-- atomic publish + versioned JSON result
```

Use Python 3.10+ standard library for orchestration; use subprocess argument arrays, never
shell-interpolated filenames. FFmpeg/ffprobe are explicit runtime dependencies. They provide
file decoding, channel-aware final normalization, MP3 encoding and delivery measurements.
Keep restoration policy and DSP in the C++ engine, reusable outside OpenCode. The finalizer uses
`loudnorm` two-pass measurements and checks whether linear or dynamic mode was actually used;
set the output rate explicitly, and do not hide dynamic limiting from the report.

Suggested repository files:

```text
skills/podcast-master/SKILL.md
skills/podcast-master/scripts/process_audio.py
skills/podcast-master/references/processing-policy.md
skills/podcast-master/references/report-schema.json
skills/podcast-master/evals/                         # prompt cases and expected outcomes
tools/install_opencode_skill.py
include/sanity/auto_master.hpp
src/auto_master.cpp
tests/auto_master_tests.cpp
tests/test_skill_runner.py
```

Extend shared modules only where required: `cli/render_main.cpp`, `master.hpp`, `pipeline.cpp`,
`analyze.cpp`, cleaning/dynamics/I/O/encoding helpers, `CMakeLists.txt`, packaging and CI docs.
Avoid two divergent implementations of the same cleaning stage.

Renderer interfaces:

```text
podcast-sanity-render --capabilities --print-json
podcast-sanity-render input.wav --analyze-only --print-json
podcast-sanity-render input.wav --auto-clean --out cleaned.wav --report engine.json --quiet
```

`--auto-clean` produces channel-preserving floating-point WAV and an effective-parameters report.
It applies restoration and leveling but delegates delivery normalization to the runner's finalizer.
The legacy manual rendering path must still dispatch `.mp3` to an actual encoder; reject unknown
extensions instead of writing mislabeled WAV. Capability output identifies version, schema,
auto-clean/stereo support and available codecs so an old installed binary cannot be used silently.

Runner interface:

```text
python <skill-dir>/scripts/process_audio.py --input <absolute-local-file> --json
python <skill-dir>/scripts/process_audio.py --doctor --json
```

Optional advanced arguments: `--renderer`, `--ffmpeg`, `--ffprobe`, `--overwrite`, `--report`,
and `--profile`. Defaults implement the complete user request. Resolve runtime paths in this
order: explicit argument, installed skill runtime config, PATH. Never hard-code a developer's
checkout, user name, current directory, or Windows drive into the skill.

Runner exit codes: 0 verified success, 1 I/O/decode/engine failure, 2 invalid request,
3 delivery/quality gate failure, 4 missing/incompatible dependency, 5 destination collision,
130 cancellation. Stdout is exactly one JSON result in JSON mode; progress and diagnostics go to
stderr. Preserve engine failure details without accidentally promoting its partial output.

## 5. Autonomous parameter selection

### Analysis and representative material

Compute speech activity, quiet-region noise estimates and confidence, hum evidence, DC, clipped
sample runs, sibilance, reverb estimates and active-speech level spread per channel. Use shared
frame timing for stereo, without using an L+R sum as the only detector signal. Do not treat the
current `music_ratio = 0` as a classifier. Ambiguous music/continuous content reduces restoration
aggressiveness; do not claim a measured RT60 when the decay estimate lacks evidence.

Analyze the entire file for problems at different times. For candidate comparison choose at most
eight deterministic 8-second excerpts with processing context, covering start/middle/end, the
noisiest quiet region, low-level speech, loud speech, transients and any reliably detected reverb.
Deduplicate overlaps and retain one common mask of speech/quiet frames for all comparisons.
Use the full source when shorter than the excerpt budget. Report excerpt timestamps and coverage.

### Bounded candidates

Compare at most four profiles on those excerpts, sequentially:

| Candidate | Initial denoise strength | Maximum initial de-reverb strength | Purpose |
|---|---|---|---|
| preservation | 0 | 0 | Repair/leveling reference with no spectral suppression |
| gentle | 0.4 | 0.2 | Clean recordings or uncertain noise estimates |
| balanced | 0.8 | 0.4 | Typical room/background noise |
| strong | 1.2 | 0.6 | Strong evidence of contamination with preservation gates satisfied |

These are starting calibration values, not measured promises. Gate de-reverb on reliable room
decay evidence. Apply hum removal only to detected mains components; bound high-pass below the
detected voice fundamental. Select de-essing and leveler strengths from the same measured
evidence, exposing effective values instead of the current hard-coded constants. All optional
stages may be disabled on clean audio. Filler processing stays off for every candidate.

Rank candidates before delivery normalization, at matched active-speech gain. Lower absolute
noise floor alone is not improvement: attenuation of the whole signal must score no better.

Implement and document a deterministic, calibrated decision table with this ordering:

1. Reject candidates with non-finite output, timing/channel changes, newly clipped samples,
   unexplained loss of active speech, excessive speech-band alteration or stereo imbalance.
2. Measure reduction in the original quiet-region masks and residual hum. Include evidence
   coverage; do not invent SNR/SI-SDR for arbitrary input without a clean reference.
3. Compare speech-envelope and speech-band preservation after gain alignment; penalize musical
   noise and over-suppression using explicitly documented proxies. Proxies are not perceptual proof.
4. Select the greatest useful reduction that passes preservation gates. Improvements below
   1 dB in the matched quiet-region metric are ties; choose the weaker candidate on ties.
5. With insufficient quiet/speech evidence choose preservation or gentle, record low confidence,
   and continue automatically. Never turn a failed noise estimate into maximum denoise.

Calibrate concrete preservation thresholds on synthetic references plus real spoken recordings
before freezing policy version 1. Record thresholds, metrics, rejection reasons, and selected
parameters in `processing-policy.md` and tests. Do not tune thresholds solely on one synthetic voice.

Use one selected profile for the full recording; the denoiser may retain its existing regional
noise tracking. Render the complete file once, recheck preservation, and allow at most one
full-length fallback to a weaker profile from the original PCM. Do not cascade candidates or
repeatedly denoise already processed audio. Keep candidate optimization independent of LLM choice.

### Stereo and dynamics

Refactor cleaning so both channels remain distinct with common frame boundaries and a common
chosen policy. Per-channel noise estimates may differ. Time-varying leveler/compressor gains
must be stereo-linked, avoiding one voice pulling the image from side to side. Keep common
delay/length and preserve negative correlation. Measure loudness as channel-energy summation,
not loudness of the arithmetic average. A two-channel count is not proof of stereo preservation.

Do not promise to reconstruct clipped words. Existing click interpolation and limiting are not
a full declipper. Report clipping damage and keep repair conservative unless a separately tested
declipping implementation demonstrates an improvement against a clean reference.

## 6. Finalization, validation and resource limits

1. Normalize the selected lossless master with channel-aware FFmpeg two-pass `loudnorm` to
   the chosen profile, initially using -2 dBTP PCM headroom. Specify a conservative LRA setting
   for the filter's operation, record it, and do not label it an external podcast requirement.
2. Encode a temporary MP3 with `libmp3lame`, CBR 128/256 as appropriate. Require the encoder
   during `--doctor`; no WAV fallback and no alternative codec disguised as MP3.
3. Probe actual MP3 format/codec, channel count, sample rate and bitrate. Decode the entire file
   with errors fatal. Independently meter decoded audio using FFmpeg `ebur128=peak=true`.
4. Verify final LUFS within target +/-1 LU, true peak <= -1.0 dBTP, finite decoded samples,
   no truncation, and duration preserved within 100 ms after accounting for encoder delay/padding.
   Silence or a clip too short to yield valid integrated loudness gets a truthful failure result,
   not an invented compliant number. Undefined measurements serialize as null with reasons.
5. If codec overshoot breaks the peak gate, lower the PCM ceiling by measured excess plus a
   0.2 dB margin, normalize again from the retained lossless master, and re-encode. Allow at most
   three encode attempts total. Never transcode an MP3 attempt into another attempt.
6. If loudness and peak cannot both pass, exit 3 with diagnostics, without publishing a success
   MP3. Never silently deliver an out-of-spec final file under the expected final name.
7. Atomically publish only the validated candidate and finalize the report.

Preflight disk space and decoded size before allocation. Process candidates sequentially and
release their audio before the next. Do not retain four full-length copies. Establish an explicit
working-memory budget with measured allocation factors, fail early with a resource error above
the tested bound, and catch allocation failures. A 60-minute, 48 kHz stereo episode is a required
release test: target <=4 GiB peak RSS on an 8 GiB machine. If this cannot be achieved, refactor
buffer ownership/I/O before release rather than silently shortening the audio. Benchmark elapsed
time and real-time factor; publish measurements rather than a speculative speed promise.

Use a per-run directory under the OS application cache and a final staging file beside the output.
Clean temporary audio on success/cancellation/error, retaining the compact report and diagnostics.
Use a per-output lock and recheck destination existence at atomic publication. Concurrent jobs for
the same destination must not race past the no-overwrite rule. Handle Windows locks and subprocess
termination as well as POSIX signals. No unbounded retries, loops, or autonomous tool installation.

## 7. Report and skill installation

The versioned report records:

- Input/output resolved paths, SHA-256, original codec, stream index, channels, sample rate,
  decoded duration; renderer/FFmpeg/runner versions and policy/schema versions.
- Before analysis, evidence confidence and sample windows; every candidate's effective settings,
  metrics and rejection reasons; the chosen settings and any full-file fallback.
- Cleaning/leveling stages and durations; source PCM, lossless master, and decoded delivery
  measurements as separate objects. Keep engine diagnostics distinct from delivery conformance.
- Every encode attempt, codec settings and measurements; final pass/fail and unavailable metrics.
- Warnings, output/report paths, elapsed time and resource measurements where supported.

`SKILL.md` must be concise, with English frontmatter/body and a description covering audio cleanup,
noise removal, automatic podcast mastering, loudness normalization and `_processed.mp3` requests.
Explain the one runner command and success checks. Do not embed credentials or machine-specific
paths. Do not let the language model substitute a one-line filter command, skip verification,
claim to listen without audio tooling, or choose arbitrary new profiles per session.

Keep the canonical skill at `skills/podcast-master/`. Install a complete copy with its scripts
and references into the user's OpenCode global skills directory, normally
`~/.config/opencode/skills/podcast-master/` (honor an explicitly configured global config root).
This makes it usable from any project. Use only OpenCode locations, not Claude/Codex skill trees.
The installer takes an explicit renderer location or finds a compatible installed runtime, writes
a runtime config with resolved dependencies, and supports check/dry-run/update/uninstall scoped
to its own files. Back up an existing installation before replacing it and report modified files.
Do not require changes to `opencode.json` when standard discovery suffices.

Ship a versioned headless runtime bundle for Linux/Windows and the skill source bundle. A working
OpenCode installation plus Python and the documented FFmpeg runtime is enough; Qt and a C++
compiler must not be runtime requirements. Test Windows codec DLL deployment without the developer
toolchain on PATH. Keep the existing GUI MSI/.deb packaging working, but do not silently install
a skill into another tool or install the entire desktop app just to process an audio file.

## 8. Ordered implementation and gates

The executor should mark these checkboxes and record evidence paths as work proceeds.

- [ ] **0. Baseline.** Read this plan, actual code and current repo instructions. Fetch origin,
  check for newer changes and existing worktrees. Use a dedicated `feat/opencode-podcast-master`
  worktree from the current shared branch, preserving other work. Inventory tool versions.
  Run a fresh headless Release configure/build/ctest and record existing failures separately.
  Reproduce relevant historical memory/CLI defects under ASan/UBSan; do not trust old build trees.
- [ ] **1. Correct prerequisites.** Fix the confirmed corpus/thread/parser/empty-input blockers,
  correct output codec dispatch and stereo encoder layout, and add meaningful regression tests.
  Ensure version propagation and reliable dependency detection. Gate: tests reproduce old faults
  and pass after fixes; fake-MP3 content fails the codec smoke test.
- [ ] **2. Analysis and preservation.** Add JSON analysis/capabilities, channel-aware measurements,
  stereo-preserving cleanup and linked dynamics. Keep manual behavior compatible where valid;
  document intentional fixes to incorrect output. Gate: independent channel/loudness tests pass,
  including anti-phase stereo, one-sided speech and unequal speakers.
- [ ] **3. Automatic policy.** Implement bounded excerpt selection, candidate comparison, effective
  parameter reporting and one weaker full-file fallback. Extend existing primitives where they
  have hard-coded strengths. Gate: selection is deterministic, input-dependent, calibrated and
  preserves clean content; record which quality claims use references versus proxies.
- [ ] **4. Runner.** Implement local probe/decode, `--doctor`, resource preflight, two-pass delivery,
  MP3 round-trip validation, bounded re-encoding, atomic publication and structured failures.
  Gate: format/path/collision/cancellation/quality tests pass on Linux and Windows.
- [ ] **5. Skill and installer.** Write the skill, references, report schema and portable installer.
  Test installed discovery and processing from an unrelated current directory. Gate: one request
  produces the correctly named file without technical-setting questions or hidden global edits.
- [ ] **6. CI and release.** Bump to 0.2.0; extend CI with headless Linux/Windows runner tests,
  independent MP3 decode, sanitizer regressions and skill/runtime artifacts. Keep desktop smoke
  tests green. Add a scheduled/manual long-file resource test with a documented command. Update
  README, STANDARDS, TESTING and packaging docs to match implemented behavior and actual test counts.
- [ ] **7. Handoff.** Review the diff against this scope, record final tests, representative audio
  results and any limitations. Hand over the implementation through the repository's required PR
  workflow; never overwrite shared-branch work. Installing on the user's machine is an explicit
  deployment step, not a side effect of running unit tests.

Recommended fresh headless baseline (Linux):

```sh
cmake -S . -B build-skill -G Ninja -DCMAKE_BUILD_TYPE=Release -DSANITY_WITH_GUI=OFF
cmake --build build-skill --parallel
ctest --test-dir build-skill --output-on-failure
```

Extend CTest to run the new runner tests against the just-built executable and pinned CI tools.
Verify with GUI enabled once before release because shared core changes affect it. On Windows,
use the documented supported generator/toolchain and a separate build directory; do not assume
the historical capybara paths still exist. This plan requires both OSes, unlike the older plan.

## 9. Acceptance tests

| Case | Required evidence |
|---|---|
| Clean spoken mono | Little/no spectral suppression selected, correct name and real MP3, final targets pass. |
| Noisy speech with quiet gaps | More restoration than the clean case, >=2 dB SNR improvement on calibrated home/rough synthetic references, gain-aligned speech preservation. |
| Clean reference preservation | No more than 0.5 dB aligned SI-SDR degradation against the preservation candidate; evaluated before lossy encoding. |
| Low-level speech + loud transient | Leveling chosen for evidence, final loudness and peak both pass, no extra clipping. |
| 50/60 Hz hum and reverberation | Relevant stage enabled only on evidence; residual metric improves; clean counterpart is not unnecessarily treated. |
| Speech with music/continuous sound | Low-confidence noise estimate does not select strong suppression; no automatic content cuts. |
| Distinct L/R, anti-phase, one-sided voice | No mono collapse/cancellation, timing aligned, shared dynamics, actual decoded L/R content verified. |
| WAV/MP3/FLAC/M4A/Ogg/Opus | Supported inputs decode locally; same basename rule; no mislabeled WAV or repeated lossy intermediate. |
| 44.1/48/96 kHz and narrow-band sources | Output 44.1/48 kHz as specified, channels preserved, no fabricated lost bandwidth. |
| Multiple dots, Unicode, spaces, leading dash | Correct basename and safe argv handling on both platforms, from an unrelated cwd. |
| Existing output, symlink alias, concurrent jobs | Collision is explicit; original and previous output hashes are unchanged. |
| Zero bytes, silence, corrupt stream, very short input | Clear structured failure or unavailable metrics; no invented compliance or final MP3 on failure. |
| Missing renderer/FFmpeg/LAME, old binary | Actionable dependency error before processing, no implicit downloads or WAV fallback. |
| Disk full, memory bound, cancellation | No successful-looking partial final file, no lost source, bounded cleanup and retry. |
| Codec peak overshoot | Final decoded peak enforced; retry count <=3; every retry derives from lossless master. |
| Determinism | Same versions/input produce same selected policy and PCM; timing fields excluded. Verify MP3 repeatability within a pinned encoder build. |
| 60-minute stereo | Entire file preserved, resource budget met, elapsed time and peak RSS documented. |
| Real speech samples | Licensed/authorized recordings with soft consonants, low voices, sibilance, breaths, and music; level-matched comparisons, not synthetic results alone. |

SI-SDR/SNR gates require a clean reference and alignment; they are not runtime promises on arbitrary
user recordings. Freeze test fixtures/seeds and policy thresholds together, recording any justified
calibration change. Do not weaken tests merely to obtain green CI. A listening check is useful
release evidence; no unsupported claim of a human-quality listening judgment from text-only tools.
If real recordings or listening tooling are unavailable, state that gap explicitly.

Skill interaction cases should include:

1. "Clean this podcast recording as much as possible and normalize the volume: /recordings/episode.wav."
2. "Prepare C:\\Audio\\Episode 07.take2.flac for podcast distribution; choose the settings yourself."
3. "Make the noise reduction stronger on this already clean interview, but keep the voice natural."
4. "Change my default microphone." This must not trigger file mastering.
5. A repeated request whose `_processed.mp3` already exists: ask only about replacement.

Run end-to-end skill evaluations in isolated output directories with and without the skill,
checking filename, input preservation, actual codec, final measured conformance, and absence of
unnecessary parameter questions. Keep execution transcripts and measured results. Evaluate the
skill only after its deterministic runner is tested; prompt wording cannot repair DSP failures.

## 10. Executor handoff prompt

> Implement `docs/superpowers/plans/2026-11-07-opencode-podcast-mastering-skill.md` in
> `Voloire/podcast-sanity`. This is the active scope. Start with the inspected baseline checks;
> historical plans are context, not completed work or instructions to execute unrelated phases.
> Deliver OpenCode skill `podcast-master` with deterministic autonomous parameter selection and
> a verified `<input-stem>_processed.mp3` beside the untouched source. Follow the ordered gates,
> keep the shared checkout intact, and record test evidence and unresolved gaps in this plan.
> All repository artifacts are US English. Do not claim quality, stereo preservation, or encoded
> conformance from filenames or pre-encode measurements. Stop short of success if the actual
> MP3 fails its gates. No cloud processing and no user questionnaire about DSP settings.

## Sources checked for this plan

- Current repository files named above, at `2d34317`.
- OpenCode V2 skills: https://opencode.ai/v2/docs/skills (checked 2026-11-07).
- Apple Podcasts audio requirements: https://podcasters.apple.com/support/893-audio-requirements
  (checked 2026-11-07; currently references ITU-R BS.1770-5). Verify metering-library capabilities
  and document the actually implemented revision rather than renaming a BS.1770-4 meter.
