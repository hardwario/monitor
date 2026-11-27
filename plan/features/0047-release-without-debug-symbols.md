# 0047 — Releases without debug symbol archives

- **Status:** Done
- **Milestone:** 2.0.0-rc.5
- **Author:** Claude (with Matej Strnad)
- **Related ADRs:** ADR-0370 (supersedes ADR-0232), ADR-0125
- **Depends on:** —

## Context

The user asked to remove the debug symbol archives from the release (rc.4 review). ADR-0370 records the
decision: the archives stay workflow artifacts for 90 days and are no longer release assets.

## Goals / Non-goals

- Goal: future tag builds create a draft release with only the four packages and `SHA256SUMS`.
- Goal: symbols are still built and kept as workflow artifacts (90 days).
- Non-goal: a long-term symbol store (separate decision before 2.0.0).

## User experience

The release download list shows the AppImage, the `.deb`, the DMG, the MSI and `SHA256SUMS`. The release
notes have no line about debug symbols.

## CLI

None.

## Design

`.github/workflows/release.yml`:

- Linux job: upload `build/packages/*-debug-symbols-linux.zip` as its own artifact `debug-symbols-linux`
  (`retention-days: 90`, `if-no-files-found: error`) and drop it from the `release-linux` artifact.
- macOS and Windows jobs: rename the artifacts `release-debug-symbols-<os>` to `debug-symbols-<os>` (so the
  publish job's `pattern: release-*` no longer picks them up) and add `retention-days: 90`.
- Publish job: the presence check lists only the four packages (step renamed "Every package");
  `SHA256SUMS`, the attestation and the draft release then cover only the packages and `SHA256SUMS`.

Docs: `packaging/RELEASE.md` (asset list and where the symbols are), `packaging/release-notes/v2.0.0-rc.4.md`
(drop the debug-symbols line, matching the published release). Nothing in the code fetches symbol archives.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Scope | rc.4 only / rc.4 and future / all releases | rc.4 and future (2026-11-13) | ADR-0370 |
| rc.4 `SHA256SUMS` | keep / regenerate without archives | regenerate (2026-11-13, done by hand) | ADR-0370 |

## Implementation steps

- [x] rc.4: delete the three archives, upload a new `SHA256SUMS`, remove the notes line (done by hand)
- [x] `release.yml` changes from Design
- [x] `packaging/RELEASE.md` and the rc.4 notes file

## Tests and verification

- `actionlint` (if installed) or a YAML parse of `release.yml`; review the diff of the three upload steps and
  the publish job.
- The next tag build (rc.5): the draft release has 5 assets; the run has artifacts `debug-symbols-{linux,macos,windows}`.

## Risks and open points

- Symbols older than 90 days are lost unless saved; the rc.4 archives exist in run 43407196438 until early
  February 2027.
