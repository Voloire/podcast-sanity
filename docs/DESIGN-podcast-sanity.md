# podcast-sanity — design

A desktop application that records a podcast and masters it locally, offline, on
the machine. There is no upload, no cloud model and no telemetry. It reproduces
the parts of a commercial "magic mastering" service that matter for spoken word:
automatic cleanup, reverb reduction, filler removal without transcription,
adaptive levelling and loudness / true-peak conformance to a delivery standard.

* Language: C++20, no external DSP libraries. Every algorithm is in this tree.
* GUI: Qt6 Widgets with Studio (charcoal/amber, default), Light and Dark themes,
  bundled Ubuntu fonts and painted studio meters and controls.
* Targets: Windows (MSI) and Linux (.deb). The same engine drives the CLI and
  the automated test harness.

---

## 1. Repository layout

```
include/sanity/      public headers (the engine API)
src/                 engine implementation
cli/                 podcast-sanity-render, the headless renderer
gui/                 Qt6 desktop application
  pages/             the seven pages plus the page base class
  theme.{h,cpp}      design tokens + generated QSS
  widgets.{h,cpp}    Card, Chip, StatTile, LevelMeter, WaveformView, NavButton
  studio_widgets.*   VuMeter, OnAirSign and LedIndicator
  app_state.{h,cpp}  shared session state + background mastering worker
  audio_capture.*    microphone / file / corpus recording sources
tests/               unit tests and the end-to-end harness
data/                icon, .desktop entry, AppStream metainfo
packaging/           Wix patch, packaging notes
docs/                this file, TESTING.md, STANDARDS.md
```

Nothing in `src/` includes Qt. The engine is a plain static library
(`sanity_core`) with no UI dependency, which is what lets the CLI, the unit
tests, the e2e harness and the GUI all exercise exactly the same code.

---

## 2. The engine

### 2.1 Signal path

Everything is processed as a **mono sum**. Multi-channel input is folded to mono
with `to_mono()`, the chain runs once, and the result is duplicated to the
requested output channel count (`mono_to_channels()`). This is a deliberate
trade-off: podcast speech is effectively mono, and processing one channel keeps
the leveler and limiter envelopes identical across channels, which is what
prevents the stereo image from wobbling. **Stereo imaging is not processed** —
see §7.

Stage order in `master()` (`src/pipeline.cpp`):

| # | Stage | Purpose |
|---|-------|---------|
| 1 | `analyze_in` | BS.1770-4 loudness, true peak, noise floor, hum, RT60, speech/music/silence ratios |
| 2 | `repair` | DC removal, click/pop repair, clipping repair |
| 3 | `dehum` | mains hum notch (50/60 Hz + harmonics) |
| 4 | `highpass` | adaptive high pass at the voice fundamental |
| 5 | `filler` | acoustic Filler Killer |
| 6 | `denoise` | region-based spectral denoiser |
| 7 | `dereverb` | spectral reverb suppression (Power Clean) |
| 8 | `deess` | sibilance control |
| 9 | `leveler` | Smart Adaptive Leveling |
| 10 | `compressor` | gentle peak control |
| 11 | `music_bed` | speech-driven ducking, loop, fades and tail |
| 12 | `output` | resample before normalisation; select output channels |
| 13 | `normalize` | iterating loudness + true peak normalisation |
| 14 | `analyze_out` | measure the delivered channels and build the conformance verdict |

The order matters. Repair comes first because a click is broadband and would
otherwise be smeared by every later stage. Filler removal comes before the
denoiser so that the denoiser never spends effort modelling a region that is
about to disappear, and *after* the high pass so the silence detector is not
confused by rumble. The limiter is last so nothing downstream can put the true
peak back over the ceiling.

### 2.2 DSP building blocks (`src/dsp.cpp`, `src/loudness.cpp`)

* **RBJ biquads** for shelf, peaking, notch and high-pass filters, with
  coefficient cases that hold up at 44.1 and 48 kHz.
* **Radix-2 iterative FFT** with precomputed twiddles.
* **WOLA STFT** with a Hann analysis window at 75 % overlap and proper overlap
  add, used by the denoiser, the de-reverb and the de-esser.
