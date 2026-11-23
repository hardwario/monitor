# ADR-0381 — After a cancelled LTE update the application copy can be written back, listed and found

- **Status:** Accepted
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (rc.4 issues, plan [0051](../features/0051-rc4-issues-5-11.md))

## Context

Issue #8: the guided LTE update saves a copy of the application before the erase. Cancelling after the erase ends the update; the copy stays in `<AppLocalData>/backups/<serial>-<UTC>.hex`, but nothing in the GUI or CLI offers it.

## Options considered

1. Ask on cancel after the erase whether to write the application back (chosen).
2. Offer the copies in the application firmware picker and in the CLI (chosen).
3. An Open backups folder action (chosen).

## Decision

- Cancel after the erase with a copy: "Write the application back first?" — Write back (connect APP, write the copy, end as cancelled with the application restored) · Leave it erased · Back. A remote-started update keeps ADR-0368 (written back without asking).
- The application `SourceCard` (Flash page, step 7) shows "Application copy from %1" for the connected device's serial (up to 10, newest first) and **Open backups folder**; application file dialogs start there when copies exist.
- CLI: `hwio chester app backups [--serial N] [--json]`, `hwio chester app flash --backup latest|PATH` (serial mismatch refused unless `--force`); `lte flash --guided` asks "Write the application back first? [Y/n]" on cancel after the erase.
- Backend: `ApplicationBackup::list()`, procedure step `offerRestore`.

## Consequences

A cancelled update no longer leaves the user without an application. One more question on cancel; one more row in the picker.
