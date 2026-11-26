# ADR-0426 — "Save view…" redacts device secrets and creates an owner-only file

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, as recommended) (plan [0060](../features/0060-test-feedback-2026-11.md), D6)

## Context

ADR-0186 and its amendments keep secrets out of console files (`claim token: <redacted>`, sensitive command
prefixes) and create them owner-only (0600). "Save view…" (ADR-0201) writes the rows as shown, with the claim
token and the BLE passkey readable, and with the default mode (0664 with a usual umask). The saved file is what
users attach to support requests (ADR-0075).

## Options considered

1. **Redact like the console file (sensitive output keys and command prefixes of the profile) and create the file
   owner-only.**
2. Owner-only only ("save what I see").
3. Redact only.

## Decision

Option 1 (accepted 2026-11-24). The screen and Copy keep showing the values.

## Consequences

- The owner-only helper moves from `libs/device` (private) to a public header usable by `app/gui`.
- A user who needs the secret in a file copies it from the screen.
- Implementation (plan 0060 A4): one redaction for console files and Save view
  (`hwio::device::ConsoleRedaction`, `libs/device`); the owner-only helper is `hwio::restrictToOwner`
  (`hwio/core/owner_only.h`), set on the `QSaveFile`'s temporary file before the first write. Windows: no mode bits;
  the file gets the ACL of the folder the user chose (Documents by default, per-user) — no ACL code (open point).
