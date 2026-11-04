# ADR-0237 — The product name is "HARDWARIO Monitor"

- **Status:** Accepted
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (rc.2 feedback, plan 0033)

## Context

The app, bundle, installers and DMG say "HARDWARIO Monitor"; the README and the rc.2 release notes say
"HARDWARIO Monitor 2". Testers asked which one is right.

## Decision

- The name is **HARDWARIO Monitor** everywhere. The "2" appears only inside a full version number
  ("HARDWARIO Monitor 2.0.0") or as "version 2".
- The rule goes into `packaging/RELEASE.md` (release notes) and the documentation style notes.

## Consequences

- README, docs and future release notes are adjusted; app identity, bundle ids and paths stay.
