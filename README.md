# podcast-sanity

Record a podcast and master it **locally**, offline. No upload, no cloud model,
no telemetry. The engine does the parts of a commercial "magic mastering"
service that matter for spoken word:

* click, pop, DC and clipping repair, mains-hum notch, adaptive high pass
* **Power Clean** — spectral denoiser and de-reverb
* **Filler Killer** — removes hesitations and empty pauses, acoustically, with
  **no transcription and no ASR anywhere in the program**
* **Smart Adaptive Leveling** plus gentle compression
* loudness and true peak to the delivery standard (BS.1770-4), default
  −19 LUFS mono / −16 LUFS stereo, true peak ≤ −1 dBTP
* a **Talk** preset and a **Music** preset
* an autonomous, headless path for OpenCode: one request, one verified
  `_processed.mp3`, every setting chosen from measurement

Ships as a Windows **MSI** and a Linux **.deb**, from one CMake project.

![icon](data/podcast-sanity.svg)

---

## Build

Dependencies: a C++20 compiler, CMake ≥ 3.20, Qt6 Widgets and Qt6 Multimedia,
and optionally LAME and libsndfile for MP3 / FLAC export and libebur128 for the
loudness cross-check in the tests.

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build qt6-base-dev \
     qt6-multimedia-dev libmp3lame-dev libsndfile1-dev libebur128-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

Windows: configure with the same command in a Qt6 developer shell; the WiX
generator needs the WiX Toolset on `PATH`.

### Packages

```sh
cd build
cpack -G DEB      # Linux: podcast-sanity_0.2.0_amd64.deb
cpack -G WIX      # Windows: podcast-sanity-0.2.0-win64.msi
```

Both packages carry the skill source and its installer under
`share/podcast-sanity/`; nothing is copied into an OpenCode directory by the
package itself.

---

## Use

Launch `podcast-sanity`. Seven pages in the left rail:

**Registrazione** → **Sanity Master** → **Standard & Loudness** →
**Libreria / Sessioni** → **Esporta & Pubblica** → **Impostazioni** →
**Diagnostica & Test**

Record (or load a file, or use the built-in synthetic corpus if you have no
microphone — the engine path is identical), press *Elabora*, check the
before/after numbers, export an MP3 at 96 kbps mono, upload it to your hosting,
and publish through your RSS feed. Spotify and Apple Podcasts read that feed;
neither has an upload API.

While recording, audio is appended to a crash-safe file as it arrives, so a crash
does not lose the take — the Record page offers to recover it.

### Command line

The same engine, scriptable:

```sh
podcast-sanity-render episode.wav --out master.wav \
    --power-clean --filler cautious --summary --report report.json
podcast-sanity-render episode.wav --analyze-only
podcast-sanity-render --gen-corpus demo.wav --profile home --seconds 30
```

The renderer exits **3** when the result is not conformant.

### From OpenCode: the `podcast-master` skill

Linux preview: short synthetic recordings and discovery have been verified.
Windows, real-speech calibration and the 60-minute memory gate are not yet
release-qualified. See the
[implementation status](docs/superpowers/plans/2026-11-07-podcast-master-progress.md).

The skill turns one request into one verified MP3 beside the original:

```text
/recordings/Episode 07.take2.wav
/recordings/Episode 07.take2_processed.mp3
```

It analyzes the file, compares four bounded restoration candidates on excerpts
of the real material, keeps the strongest one that passes the preservation
gates, levels with stereo-linked dynamics, then normalizes and encodes for
delivery. Filler removal, silence trimming and time stretching are off, and the
source is never modified.

```sh
# Install the skill into ~/.config/opencode/skills/podcast-master
python3 tools/install_opencode_skill.py --action install \
    --renderer build/podcast-sanity-render
python3 tools/install_opencode_skill.py --action check --json

# The same command the skill runs
python3 ~/.config/opencode/skills/podcast-master/scripts/process_audio.py \
    --input "/recordings/Episode 07.take2.wav" --json
```

`podcast` profile: −16 LUFS ±1 LU and true peak ≤ −1.0 dBTP, measured on the
decoded MP3, LAME CBR 128 kbps mono / 256 kbps stereo. Runtime requirements are
Python 3.10+, FFmpeg and ffprobe; the Qt application and a C++ compiler are not
needed to process audio. Details in
[`skills/podcast-master/SKILL.md`](skills/podcast-master/SKILL.md) and
[`references/processing-policy.md`](skills/podcast-master/references/processing-policy.md).

---

## Documentation

* [`docs/DESIGN-podcast-sanity.md`](docs/DESIGN-podcast-sanity.md) — architecture,
  the stage chain and why it is ordered that way, the GUI, packaging, limits.
* [`docs/STANDARDS.md`](docs/STANDARDS.md) — what is measured, the delivery
  targets and where the numbers come from.
* [`docs/TESTING.md`](docs/TESTING.md) — the unit, end-to-end, policy, runner
  and GUI suites, the metrics, and how the Filler Killer thresholds were
  calibrated.
* [`skills/podcast-master/SKILL.md`](skills/podcast-master/SKILL.md) — the
  OpenCode skill: one command, success checks, when to ask the user.
* [`skills/podcast-master/references/processing-policy.md`](skills/podcast-master/references/processing-policy.md)
  — candidates, gates, thresholds and the calibration evidence behind them.

## Verified, not asserted

* 22 unit cases / 127 checks over the DSP primitives.
* An 8-case / 42-check end-to-end harness that generates synthetic episodes with
  ground truth, masters them and judges loudness, true peak, filler precision and
  recall, SNR, SI-SDR, determinism and report completeness.
* An 8-case / 84-check suite for the autonomous policy: determinism, clean
  material staying untouched, noisy material being cleaned with measurable
  evidence, bounded candidates, stereo preservation, empty input and the report
  contract.
* 8 runner contract tests that drive the real Python entry point against the
  real renderer and FFmpeg: naming, collision without overwrite, real MP3 codec
  and bitrate, final loudness and true peak on the decoded file, source hash
  unchanged, stereo kept stereo, invalid input, exported report.
* A GUI self test that walks all seven pages, masters, exports an MP3 and checks
  the report contract — run under `xvfb` in CI.
* Every run emits a JSON response with per-stage timings, real-time factor,
  a parameters hash and the conformance verdict.

## License

GPL-3.0. See [`LICENSE`](LICENSE).
