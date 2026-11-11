# ADR-0337 — Remote support session ships in 2.0.0-rc.4

- **Status:** Accepted
- **Date:** 2026-11-11
- **Decided by:** Matej Strnad (rc.4 preparation)
- **Supersedes:** ADR-0326

## Context

ADR-0326 moved plan [0044](../features/0044-remote-support-session.md) to 2.1 so that it would not extend the 2.0.0
release candidate scope. The remote support session is now wanted in the next release candidate.

## Options considered

1. Keep 2.1 (ADR-0326).
2. Release it as a separate 2.1 pre-release from a branch.
3. Move the full plan 0044 (A1–D1) into 2.0.0-rc.4.

## Decision

Option 3: the full plan 0044 — console sharing, join approval, tunneled flash / erase / recover / reset, CLI, GUI,
translations, docs and the interop check against the local relay — is part of 2.0.0-rc.4.

## Consequences

- rc.4 is not tagged until plan 0044 passes its verification section, in addition to the release gates of
  `packaging/RELEASE.md`.
- The relay itself stays unchanged (ADR-0334); no backend-team work blocks rc.4.
- Strings in the plan that name "2.1.0" as an example version stay examples; the hello carries the real version.
