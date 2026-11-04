# ADR-0235 — BLE device list: smoothed RSSI with hysteresis, steady while pointed at

- **Status:** Accepted (amends ADR-0083 and ADR-0129: the order of the device list)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (rc.2 feedback, plan 0033)

## Context

The BLE pickers sort by the last RSSI. With tens of CHESTERs around (production, lab), rows swap places every scan
interval and the row under the pointer changes just before a click. The HIO Manager mobile app solves this with
easing and averaging.

## Options considered

1. **Smoothed RSSI + hysteresis, profile devices first, order frozen while pointed at (chosen).**
2. Order by name; RSSI only shown.
3. Order of first discovery with a manual "sort by signal" button.

## Decision

- `BleScanner` keeps a time-based exponential moving average of the RSSI per device and sorts by it (CLI and GUI).
- The GUI pickers move a row above another only when its smoothed RSSI is higher by at least the hysteresis
  margin; devices with a profile come before the others ("Show all devices").
- While the pointer is over the list or a row has keyboard focus, the order is frozen; new devices are appended.
- Parameters (time constant, margin) are in plan 0033.

## Consequences

- The strongest devices still come first, without jumping.
- The shown RSSI is the smoothed value (rounded), so it flickers less too.
