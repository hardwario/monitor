# ADR-0265 — Executable bit and macOS quarantine of a downloaded nrfutil

- **Status:** Accepted (plan 0038, D4)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

A browser download is not executable on macOS/Linux and is quarantined on macOS. Whether Nordic's macOS binary is
notarized is not verified yet (plan 0038 Phase 0 spike).

## Options considered

1. **The app adds owner-execute after the click; it does not remove `com.apple.quarantine`; if Gatekeeper blocks,
   it shows the "Open Anyway" instructions (chosen).**
2. The app removes the quarantine attribute after the click.
3. Instructions for Terminal only (`chmod`, `xattr -d`).

## Decision

Option 1. The docs (rc.3) tell macOS users to allow nrfutil in **System Settings › Privacy & Security › Open
Anyway** if macOS blocks it. Revisited after the Phase 0 spike.

## Consequences

No security marker is removed silently; one extra manual step if Nordic's binary is not notarized.
