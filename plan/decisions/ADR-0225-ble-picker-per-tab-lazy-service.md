# ADR-0225 — BlePickerViewModel per tab; one lazily created BleService

- **Status:** Accepted (amends the 0012 design sketch of 2026-10-10)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad

## Context

The 2026-10-10 sketch of 0012 introduced a `ChooseDeviceViewModel` owning the probe picker and a BLE picker. The
implemented shell (0004, 0021, 0028) gives each `DeviceTabViewModel` its own `ProbePickerViewModel` and the probe
list comes from one shared `ProbeScanner` in `AppViewModel`.

## Options considered

1. **A `BlePickerViewModel` per tab next to `ProbePickerViewModel`, a `transport` property on the tab, one shared
   `BleService` (BleHost + BleScanner) in `AppViewModel` (chosen).**
2. A new `ChooseDeviceViewModel` owning both pickers and the transport switch.

## Decision

- `DeviceTabViewModel` gets `transport` ("jlink" | "ble"), `blePicker` and `consoleSession()`; `session()` stays
  the J-Link `DeviceSession` (nullptr in BLE tabs) for operations.
- `BleService` (owned by `AppViewModel`, GUI thread) creates the `BleHost` on the first Bluetooth use (Bluetooth
  view shown or a BLE tab connects): users who never use Bluetooth never start the BLE thread or see the macOS
  permission prompt. Pickers hold a scan reference while visible; the one `BleScanner` scans while any holds it.
- The adapter state is polled without blocking the GUI thread (posted `QueryAdapter`, reply read on the next tick).

## Consequences

- The existing Choose-device page and its tests keep working; the Bluetooth view is an addition.
- The BLE list is reusable by the 0031 wizard (Embedded `ble-picker`) through the same view model.
