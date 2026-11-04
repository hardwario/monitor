# ADR-0261 — J-Link firmware update: a failure cancels a pending quit; Unknown status asks and trusts the vendor call

- **Status:** Accepted (plan 0036)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, in chat, after the independent review of the 0036 implementation)

## Context

Plan 0036 makes quitting wait for a running J-Link firmware update. The implementation quit as soon as the update
ended, also when it failed, so the user never saw `probe.firmware_update_failed` and its advice (J-Link Commander can
repair the firmware). Separately, a probe whose firmware status is `Unknown` (the firmware strings cannot be read or
compared) went to the normal confirmation, and `JLinkProbe::updateFirmware()` judged the result by re-reading the
status, which stays uncomparable, so a successful update was reported as `probe.firmware_update_failed`.

## Options considered

1. **A failed update cancels the pending quit and shows the error; Unknown gets an explicit "Update anyway?" and its
   result is judged by the vendor call's return value (chosen).**
2. Quit anyway after a failure; treat Unknown like Outdated (the behaviour before the review).

## Decision

- When the update fails while a quit is pending, the quit is cancelled (`AppViewModel` and
  `ProbeFirmwareViewModel::quitPending` are reset) and the dialog shows `probe.firmware_update_failed` with its advice;
  the application stays open.
- When the inspection reports `Unknown`, the confirmation dialog says "The J-Link firmware status could not be
  determined. Update anyway?". For that path `JLinkProbe::updateFirmware()` does not compare the status after the
  update: the return value of `JLINKARM_UpdateFirmwareIfNewer` decides (0 = the library had nothing to write, reported
  as "already up to date"; otherwise updated). A probe that does not come back is still `probe.firmware_update_failed`.

## Consequences

- The user always sees why an update failed, even after asking to quit; quitting needs a second request.
- `hwio jlink update-firmware` on an `Unknown` probe no longer reports a false `probe.firmware_update_failed` (shared
  backend).
