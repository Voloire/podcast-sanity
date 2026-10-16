# Executor prompt for the 2026-10-16 plan

Paste the text below as the first message to the implementing agent. It is self-contained: the agent needs no other context than the repository.

---

You are implementing a reviewed, task-by-task plan in the repository at `C:\Users\Voloirex\podcast-sanity` (Windows 11, PowerShell 5.1, Visual Studio 2019 Build Tools, Qt 6.5.3 at `C:\Users\Voloirex\deps\qt`, vcpkg at `C:\Users\Voloirex\deps\podcast-vcpkg`, WiX 3 at `C:\Users\Voloirex\deps\wix3`, CMake 4.4 on PATH).

Read these two files completely before doing anything else:

1. `docs/superpowers/plans/2026-10-16-review-fixes-and-studio-release.md`: the plan. Its "Review findings" section is the specification; every task cites the finding IDs it closes; every task ends with a commit message you must use verbatim.
2. `docs/TESTING.md`: how the three existing test suites work.

Then execute the plan in order: Task 0.1, then Phase 1, Phase 2, Phase 3, Phase 4, Phase 5, Phase 6, Phase 7. Phases 3 to 6 are independent of each other but each depends on Phase 2 being complete; do them in the order written unless told otherwise.

Rules that override anything else you may be inclined to do:

- Work on branch `review-2026-10-16` (created in Task 0.1 from `master`). One commit per task, message exactly as the plan gives it. Never amend, never squash, never push to `master`, never merge. Git remotes stay SSH.
- Before every commit run the full test suite: `cmake --build build-windows --config Release --parallel` then `ctest --test-dir build-windows -C Release --output-on-failure`. All tests must pass. If a test fails and the task did not say it would, stop and report the exact failing line; do not change thresholds, tolerances or expected values to make a test pass unless the task explicitly says so (only Task 2.8 does).
- Follow the steps literally. Where a step gives code, use that code. Where a step says "stop and report", stop and report. Where the expected output differs from what you observe, report the difference before continuing.
- Every artifact you write is English: code, comments, commit messages, docs. User-facing GUI strings stay Italian as they are. No AI, model, vendor or assistant is ever named as author, co-author or contributor anywhere: not in commits, not in files, not in the pull request. Do not add `Co-Authored-By` or "Generated with" lines.
- Do not touch anything outside this repository. Do not read or write the network at runtime or in tests; the only download the plan allows is the Ubuntu font package in Task 5.1, done once by you at build time.
- Do not "improve" code the task does not name. Side findings go in one line in your final report.
- Tooling notes: `rg` is not installed (use `Select-String`); PowerShell 5.1 has no `&&`; Git Bash converts paths and eats backslashes in unquoted heredocs, so prefer PowerShell for files and processes.

Reporting:

- After each task, one line: task number, commit hash, ctest summary line (for example `100% tests passed, 0 tests failed out of 9`).
- When a task requires a hand check (screenshots, MSI install, listening to the preview), do the check, say what you observed, and attach the screenshot paths.
- Final report, at most 300 words, result first: branch state (`git status --short` must be empty, all commits pushed), the PR URL from Task 7.2 (open the PR, do not merge it), the final ctest summary, the ffmpeg cross-check value from Task 1.1, the MSI size before and after Task 6.1, anything you skipped and why, and the "Decisions for the owner" list from the end of the plan with what you assumed.

Communication with the owner is in Italian; everything written into the repository is in English.

Start with Task 0.1.
