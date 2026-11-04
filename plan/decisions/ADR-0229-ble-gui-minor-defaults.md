# ADR-0229 — BLE GUI minor defaults

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad

## Context

Plan 0012 listed a row of minor defaults "pending" since 2026-10-10.

## Options considered

Accept as proposed / change individual values.

## Decision

Accepted as proposed:

- Signal icon thresholds −55 / −67 / −80 dBm (from the old app); RSSI 0 = unknown (no number).
- macOS: the device id (a CoreBluetooth UUID) is not shown in the row, only in the tooltip.
- `ui/lastTransport` default `jlink`.
- Scan restarted every 10 s while a Bluetooth picker is visible; rows dimmed "not seen recently" after 15 s,
  removed after 60 s; screen-reader announcements at most once per 5 s.
- Flash rail item in a BLE tab: disabled, tooltip "Flashing needs a J-Link. Open a J-Link tab to flash this
  device."
- BLE auto-reconnect default **off** (`ble/autoReconnect` = false; J-Link stays on); remembered per device (LRU 32).
- `BleHost` created lazily (ADR-0228).

## Consequences

A BLE tab that loses the device shows Disconnected with [Reconnect] unless the user enabled auto-reconnect.
