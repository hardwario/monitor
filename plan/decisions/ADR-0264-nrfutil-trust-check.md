# ADR-0264 — Trust check before running a downloaded nrfutil

- **Status:** Accepted (plan 0038, D3)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

Running a user-chosen file is code execution on the user's behalf. Nordic updates the file, so hashes cannot be pinned.

## Options considered

1. Explicit user action with the path shown, sanity checks, post-run version check.
2. Option 1 plus platform signature checks (Windows Authenticode signer, macOS Team ID).
3. Pinned SHA-256 hashes (impossible: the file changes).

## Decision

Option 1 first (rc.4), option 2 in part B: explicit user action with the full path shown; sanity checks (regular file,
not a symlink, 1–300 MB, owned by the current user and not group/world-writable on POSIX); after the run, the located
nrfutil must pass `checkVersions()`. Part B adds the Windows Authenticode signer and macOS Team ID check before
running; a mismatch is an error, not a warning. No hash pinning.

## Consequences

Until part B the app trusts the user's choice like a terminal would; part B adds platform code in `libs/hw/nrfutil`
(WinVerifyTrust / Security.framework or a `codesign` subprocess).
