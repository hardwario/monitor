# ADR-0255 — Where the GUI offers the J-Link firmware update

- **Status:** Accepted (plan 0036; amends the placement list of ADR-0132)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

ADR-0132 lists three places: the device-bar banner, the doctor result row `probe-firmware`, and the troubleshooting
topic `probe-firmware`. The GUI has no doctor page; the `probe-firmware` check appears in the setup wizard's test
step (0008). ADR-0246 requires a disconnected tab.

## Options considered

1. **Banner on the tab (`probe.firmware_outdated` warning) + the `probe-firmware` row of the setup wizard's test
   step (0008) + the troubleshooting topic `probe-firmware` (0009) (chosen).**
2. Banner only.
3. Banner + a button on every Choose device row with outdated firmware.

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04).

## Consequences

- One shared action (`ProbeFirmwareViewModel::requestUpdate`) used from three places; the hooks in 0008/0009 appear
  only when 0036 is implemented.
- The CLI command `hwio jlink update-firmware` stays shown as the alternative in the topic.
