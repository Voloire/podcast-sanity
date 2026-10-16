# Testing podcast-sanity

Three suites, all driven by `ctest`, all autonomous — no human, no hardware and
no golden files that need regenerating by hand.

```
cd build && ctest --output-on-failure
```

| Suite | Target | Size | Time | What it protects |
|-------|--------|------|------|------------------|
| `unit` | `sanity_tests` | 22 cases, 127 checks | ~0.6 s | The DSP primitives |
| `e2e` | `sanity_e2e_tests` | 8 cases, 42 checks | ~40 s | The whole chain against ground truth |
| `gui` | `podcast-sanity --selftest` | walks 7 pages + master + export | ~5 s | The interface wiring |

---

## 1. Unit tests (`tests/test_main.cpp`, `tests/tinytest.hpp`)

A ~60-line header-only harness, so the tests have no framework dependency.

Covered:

* **K-weighting coefficients** at 48 kHz against the BS.1770-4 tabulated values,
  to 1e-9, plus the analytic derivation at other rates.
* **Loudness** against `libebur128` when it is present (the build reports
  whether the cross-check is active), on tones, noise and silence.
* **True peak**: the deterministic 12 kHz / 45° case (samples ±0.6364, true peak
  ≈ −0.915 dBFS) fixes the oversampled peak detector to a number.
* **Filter bank**: RBJ biquad responses at the corner and in the stop band.
* **FFT**: forward/inverse round trip, Parseval, impulse and sine bin placement.
* **STFT**: analysis/synthesis is unity-gain with the WOLA Hann at 75 % overlap.
* **Resampler**: passband flatness and image rejection for 44.1↔48.
* **Segmenter**: frame hop contract `segmenter_hop(rate) = max(1, rate/100)`, so
  features are always 10 ms apart regardless of rate.
* **Corpus generator**: the envelope, the isolated hesitations, and
  `Degradations::filler_every == 0` meaning "never insert a hesitation".
* **Containers**: WAV round trip for 16-bit, 24-bit and float, and the JSON
  writer's escaping and number formatting.

---

## 2. End-to-end harness (`tests/e2e_main.cpp`)

This is the primary deliverable of the "observability first" requirement.

### 2.1 What it does

For each profile it generates a synthetic episode with `make_corpus_talk()` —
a structured talk with an envelope, pauses and known hesitations — applies a
known degradation, runs `master()`, and then *judges the result against the
ground truth the generator kept*. Nothing is compared against a stored file; the
oracle is the generator, so the test cannot rot.

Profiles: `studio` (clean, a little noise), `home` (noise, hum, room), `rough`
(low level, heavy noise, hum, clipping, 12 injected ±0.95 clicks, isolated
hesitations).

### 2.2 The checks

| Check | Threshold | Rationale |
|-------|-----------|-----------|
| Loudness | within ±1 LU of −19 (mono) | The delivery tolerance; the engine actually lands within 0.05 LU |
| True peak | ≤ −1.0 dBTP | Measured on the written output, not on an internal buffer |
| Filler recall | ≥ 0.75 | Ground-truth hesitations must be found |
| Filler precision | ≥ 0.75 | Words must not be cut |
| Removed filler time | ≤ truth × 1.6 + 0.2 s | Catches over-eager removal that still scores well by interval overlap |
| No false positives | 0 on a no-hesitation corpus | The corpus is regenerated with `filler_every = 0` |
| SNR improvement | ≥ +2 dB on home and rough | Cleaning must actually clean |
| SI-SDR (home) | no worse than the input | The denoiser must not add artefacts |
| SI-SDR (rough) | ≥ −1.5 dB | Allows for the intentional EQ |
| Determinism | byte-identical audio and equal `params_hash` | Same input and parameters, same bytes, every time |
| Report completeness | every enabled stage present, with metrics | The observability contract |
| Music preset | runs and stays inside the tolerance | A second chain, not just talk |

### 2.3 Two measurement details worth remembering

**SI-SDR is measured with the adaptive high pass disabled, and with gain and
delay aligned.** The high pass is an *intentional* EQ against a full-band
reference, so leaving it on would count a deliberate, correct decision as
distortion. `aligned_si_sdr_db()` does a coarse decimated lag search and then
refines at full rate, so a sub-sample alignment error cannot masquerade as a
quality loss.

**Speech preservation is only measured on filler-off runs**, because removing a
hesitation shifts the time axis and every sample-index metric with it.

### 2.4 Running it

