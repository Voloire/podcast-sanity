# Processing policy, version 1

This file is the reference for what the skill decides and what its numbers prove. The thresholds
below are implemented in `src/auto_master.cpp`; the policy version (`1`) is recorded in every
report so a result can be traced back to the rules that produced it. Changing a threshold or a
candidate requires bumping `kAutoPolicyVersion` and updating this file.

## 1. What the automatic choice is, and is not

The engine measures the file, renders a small set of bounded candidates on deterministic excerpts,
scores them against the same speech/quiet mask, and keeps the strongest candidate that passes the
preservation gates. It is not a perceptual model, it does not listen, and it does not search for a
global optimum. Every claim below is a measurement with a stated method; none of them is a promise
about how the result sounds.

Content editing is out of scope on purpose: filler removal, silence trimming, word removal and
time stretching are disabled for every candidate. Clipped words are not reconstructed; the report
counts the damaged samples instead.

## 2. Evidence gathered before any processing

Measured on the whole file, on the loudest channel as detector (never an L+R sum, which cancels
anti-phase material):

| Evidence | Used for |
|---|---|
| Speech activity and per-segment levels | Detector, leveler strength, confidence |
| Quiet regions and their noise floor | Denoise score, confidence |
| Mains hum fundamental and excess | Whether de-hum runs at all |
| RT60 estimate | De-reverb, only above 0.30 s and with speech present |
| Sibilance (5-8 kHz to total ratio) | De-essing |
| Loudness range (LRA) | Leveler strength |
| Channel RMS, channel imbalance, stereo correlation | Stereo preservation gate |
| Clipped sample count | Report only |

`music_ratio` is a deliberately crude continuous-sound heuristic (non-speech frames above the
noise floor, tonal, not percussive). Above 0.25 it lowers restoration aggressiveness; it is never
used to claim a music/speech classification.

## 3. Representative material

- Up to 8 excerpts of 8 s, each with 0.5 s of context, taken from: start, middle, end, the
  quietest region, the loudest speech, the lowest-level speech, the loudest transient, and a
  reverb-evidence position when RT60 > 0.30 s.
- Overlapping excerpts are dropped; the same speech/quiet mask is used for every candidate.
- A file shorter than 64 s is compared in full, which is the most representative material
  available. Reports name the reason for each excerpt.

## 4. Candidates

| Candidate | Denoise | De-reverb | Purpose |
|---|---|---|---|
| `preservation` | 0 | 0 | Repair and leveling reference, no spectral suppression |
| `gentle` | 0.4 | 0.2 | Clean recordings, or an uncertain noise estimate |
| `balanced` | 0.8 | 0.4 | Typical room and background noise |
| `strong` | 1.2 | 0.6 | Clear contamination that still passes the gates |

De-reverb is additionally gated on reliable decay evidence, and both denoise and de-reverb are
capped at 0.4 / 0.2 when continuous background content is detected.

De-hum runs only when a mains component was detected, and only on the harmonics that carry hum.
The adaptive high-pass stays below the detected voice fundamental (55 Hz floor). De-essing runs
above -16 dB sibilance, with an amount of `clamp((sibilance + 16) / 6, 0.2, 1.0)`.

Leveling is chosen from the measured loudness range: `>= 10 LU` full, `>= 6 LU` 0.8, `>= 3 LU`
0.5, otherwise 0.2. The compressor threshold sits 6 dB under the active speech level, ratio 3:1,
10 ms attack, 200 ms release. Both are stereo-linked: one gain envelope from the loudest channel
is applied to every channel, so a transient on one side cannot pull the image.

## 5. Metrics and gates

All comparisons are made after removing the candidate's intentional level change, using the
active speech level of the candidate against the input.

