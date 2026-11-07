---
name: podcast-master
description: Master a local podcast recording autonomously - cleanup, noise and hum removal, de-reverb, leveling, loudness normalization to -16 LUFS and MP3 delivery. Use it whenever the user asks to clean, fix, master, normalize the volume of, or prepare a podcast, interview or voice recording for distribution, or expects a `_processed.mp3` beside their file. The skill chooses every setting itself; do not ask the user about denoise strength, presets, compression or bitrate.
---

# Podcast mastering

One command takes a local audio file and writes a verified `<stem>_processed.mp3` next to it.
The restoration policy, the loudness targets and the delivery checks are fixed by this skill.
Never replace the command with a hand-written FFmpeg filter chain, and never report success
because a subprocess exited zero.

## Run it

```sh
python3 "$SKILL_DIR/scripts/process_audio.py" --input "<absolute-path>" --json
```

`$SKILL_DIR` is the directory containing this file. From an installed skill that is
`~/.config/opencode/skills/podcast-master` (`%USERPROFILE%\.config\opencode\skills\podcast-master`
on Windows). Resolve the path; never hard-code a checkout.

Check the runtime once per machine, or after an upgrade:

```sh
python3 "$SKILL_DIR/scripts/process_audio.py" --doctor --json
```

Exit codes: `0` verified success, `1` I/O or engine failure, `2` invalid request, `3` delivery
gate failure, `4` missing or incompatible dependency, `5` destination collision, `130` cancelled.
In `--json` mode stdout carries exactly one JSON object; progress goes to stderr.

## What the user gets

- The source is never modified. Its SHA-256 is verified before and after.
- Mono stays mono, stereo stays stereo, both channels preserved; no downmix, no mono collapse.
- Filler removal, silence trimming, word removal and time stretching are always off. Pauses and
  breaths stay where they are.
- Delivery profile `podcast`: -16 LUFS +/-1 LU and true peak at most -1.0 dBTP, both measured on
  the decoded final MP3. MP3 is LAME CBR 128 kbps mono / 256 kbps stereo.
- All processing is local. No cloud service, no account, no ASR.

"Maximum cleanup" means the strongest measured cleanup that preserves the voice. The engine
compares four bounded candidates on excerpts of the actual file and keeps the strongest one that
passes the preservation gates; on a clean recording that is no spectral suppression at all.

## When to ask the user

Ask only for these, and nothing about audio settings:

1. The input path is missing or matches several files.
2. The file has several audio streams, or more than two channels.
3. The destination `_processed.mp3` already exists. Report the collision and ask before rerunning
   with `--overwrite`. Never invent a numbered filename.

A request to change a microphone, an interface, a recording app or a default device is not a
mastering request. Do not run the skill for it.

## Read the result

The JSON has `ok`, `status`, `error`, `output`, `measurements`, `conformance`, `engine`,
`attempts` and `warnings`. Report to the user:

- the absolute output path, and its duration;
- `measurements.integrated_lufs` and `measurements.true_peak_dbtp` as measured on the MP3;
- one sentence on the automatic choices: `engine.chosen.profile`, and whether de-hum, de-essing,
  de-reverb and leveling were enabled;
- any warning that changes what they should know, such as `engine.warnings`, unrecoverable
  clipping in the source (`engine.analysis.clipped_samples`), or `engine.full_file_fallback`.

Never dump the whole report. Never claim to have listened to the audio: the evidence is
measurement, and `references/processing-policy.md` says exactly what each number proves.

## When it fails

- `status: collision` - the output exists; ask about replacement.
- `status: dependency_error` - run `--doctor --json`, fix what it names. Do not download tools,
  do not fall back to a WAV with an `.mp3` name, do not install anything silently.
- `status: quality_gate_failed` - the file could not meet the loudness and peak targets. The
  `attempts` array shows every measurement. Say so plainly; do not hand over the unverified file
  under the expected name.
- `status: invalid_request` - explain what is wrong with the input (empty, corrupt, several
  streams, unsupported channel count).

Details of every metric, threshold and rejection reason: `references/processing-policy.md`.
Shape of the JSON result: `references/report-schema.json`.
