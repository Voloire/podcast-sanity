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
* **Background music bed with ducking and a 15 s preview**
* five shared delivery profiles, including recommended **podcast-voice**
* **Studio** interface with Ubuntu fonts, VU meters, ON AIR, painted controls
  and a waveform time ruler; Light and Dark themes remain available

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

Windows 11 x64: use `packaging\build-windows.ps1` to build the native application,
run the tests and create a self-contained **MSI** and portable ZIP. The installer
includes Qt, codecs and the Visual C++ runtime and installs for the current user.
See [Windows build and installation](packaging/README.md#windows--msi) for
prerequisites and paths. Docker, WSL and Python are not required to run the app.

### Packages

```sh
cd build
cpack -G DEB      # Linux: podcast-sanity_0.2.0_amd64.deb
cpack -G WIX      # Windows: podcast-sanity-0.2.0-win64.msi
```

---

## Use

Launch `podcast-sanity`. Seven pages in the left rail:

**Registrazione** → **Sanity Master** → **Standard & Loudness** →
**Libreria / Sessioni** → **Esporta & Pubblica** → **Impostazioni** →
**Diagnostica & Test**

Record (or load a file, or use the built-in synthetic corpus if you have no
microphone — the engine path is identical), press *Elabora*, check the
before/after numbers, export an MP3 at 96 kbps mono, upload it to your hosting,
and publish through your RSS feed. The application does not upload files or
call a publishing API; use your host's publishing workflow.

The export page offers the same profiles; the recommended one is selected by default.

While recording, audio is appended to a crash-safe file as it arrives, so a crash
does not lose the take — the Record page offers to recover it.

### Command line

The same engine, scriptable:

```sh
podcast-sanity-render episode.wav --out master.wav \
    --power-clean --filler cautious --summary --report report.json
podcast-sanity-render episode.wav --analyze-only
podcast-sanity-render episode.wav --out episode.mp3 --deliver podcast-voice
podcast-sanity-render episode.wav --out mix.mp3 --deliver podcast-voice \
    --music bed.flac --music-level -14 --music-duck -10 --music-tail 2
podcast-sanity-render --list-profiles
podcast-sanity-render --gen-corpus demo.wav --profile home --seconds 30
```

The renderer exits **3** when the result is not conformant.

---

## Documentation

* [`docs/manuale-utente.html`](docs/manuale-utente.html) — Italian user manual:
  open locally in a browser for step-by-step workflows, control explanations,
  practical settings, troubleshooting and a glossary. Works offline.
* [`docs/DESIGN-podcast-sanity.md`](docs/DESIGN-podcast-sanity.md) — architecture,
  the stage chain and why it is ordered that way, the GUI, packaging, limits.
* [`docs/STANDARDS.md`](docs/STANDARDS.md) — what is measured, the delivery
  targets and where the numbers come from.
* [`docs/TESTING.md`](docs/TESTING.md) — the unit, end-to-end and GUI suites,
  the metrics, and how the Filler Killer thresholds were calibrated.

## Verified, not asserted

* **11/11 Windows CTest tests pass**: unit, four CLI checks, four end-to-end
  configurations, GUI and GUI screenshots.
* 36 unit cases / 1342 checks over the DSP primitives (without the libebur128
  cross-check, which the Windows build does not link).
* A 12-case / 66-check end-to-end harness (63 checks at 44.1 kHz) that generates synthetic episodes with
  ground truth, masters them and judges loudness, true peak, filler precision and
  recall, SNR, SI-SDR, determinism and report completeness.
* A GUI self test that walks all seven pages, masters, exports an MP3 and checks
  the report contract. Linux CI is configured for `xvfb`; release verification
  on the owner's Debian workstation is pending.
* Every run emits a JSON response with per-stage timings, real-time factor,
  a parameters hash and the conformance verdict.
* MSI 0.1.0-to-0.2.0 upgrade, isolated installed-app test and uninstall verified.
  The MSI is unsigned; signing requires an owner-provided certificate.
* Subjective listening and interactive recording/preview/installer-finish checks
  remain pending. Automated screenshots are not a substitute for those checks.

## License

GPL-3.0. See [`LICENSE`](LICENSE).