| Metric | Method | Gate |
|---|---|---|
| Quiet-region reduction | Mean energy of the shared non-speech frames, gain aligned | Score (dB) |
| Residual hum | Narrow-band energy at the detected fundamental | +0.5 weight in the score when hum was present |
| Speech envelope | 10th percentile of per-frame speech energy change, gain aligned | `>= -12 dB` |
| Speech band | 300-3400 Hz energy over speech frames, gain aligned | `>= -1.5 dB` |
| SI-SDR on speech samples | Candidate against the `preservation` render, speech samples only | `>= 0 dB` |
| Stereo imbalance change | `abs(after - before)` of the L/R level difference | `<= 0.5 dB` |
| New full-scale samples | Samples at or above 1.0 in the float render | `0` |

Why these numbers:

- The speech-envelope gate is generous because the frames it measures also contain the noise the
  candidate is supposed to remove; a real loss of voice is much larger than 12 dB. It exists to
  catch a collapsed candidate, not to fine-tune.
- The speech-band gate is tight because suppressing the 300-3400 Hz band is what makes a voice
  sound thin and distant.
- SI-SDR against the preservation render is the check that separates "changed" from "improved":
  it is measured only where the shared mask says speech, so it answers "did the voice survive"
  rather than "did the file change". It is a proxy, not perceptual proof.
- Clipping is counted at 1.0, not at 0.9999: the working format is float, so values above 0.9999
  are headroom that the delivery limiter can use.

## 6. Selection

1. Reject every candidate that fails a gate, and record the reason.
2. Score the survivors by gain-aligned quiet-region reduction, plus half the hum reduction when
   hum was present.
3. Take the strongest survivor. An improvement below 1 dB over the current best is a tie, and a
   tie goes to the weaker candidate.

The full file is then rendered once with the chosen settings, and preservation is re-checked at
full length (envelope `>= -12 dB`, band `>= -2 dB`, imbalance `<= 0.5 dB`, no new full-scale
samples). If that fails, exactly one weaker non-rejected candidate is rendered again from the
original PCM. Candidates are never cascaded and already-processed audio is never denoised again.

With fewer than 20 quiet frames the search is capped at `gentle`, confidence is capped at 0.35,
and a warning is recorded: a failed noise estimate must never become maximum denoise.

## 7. Delivery

The runner, not the engine, owns delivery: channel-aware two-pass FFmpeg `loudnorm` to the
profile target, `libmp3lame` CBR encode, then an independent full decode and `ebur128=peak=true`
measurement of the encoded file. The PCM ceiling starts at -2 dBTP. If the encoded file overshoots
-1.0 dBTP the ceiling is lowered by the measured excess plus 0.2 dB and the file is normalized
again from the lossless master; at most three attempts, never a re-encode of an MP3 attempt.

Profile `podcast`: -16 LUFS +/-1 LU, true peak `<= -1.0 dBTP`, 128 kbps mono / 256 kbps stereo.
Profile `speech-mono`: -19 LUFS, otherwise identical; it is the legacy convention and an explicit
choice, not a current vendor requirement.

## 8. Calibration evidence

Measured on 2026-11-07 with renderer 0.2.0 on Linux x86-64, synthetic corpus from
`--gen-corpus`:

| Input | Chosen | Quiet reduction | SI-SDR | Fallback |
|---|---|---|---|---|
| `rough`, 12 s | `gentle` | 1.8 dB | 13.8 dB | no |
| `studio`, 10 s | `preservation` | -0.6 dB | n/a (reference) | no |
| `rough`, stereo, 3 ms / -2 dB offset | `gentle` | 1.8 dB | 13.8 dB | no, imbalance change 0.001 dB |

The denoiser saturates on this synthetic material: `gentle`, `balanced` and `strong` land within
about 0.1 dB of each other on quiet-region reduction, so the tie rule correctly keeps the weakest
of them. The studio corpus selects `preservation` because no candidate improves on it.

These numbers calibrate the gates; they are not a promise about arbitrary real recordings. Real
speech with soft consonants, low voices, sibilance, breaths and background music is required
release evidence and is listed as an open gap in the plan until such recordings are available.
