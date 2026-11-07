# Podcast master implementation and local deployment

Status: Linux preview installed and usable in OpenCode. Not release-qualified.
Date: 2026-11-07.

## Delivered preview

- Canonical skill, references, schema and stdlib Python runner under
  `skills/podcast-master/`.
- Installer supports install, check, dry-run, backup/update and scoped uninstall.
- Renderer provides versioned capabilities, channel-aware analysis and
  channel-preserving automatic restoration with linked dynamics.
- Runner performs local decoding, bounded candidate selection, two-pass
  loudness finishing, actual LAME MP3 encoding, independent delivery checks,
  source hashing and atomic no-clobber publication.
- Existing GUI remains buildable and its smoke test passes.
- Installed on voloire-XPS in `~/.config/opencode/skills/podcast-master`, with
  renderer in `~/.local/lib/podcast-sanity/0.2.0/podcast-sanity-render`.
  No changes to `opencode.json` or Claude/Codex skill trees.
- OpenCode's `skill` tool successfully loaded the installed skill. This verifies
  discovery, not an independent model-trigger evaluation.

## Validation

`ctest --test-dir build-skill --output-on-failure`: all five suites pass:
unit, auto_policy, skill_safety (six tests), skill_runner (eight tests), e2e.
Elapsed: 149.90 seconds on this machine.

`ctest --test-dir build-gui --output-on-failure -R gui`: passes, 9.79 seconds.

Installed-runner acceptance from `/tmp/opencode`, no explicit runtime flags:
`Épisode 07.take2.wav`, generated home corpus, eight seconds, becomes
`Épisode 07.take2_processed.mp3`. Independently metered delivery: -16.4 LUFS,
-2.8 dBTP, MP3 128 kbps, mono, 48 kHz, eight seconds. Selected preservation.
Synthetic speech only; no claim of listening or real-recording calibration.

Safety regressions cover collision preservation, refusing non-atomic fallback,
not stealing old locks, transactional uninstall refusal, uninstall cleanup,
configured tool resolution, report/source aliasing and silence failure.
The speech-band metric now uses after-minus-before so loss is negative.

## Release blockers and remaining implementation

Do not mark the original eight-phase plan complete or merge as a finished
cross-platform release on the strength of the preview tests.

1. Whole-file in-memory DSP has no measured working-memory budget. The required
   60-minute stereo test at <=4 GiB RSS has not been performed. Refactor buffer
   ownership and/or process in bounded blocks before claiming long-file safety.
2. No authorized real speech recordings or listening evaluation have been used.
   Preservation thresholds remain provisional; synthetic selection tests do not
   establish the required reference SNR/SI-SDR acceptance for the new policy.
3. Windows build, Unicode I/O, deployed DLLs and cancellation remain unverified.
   CI was extended but has not yet provided Windows evidence.
4. Add cancellation/resource-failure tests, broader codec/rate/channel fixtures,
   non-finite candidate rejection and full-file fallback regression coverage.
5. Subprocess lifecycle, structured invalid-argument JSON, unavailable metrics,
   diagnostic retention and stage/resource reporting need further work.
6. Deliver standalone headless runtime bundles, scheduled/manual resource CI and
   isolated with/without-skill interaction evaluations. Current packaging only
   includes the source skill in the existing application packages.

The local installation is a preview for short recordings, not evidence that
these release gates passed. No user recordings were processed during testing.
