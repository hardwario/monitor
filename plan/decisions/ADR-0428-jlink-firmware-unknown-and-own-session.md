# ADR-0428 — J-Link firmware status "unknown" is "could not be verified"; the GUI checks its own probe

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, as recommended) (plan [0060](../features/0060-test-feedback-2026-11.md), D8 and D9)

## Context

When the probe's firmware cannot be compared with the library's (`GetEmbeddedFWString` failed, seen with a J-Link
EDU Mini on 9.82 before `ee27f96`), the diagnostics show a green "Not known" (review R11: nothing is known to be
wrong), and `hwio jlink update-firmware` reports "up to date" when the library wrote nothing (ADR-0261). The GUI's
diagnostics also skip a probe held by the GUI's own console ("Not checked: the J-Link is in use"), although the
session read the status when it opened the probe.

## Options considered

- Unknown: **neutral "Could not be verified" (skipped status), CLI update text says the version was not compared,
  `--json` `verified: false`** / warning / keep green.
- Own probe: **the check uses the status the GUI's session read at open** / keep "Not checked".

## Decision

The bold options (accepted 2026-11-24).

## Consequences

- `DeviceSession` keeps the firmware status for every result (not only Outdated / Newer); the check gets an
  optional lookup from the GUI. The CLI (another process) still reports a held probe as in use.
- ADR-0261's flow is unchanged; only texts and the check status change.
