# ADR-0239 — Release notes come from a file in the repository

- **Status:** Accepted (supersedes the "generated notes" detail of ADR-0125)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (user-approved fix, plan 0034, B3)

## Context

ADR-0125 creates the draft GitHub release with generated notes (`gh release create --generate-notes`, a list of
commits). The notes for v2.0.0-rc.2 were written by hand in `packaging/release-notes/v2.0.0-rc.2.md`, and the
release checklist (`packaging/RELEASE.md`, ADR-0237 product name check) assumes hand-written notes.

## Options considered

1. **Notes from `packaging/release-notes/<tag>.md` (chosen).** Reviewed in a pull request like any other text,
   checked by `product_name_check`.
2. Generated notes, edited in the draft by hand: easy to forget, not reviewed, not checked.

## Decision

`release.yml` creates the draft release with `--notes-file packaging/release-notes/<tag>.md`. The gate job fails at
once with a clear message when that file is missing or empty. The file is committed before tagging.

## Consequences

- Every release tag needs its notes file first (`packaging/RELEASE.md` checklist).
- The publish job checks out `packaging/release-notes` (sparse, no persisted credentials).
