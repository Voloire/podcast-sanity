# Standards and delivery targets

podcast-sanity measures and conforms to published standards rather than to a
house preference. This note records exactly what is implemented, what the
defaults are, and where the numbers come from.

---

## 1. Loudness measurement

**ITU-R BS.1770-4**, implemented in `src/loudness.cpp`.

* K-weighting is the two-stage filter of the recommendation: a high shelf
  (+4 dB at ~1681.97 Hz, Q 0.70718) into a high pass (38.14 Hz, Q 0.50033). The
  coefficients are derived analytically from the stage definitions so every
  sample rate is supported; at 48 kHz they agree with the tabulated values to
  1e-9, which the unit tests assert.
* Integrated loudness uses 400 ms gating blocks at 100 ms step (75 % overlap),
  an absolute gate at −70 LUFS and a relative gate 10 LU below the ungated mean,
  and applies the −0.691 dB offset exactly once, to the summed channel energies.
* Momentary loudness is the 400 ms window value; short term is 3 s.
* Loudness range (LRA) follows EBU Tech 3342.
* Channel energy is summed across channels before gating, per BS.1770: the
  `AudioBuffer` overloads of `measure_loudness()`, `integrated_lufs()`,
  `loudness_range()` and `true_peak_db()` are what the autonomous path and the
  delivery checks use. Folding L+R first would understate anti-phase material and
  hide a channel imbalance. The legacy `master()` chain still processes a mono
  sum and therefore reports the loudness of that sum; see §7 of
  `DESIGN-podcast-sanity.md`.

**True peak** is measured with 4× oversampling and a polyphase FIR, evaluated
only near candidate peaks (a pruned scan). The unit tests pin the detector with
a known case: a 12 kHz sine at 45° phase on a 48 kHz grid has samples of ±0.6364
and a true peak of about **−0.915 dBFS**. When `libebur128` is present it is used
as an independent cross-check in the tests, not as the production meter — note
that `ebur128_true_peak` returns a *linear* value (1.0 = 0 dBTP).

---

## 2. Delivery targets

Two named profiles exist. They are delivery choices, not measurement standards:
BS.1770 defines how to measure loudness and true peak, not what a podcast should
target.

| Profile | Integrated loudness | True peak | Used by |
|---------|--------------------|-----------|---------|
| `podcast` | **−16 LUFS** ±1 LU, mono or stereo | **≤ −1.0 dBTP** | The `podcast-master` skill, the default for new work |
| `speech-mono` | **−19 LUFS** ±1 LU | **≤ −1.0 dBTP** | Explicit choice, and the legacy CLI/GUI default for mono |

−16 LUFS with a −1 dBTP ceiling is the current Apple Podcasts guidance for
spoken-word delivery, mono included; the older −19 mono convention is kept only
as `speech-mono` and is not described as a current vendor requirement. The GUI
presets remain as before and map onto these numbers:

| Preset | Loudness | True peak | Note |
|--------|----------|-----------|------|
| Apple Podcasts, mono voice | −19 LUFS | −1.0 dBTP | Legacy `Preset::Talk` default |
| Apple Podcasts, stereo | −16 LUFS | −1.0 dBTP | Default for `Preset::Music` |
| Spotify (playback target) | −14 LUFS | −1.0 dBTP | Spotify normalises to −14; publishing louder gains nothing |
| EBU R128 / broadcast | −23 LUFS | −1.0 dBTP | European broadcast |
| AES streaming | −16 LUFS | −1.0 dBTP | |
| Custom | any | any | |

Optional ceilings: `max_lra`, `max_short_term` and `max_momentary`. All three are
**0 = unconstrained** by default; when set, exceeding them is reported as a
conformance failure. A non-zero `max_short_term` or `max_momentary` turns on
clause-by-clause enforcement in the pipeline.

---

## 3. Encoding and the hosting envelope

Spotify, Apple Podcasts and the other directories do not accept uploads: they
read a **RSS feed** hosted elsewhere (Buzzsprout, Transistor, Spotify for
Creators, your own server). The hosting provider is the thing that may re-encode,
so the file should arrive already inside the envelope the host promises not to
touch:

* **Mono ≤ 96 kbps MP3**, **stereo ≤ 192 kbps MP3**.

`recommended_mp3_bitrate(channels)` encodes exactly this, and the export page
uses it as the default and warns when a higher bitrate is chosen.

The autonomous skill uses a different, explicit pair: **128 kbps mono / 256 kbps
stereo**, LAME CBR, which is what Apple's guidance allows for podcast delivery.
It is not a claim that a host will leave the file alone.

Compliance is never asserted on the pre-encode master. The skill normalises to a
−2 dBTP PCM ceiling for codec headroom, encodes, then decodes the whole MP3 and
meters it with `ebur128=peak=true`; only that decoded measurement decides
pass/fail. If the codec overshoots −1.0 dBTP, the ceiling is lowered by the
measured excess plus 0.2 dB and the file is normalized again from the lossless
master, up to three attempts. A file that cannot satisfy both the loudness
tolerance and the peak ceiling fails with exit 3 and is not published under the
expected name.

Other formats are offered for archival or for hosts that accept them: WAV 16/24
and 32-bit float, FLAC 24, and Ogg Vorbis. The build hides any format for which
no encoder is present rather than offering a button that cannot work.

---

## 4. What "conformant" means in the report

`Report::conformance` carries three booleans, and the CLI exits 3 when the
overall verdict is false:

```json
"conformance": { "loudness_ok": true, "true_peak_ok": true, "ok": true }
```

* `loudness_ok` — the achieved integrated loudness is within tolerance of the
  target (the engine lands within 0.05 LU on the corpus).
* `true_peak_ok` — the measured true peak of the **written output** is at or
  below the ceiling. Normalisation iterates limit → measure → correct so the
  peak sits *on* the ceiling rather than below it, which is what makes the
  loudness target reachable without clipping.
* `ok` — both, plus any configured maximum LRA / short-term / momentary.

Nothing else in the report decides conformance: the filler counts and the
per-stage metrics are diagnostics, not gates.

The skill's report has its own `conformance` object with `loudness_ok`,
`true_peak_ok`, `duration_ok` and `codec_ok` (which also covers the channel
count), all measured on the decoded MP3 rather than on the PCM master. The
engine's `full_file_check` is a separate, earlier gate on preservation and never
counts as a delivery verdict.

---

## 5. References

* ITU-R BS.1770-4, *Algorithms to measure audio programme loudness and true-peak
  audio level*.
* EBU Tech 3341 / 3342, *Loudness metering and loudness range*.
* Apple, *Podcast Authoring Practices* (about −16 LUFS and a −1 dBTP ceiling for
  podcast delivery; the −19 mono figure is a legacy convention).
* AES TD1004, *Recommendation for loudness of audio streaming*.
* Buzzsprout / Transistor hosting documentation, for the no-re-encode MP3
  bitrate envelopes.
