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
* Multi-channel material is folded to mono, so the reported integrated loudness
  is that of the mono sum — the same signal the chain processes. See the
  limitation in `DESIGN-podcast-sanity.md` §7.

**True peak** is measured with 4× oversampling and a polyphase FIR, evaluated
only near candidate peaks (a pruned scan). The unit tests pin the detector with
a known case: a 12 kHz sine at 45° phase on a 48 kHz grid has samples of ±0.6364
and a true peak of about **−0.915 dBFS**. When `libebur128` is present it is used
as an independent cross-check in the tests, not as the production meter — note
that `ebur128_true_peak` returns a *linear* value (1.0 = 0 dBTP).

---

## 2. Delivery targets

The defaults follow the **Apple Podcasts Authoring Practices**, which is what
most podcast tooling in practice meets.

| Parameter | Mono (spoken word) | Stereo (music / mixed) |
|-----------|--------------------|------------------------|
| Integrated loudness | **−19 LUFS** | **−16 LUFS** |
| True peak ceiling | **−1.0 dBTP** | **−1.0 dBTP** |
| Tolerance | ±1 LU | ±1 LU |

`Params::target_lufs = 0` is the sentinel for "automatic": the engine picks
−19 for a mono output and −16 for stereo. Every other standard is expressible by
setting the target explicitly. The GUI exposes these presets:

| Preset | Loudness | True peak | Note |
|--------|----------|-----------|------|
| Apple Podcasts, mono voice | −19 LUFS | −1.0 dBTP | Default for `Preset::Talk` |
| Apple Podcasts, stereo | −16 LUFS | −1.0 dBTP | Default for `Preset::Music` |
| Spotify (playback target) | −14 LUFS | −1.0 dBTP | Spotify normalises to −14; publishing louder gains nothing |
| EBU R128 / broadcast | −23 LUFS | −1.0 dBTP | European broadcast |
| AES streaming | −16 LUFS | −1.0 dBTP | |
| Custom | any | any | |

Optional ceilings: `max_short_term` and `max_momentary` are **enforced** by a slow
gain reduction inside the normalisation stage and then **reported**; `max_lra` is
**reported only** (a loudness range reduction would be a second leveler pass and
is not part of the chain). All three are 0 = unconstrained; when set, exceeding
them makes `conformance.ok` false with the reason in `warnings`.

---

## 3. Encoding and the hosting envelope

Spotify, Apple Podcasts and the other directories do not accept uploads: they
read a **RSS feed** hosted elsewhere (Buzzsprout, Transistor, Spotify for
Creators, your own server). The hosting provider is the thing that may re-encode,
so the file should arrive already inside the envelope the host promises not to
touch:

* **Mono ≤ 96 kbps MP3**, **stereo ≤ 192 kbps MP3**.

`recommended_mp3_bitrate(channels)` encodes exactly this, and the export page
uses it as the default and warns when a higher bitrate is chosen. Because the
master is already loudness- and peak-conformant *before* encoding, a re-encode
by the host cannot move it out of spec by any meaningful amount.

Other formats are offered for archival or for hosts that accept them: WAV 16/24
and 32-bit float, FLAC 24, and Ogg Vorbis. The build hides any format for which
no encoder is present rather than offering a button that cannot work.

---

## 4. What "conformant" means in the report

`Report::conformance` carries six booleans, and the CLI exits 3 when the
overall verdict is false:

```json
"conformance": { "loudness_ok": true, "true_peak_ok": true, "lra_ok": true, "short_term_ok": true, "momentary_ok": true, "ok": true }
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

---

## 5. References

* ITU-R BS.1770-4, *Algorithms to measure audio programme loudness and true-peak
  audio level*.
* EBU Tech 3341 / 3342, *Loudness metering and loudness range*.
* Apple, *Podcast Authoring Practices* (−16 stereo / −19 mono, −1 dBTP).
* AES TD1004, *Recommendation for loudness of audio streaming*.
* Buzzsprout / Transistor hosting documentation, for the no-re-encode MP3
  bitrate envelopes.