* **Polyphase sinc resampler** for the occasional 44.1↔48 conversion (and for
  MP3 encoding when the source rate is outside LAME's set).
* **K-weighting** derived analytically from the BS.1770-4 stage definitions
  rather than hard-coded for 48 kHz, so any sample rate is correct; the unit
  tests check the 48 kHz coefficients against the tabulated values to 1e-9.
* **True peak** with 4× oversampling by a pruned polyphase FIR, only around
   candidate peaks. The tests assert a known case: 12 kHz at 45° phase on a 48 kHz grid gives
  samples of ±0.6364 and a true peak of about −0.915 dBFS.

**No third-party DSP is used.** `libebur128` and `lame` / `libsndfile` are
optional cross-check and codec libraries; libsndfile also reads supported input
formats. The engine build works without them.

### 2.3 Loudness (`src/loudness.cpp`)

BS.1770-4 exactly: 400 ms gating blocks at 100 ms step, absolute gate −70 LUFS,
relative gate −10 LU, the −0.691 offset applied once to the summed channel
energies. LRA per EBU Tech 3342. Short term is a 3 s window, momentary 400 ms.
Measurement sums channel energies without phase cancellation and takes the
maximum per-channel true peak. The processing chain still uses the mono sum;
dual-mono normalisation compensates for the delivered channel-energy sum.

Loudness normalisation **iterates**: measure → compute the gain → apply → limit
→ re-measure, up to four rounds, stopping early when the error is under 0.05 LU.
An earlier downward-only design could not recover from an over-limit and left the
integrated loudness short of the target; iterating lands within 0.05 LU while
keeping the true peak exactly on the ceiling.

### 2.4 Cleaning (`src/clean.cpp`)

* **Declicker.** The first version thresholded against a global median and
  flagged 17 % of a clean studio file as clicks. It now uses a 3-point median
  predictor residual compared against a local mean over ±25 ms, with
  `thr = max(25 × local, 0.02 × peak)`, and never touches runs longer than 4 ms
  (those are speech transients, not clicks). Result: 0 repairs on clean material,
  and all 12 injected clicks on the "rough" corpus repaired.
* **Adaptive high pass.** The corner is `clamp(0.7 × p05(voiced f0), floor, 120 Hz)`
  taken from the voiced-frame fundamental distribution, realised as a **single
  12 dB/oct biquad**. Multi-stage zero-phase cascades were tried and rejected:
  they lengthened the response and measured worse on SI-SDR.
* **Denoiser.** The material is segmented by activity; each region gets its own
  noise print, and only that region is filtered with it. This is what makes it
  work on a podcast with a pause in one acoustic and speech in another.

### 2.5 Filler Killer (`src/filler.cpp`)

Purely acoustic — **no ASR, no transcription, and nothing from this stage is ever
exposed as text.** Candidates come from an activity segmentation over the
`Segmenter` features; each candidate is scored by how much it looks like a
hesitation (isolated, short, level below the surrounding speech, spectral shape
between a vowel and a schwa, no strong pitch continuity with its neighbours) and
removed only above a mode threshold:

| Mode | Threshold | Intent |
|------|-----------|--------|
| Prudente (default) | 0.64 | Only unambiguous hesitations |
| Bilanciato | 0.54 | Ordinary editing |
| Aggressivo | 0.42 | Tight cuts, accepts some risk |

The detector resamples to its calibrated 48 kHz rate and maps hits back to the
original sample coordinates. Synthetic look-alikes occasionally exceed 0.64;
see the measured false-positive allowance in `TESTING.md`. Removals are crossfaded with an equal-power window so
there is no step in the waveform. Candidates are exposed through
`detect_fillers(..., std::vector<FillerCandidate>*)` so the GUI can show what it
*would* remove and the harness can score precision and recall.

### 2.6 Dynamics (`src/dynamics.cpp`)

Adaptive leveler (slow, phrase-scale gain riding with a strength control), a
gentle compressor, and a **forward-window true-peak limiter** — the original
lookahead implementation was backwards and let peaks through; the fix and a
`measure_peak` switch (so the caller can skip a redundant oversampled scan when
it already knows the peak) are both covered by tests.

### 2.7 Export (`src/encode.cpp`)

WAV 16/24/32f always; MP3 through LAME; FLAC, Ogg Vorbis and real Opus through libsndfile, each
when the build has them (otherwise the option is hidden in the GUI).
`recommended_mp3_bitrate()` encodes the hosting rule: mono ≤ 96 kbps and stereo
≤ 192 kbps are the project's hosting recommendations, not a guarantee that a
particular provider will preserve an uploaded file. Shared profiles also cover
stereo podcasts, WAV/FLAC archives and broadcast delivery.

---

### 2.8 Music bed (`src/music_bed.cpp`)

The bed's pause level is relative to the active speech level (default -14 dB).
The segmenter's 10 ms speech frames drive an additional -10 dB duck, with an
80 ms attack, 300 ms hold and 700 ms release. Asymmetric smoothing in dB and
per-sample interpolation avoid abrupt gain changes. Music is mono-summed and
resampled to the speech rate; repeated loops have 50 ms equal-power crossfades.
Raised-cosine fades shape the beginning and end, an offset delays the start,
and an optional tail extends the output after speech ends.

The stage follows the Filler Killer so cuts affect speech alone, using the
recomputed post-cut features. It precedes output resampling and normalisation
so the delivery loudness and true-peak ceiling cover the entire mix. Its report
records reference levels, duck ratio, loop count and tail duration. The GUI
stores the music path and settings with the session and offers a 15-second
preview (plus at most three seconds of tail).

---

## 3. Observability

This was a first-class requirement, not an afterthought.

* **Every run emits a JSON report** (`Report::to_json()`): tool and version,
  the full `Params`, a `params_hash`, input and output descriptions, before/after
  analysis, the filler list with scores, the conformance verdict, the warnings,
  total time and real-time factor.
* **Every stage reports `name`, `enabled`, `ms`, `rtf` and a `metrics` map.**
  Disabled stages still appear, with `enabled: false` and no metrics key.
* The GUI surfaces the same data in three places: the Master page stage table,
  the Standards page conformance tiles, and the Diagnostics page JSON viewer,
which can also run its in-process diagnostic scenarios and save the report.
* The CLI exits with code 3 when the result is not conformant, so a build
  pipeline can gate on it.

`rtf` is processing milliseconds divided by the audio milliseconds. On this
machine the installed 8-second GUI self-test measured RTF about 0.04. Runtime
depends on material, options and hardware; short corpus timings do not establish
an hour-long episode's completion time.

---

## 4. The GUI

### 4.1 Structure

A fixed 238 px navigation rail on the left, a `QStackedWidget` of seven pages,
and a status bar that always carries a state chip. Pages are scrolled columns of
`Card`s. Each card has a title, an optional subtitle, a `Chip` that always shows
the panel's state, and — for the long cards — a collapse button, so a working
page can be folded down to its headers.

The seven pages are:

1. **Registrazione** — source selection (microphone / file / synthetic corpus),
   input-device picker, sample rate, channels, input gain,
   a guided three-second microphone test that measures the
   input peak and recommends a gain change, REC / PAUSA / RIPRENDI / STOP, a
   hotkeyed marker button (`M`), an analogue VU, sample-peak and 4x true-peak bars,
   an ON AIR sign, a rolling
   30-second waveform, and the crash-safe recovery control.
2. **Sanity Master** — the preset, every switch in the chain with its own
   strength control, the Filler Killer mode plus a "show what it would remove"
   background candidate table, music-bed controls and preview, the process button
   with elapsed time, before/after loudness meters and tiles, A/B playback
   of the original against the master, and the per-stage diagnostics table.
3. **Standard & Loudness** — the published standards, the target editor, the
   conformance panel, and a "measure a file" analysis of supported audio with the full
   BS.1770-4 read-out.
4. **Libreria / Sessioni** — saved sessions with loudness, true peak and
   conformance, per-row open / re-export / reveal / delete, search and a
   configurable library folder.
5. **Esporta & Pubblica** — format, channels, rate, filename, the no-re-encode
   hint, a pre-flight conformance check, the export itself, and the publishing
   steps for publishing through a hosted RSS feed. The app makes no upload calls.
6. **Impostazioni** — theme, engine defaults, folders, the offline/privacy
   statement and the version.
7. **Diagnostica & Test** — runs the e2e harness in-app on three synthetic
   profiles and shows the measured precision, recall, SNR, SI-SDR, RTF,
   determinism and conformance, plus the raw JSON report and the observability
   contract the suite enforces.

### 4.2 Threading and state

`AppState` is a singleton `QObject` holding the current `Session` (working audio,
parameters, master, report). Pages never call each other; they read and write
`AppState` and listen to its signals (`audio_changed`, `mastered_changed`,
`params_changed`, `library_changed`, `mastering_started/finished`,
`status_message`, `theme_changed`).

Mastering, diagnostic scenarios, filler detection and music-preview mixing run
on workers and post results back to the GUI thread. File loading, some preview
preparation and capture-meter updates still run on the GUI thread. The microphone
test is timer-driven; it does not sleep on the GUI thread. Cancel marks a job
discarded immediately, updates the UI and ignores its eventual result; the
already-running engine computation continues in the background.

The self test can walk all seven pages and capture each one with
`QWidget::grab()` (`--selftest-shots DIR`), which is the visual-regression
check and needs no X11 screenshot tooling. It is how the double-escaped
`\u25CF` / `\n` sequences in the button labels and the brand wordmark were
caught.

### 4.3 Crash-safe recording

While recording, incoming 16-bit PCM is appended to
`<cache>/podcast-sanity/recording-live.pcm` together with a JSON sidecar holding
the rate, channel count and format, written before the first audio arrives. A
take is only "closed" when STOP reads the file back and hands the samples to the
session. If the app dies, the file is still on disk and the Record page offers to
recover it. Memory during recording stays bounded: only a rolling 30-second
window is kept for the display, and the full take is read from disk at the end.

The guided flow covers a level test, not a full calibration wizard; see §7.

---

## 5. Packaging

* **Linux**: `cpack -G DEB` produces `podcast-sanity_<version>_amd64.deb` with
  the two binaries, the `.desktop` entry, the icon and the AppStream metainfo.
  Dependencies are generated with `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` and pinned to
  the Qt6, LAME and libsndfile runtime packages.
* **Windows**: a fixed `CPACK_WIX_UPGRADE_GUID` makes upgrades replace the
  previous install rather than stack beside it. Version 0.2.0 includes Studio
  artwork, a finish-page launch checkbox, trimmed Qt runtime and signing hooks.
  Upgrade from 0.1.0, isolated installed-app execution and uninstall were tested.
* Both are driven from the same `CMakeLists.txt`; `CPACK_GENERATOR` selects
  `DEB;TGZ` on Linux and `WIX;ZIP` on Windows.

---

## 6. What is verified, and how

See `TESTING.md`. Windows: 11/11 CTest tests, 36 unit cases / 1342 checks,
a 12-case / 66-check end-to-end harness (63 checks at 44.1 kHz), and a GUI self test
that walks all seven pages, masters, exports an MP3 and checks the report
contract plus screenshot coverage. Debian release checks remain pending.

---

## 7. Known limitations

* **Stereo imaging is not processed.** The chain works on the mono sum, so any
  genuine stereo content (music beds, wide ambience) is collapsed and re-spread.
  A per-channel or mid/side path is the next significant piece of work.
* **No ASR.** Filler Killer is acoustic only. The "modalità precisa" that would
  detect individual filler *words* is a separate, larger project and is
  deliberately absent.
* **Cancellation does not preempt DSP computation.** The UI reports cancellation
  immediately and discards the worker's result; CPU work may continue until the
  current job completes.
* **Recording device hot-plug** is detected but the recorder does not silently
  migrate to a new default device mid-take.
* The guided microphone test measures the minimum rolling RMS during the test; it
  does not yet run a full sweep or a room-tone capture.
* Lossy encoding may raise decoded peaks. Delivery conformance describes the
  unencoded master; the recommended MP3's decoded peak is separately checked.
* Code signing requires a certificate the project does not have yet.
* Interactive recording, preview, installer Finish and subjective listening
  checks remain pending; automated screenshots cover the rendered interface.
