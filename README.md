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
cpack -G DEB      # Linux: podcast-sanity_0.1.0_amd64.deb
cpack -G WIX      # Windows: podcast-sanity-0.1.0-win64.msi
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

---

## Documentation

* [`docs/DESIGN-podcast-sanity.md`](docs/DESIGN-podcast-sanity.md) — architecture,
  the stage chain and why it is ordered that way, the GUI, packaging, limits.
* [`docs/STANDARDS.md`](docs/STANDARDS.md) — what is measured, the delivery
  targets and where the numbers come from.
* [`docs/TESTING.md`](docs/TESTING.md) — the unit, end-to-end and GUI suites,
  the metrics, and how the Filler Killer thresholds were calibrated.

## Verified, not asserted

* 28 unit cases / 1269 checks over the DSP primitives (without the libebur128
  cross-check, which the Windows build does not link).
* A 10-case / 55-check end-to-end harness that generates synthetic episodes with
  ground truth, masters them and judges loudness, true peak, filler precision and
  recall, SNR, SI-SDR, determinism and report completeness.
* A GUI self test that walks all seven pages, masters, exports an MP3 and checks
  the report contract — run under `xvfb` in CI.
* Every run emits a JSON response with per-stage timings, real-time factor,
  a parameters hash and the conformance verdict.

## License

GPL-3.0. See [`LICENSE`](LICENSE).