```
sanity_e2e_tests --scenario home --seconds 30 --json /tmp/report.json
```

`--seconds` changes the material length without changing what is checked, which
is how the thresholds were calibrated at 20 s, 30 s and 45 s.

---

## 3. How the Filler Killer thresholds were calibrated

The thresholds are not guesses. `Segmenter` features were dumped for every
candidate on the corpus at three lengths, each candidate was scored, and the
distribution was inspected:

* True hesitations score **0.65 – 0.92**.
* Look-alikes (short words, a soft consonant cluster, a brief breath) score
  **≤ 0.64**.
* At 30 s in the `rough` profile exactly one look-alike reaches 0.652, which is
  why `Prudente` sits at **0.64**: recall stays at 1.0 and precision is 1.0 in
  the common case and 0.8 in the worst measured case, which the ≥ 0.75 gate
  accepts while still failing anything genuinely reckless.

Balanced (0.54) and Aggressive (0.42) trade precision for tighter editing and are
opt-in.

---

## 4. The GUI self test

On Windows, `ctest -C Release` runs the native Windows platform plugin. The build
deploys Qt beside the executable automatically. The Windows codec test also
round-trips Unicode filenames and checks stereo channel separation in WAV,
FLAC, Ogg and MP3 (when the codecs are present).

`packaging/test-windows-msi.ps1 -Msi <path>` tests the actual installed MSI with
SDK paths removed, saving GUI screenshots and logs. This leaves the per-user
installation available from the desktop and Start menu. See
[packaging](../packaging/README.md#windows--msi).

```
xvfb-run -a --server-args="-screen 0 1400x900x24" \
  ./podcast-sanity --selftest --selftest-seconds 12
```

It:

1. selects all seven pages in turn, forcing each to construct and lay out — a
   null widget or a broken signal connection crashes here rather than in front
   of a user;
2. generates a synthetic episode if no audio was passed on the command line;
3. starts mastering through the *same* `AppState` path the button uses, on the
   worker thread;
4. checks the report contract (stages present, conformant, true peak on the
   ceiling) and exits non-zero if it is violated;
5. exports an MP3 with the recommended bitrate and fails if the file is missing
   or implausibly small;
6. prints a one-line summary and exits.

`ctest -R gui` runs it under `xvfb-run` when available, and under
`QT_QPA_PLATFORM=offscreen` otherwise, so it works in a container.

Adding `--selftest-shots DIR` captures every page with `QWidget::grab()` as a PNG
— once during the walk and again for the pages that only have content after a
master exists (Standards, Export, Diagnostics). That is the visual-regression
check: it is how the double-escaped `\u25CF`/`\n` sequences in the button labels
and the brand wordmark were found, and it needs no X11 tooling:

```
./podcast-sanity --selftest --selftest-shots /tmp/shots
```

The self test is where a real bug was caught: `subtitle_` was connected in a
lambda but never constructed, and the crash only appeared once audio was loaded
through the same path the user takes.

---

## 5. Debug tooling

The throwaway programs used during development are described here so they can be
rebuilt: `/tmp/dbgchain.cpp` (per-stage SI-SDR), `/tmp/dbgfill3.cpp` (filler
detection after each preprocessing step), `/tmp/dbgthr.cpp` (score separation
sweep at 20/30/45 s), `/tmp/dbgclick.cpp`, `/tmp/prof.cpp`, `/tmp/dbglevel.cpp`,
`/tmp/dbgfeat.cpp`. They link `build/libsanity_core.a` and are not part of the
repository.

ASan/UBSan build:

```
cmake -S . -B build-dbg -G Ninja -DSANITY_WITH_GUI=OFF \
      -DCMAKE_BUILD_TYPE=Debug -DSANITY_SANITIZE=ON
```

---

## 6. Reproducing a result from the CLI

The CLI is the same engine, so anything the GUI can do can be scripted:

```
# Regenerate the corpus with ground truth (needed after any corpus.cpp change)
podcast-sanity-render --gen-corpus /tmp/c.wav --profile rough --seconds 30 \
                      --truth /tmp/c-truth.json

# Master it and write both the audio and the observability report
podcast-sanity-render /tmp/c.wav --out /tmp/c-master.wav \
                      --power-clean --filler cautious \
                      --summary --report /tmp/c-report.json

# Measure without touching the audio
podcast-sanity-render /tmp/c.wav --analyze-only
```

The renderer exits **3** when the result is not conformant, so a CI job can gate
a release on the standard being met.
