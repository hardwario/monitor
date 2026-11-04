# ADR-0246 — J-Link firmware update in the GUI only on a disconnected probe

- **Status:** Accepted (supersedes the session pause/resume part of ADR-0132 and of plan 0017 § B)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2.0.0 planning, plan 0036)

## Context

ADR-0132 decided that the GUI offers the J-Link firmware update and that an open session on the probe is detached,
the firmware updated and the session re-attached (`pauseForProbeMaintenance()` in 0017 § B). That needs a new
session state and cross-thread hand-over of the probe between the session worker and the update. The backend already
has the short-lived `device::updateProbeFirmware()` used by `hwio jlink update-firmware`, which opens the probe
itself under its `ProbeLock`.

## Options considered

1. **The tab must be disconnected first; the banner offers "Disconnect and update"; the update uses the existing
   `updateProbeFirmware()` (chosen).**
2. Pause/resume the session around the update (ADR-0132, 0017 § B).

## Decision

- The GUI updates the J-Link firmware only when no session of this process uses the probe.
- The banner for `probe.firmware_outdated` on a connected tab offers **[Disconnect and update…]**: the tab
  disconnects (as the device-bar Disconnect does), then the confirmation dialog of ADR-0132 opens.
- The update calls the existing `device::updateProbeFirmware()` (or a variant that does not touch the GUI-owned
  `SessionRegistry` off its thread, plan 0036); no session pause/resume, no new session state.
- The other parts of ADR-0132 stay (confirmation dialog, no downgrade offered for `Newer`, "Not now" per probe until
  restart, blocked while MCP or an operation holds the probe, error `probe.firmware_update_failed`, no MCP tool).

## Consequences

- No new threading in `DeviceSession`; plan 0017 step B1 is dropped.
- The user reconnects after the update (or the dialog does it, plan 0036 decision).
- The console of that tab stops during the update; its log file is closed and a new one starts on reconnect.
