# Changelog

## 0.2.0 — 2026-10-16

### Engine and reliability
- Measure multichannel loudness without phase cancellation and meter each
  channel's true peak; compensate dual-mono output loudness correctly.
- Resample before final limiting, improve peak/loudness iteration, bound the
  recording display envelope and correct LRA energy gating.
- Detect fillers at the calibrated rate, improve parameter hashes and nested
  JSON, guard empty resampling, scale hum probes and enforce the high-pass floor.
- Read supported audio through libsndfile; offer genuine Opus encoding when
  available. Discard cancelled worker results and handle worker exceptions.

### Delivery and music
- Share five delivery profiles between GUI and CLI, recommending MP3 96 kbps
  mono at -19 LUFS; re-master when delivery settings change.
- Add music beds with speech-driven ducking, looping, fades, offset and tail,
  CLI switches, session persistence and a 15-second GUI preview.

### Interface
- Add the default charcoal/amber Studio theme and bundled Ubuntu fonts while
  preserving Light/Dark preferences.
- Add VU/loudness meters, measured true-peak bars, ON AIR, LED state indicators,
  painted transport/navigation controls, waveform ruler and dB grid.
- Make the master-page load button functional and run filler candidates on a
  worker. Remove recording controls that had no implementation.

### Packaging and verification
- Ship a trimmed Windows runtime, branded MSI dialogs and launch checkbox,
  publisher metadata, signing hooks and automated upgrade/uninstall checks.
- Windows: 11/11 CTest tests, 36 unit cases / 1342 checks and a 12-case harness.
- Debian: 9/9 CTest tests (39 unit cases / 1353 checks with libebur128), a .deb
  that upgrades 0.1.0 to 0.2.0 and removes cleanly, a -16.0 LUFS ffmpeg
  cross-check, and an ASan/UBSan build with no sanitizer report.
- Report the project version consistently in the summary, the JSON report and
  `--version`, and install the Debian copyright and third-party notices under
  `/usr/share/doc/podcast-sanity`.
- Subjective listening, interactive recording/preview/installer checks and
  certificate-based signing remain pending.

## 0.1.0 — 2026-10-15
- Initial local recording, mastering, acoustic filler removal, session library
  and audio export release.
