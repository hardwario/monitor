# ADR-0224 — BLE GUI in two plans; pairing help in the ErrorPanel until the wizard exists

- **Status:** Accepted (amends ADR-0084 for the time until plan 0031 is implemented)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad

## Context

Plan 0012 (written 2026-10-10) combined the BLE picker and tab with the BlePairing wizard (G4) and the setup-wizard
Bluetooth step (G5). In November 2026 the wizard framework (0007) exists, but no concrete wizard is registered:
0008 (first-run setup) and 0009 (connect / troubleshooting) are approved and not implemented. Bluetooth in the GUI
is a gap for the first release candidate.

## Options considered

Split:
1. **Phase 1 now (picker, tab, console, restore, a11y, Czech); G4/G5, the wizard illustrations and the PIB passkey
   read move to a new Draft plan 0031 that depends on 0008/0009 (chosen).**
2. Phase 1 now, G4/G5 stay in 0012 as a blocked phase 2.
3. Implement 0008/0009 first, then all of 0012.

Pairing help until the wizard:
1. **ErrorPanel actions (chosen).**
2. Text only.

## Decision

- 0012 covers the picker and the BLE tab; plan 0031 covers the BlePairing wizard, the setup Bluetooth step, the
  `ble-enable` / `ble-pair-os` illustrations and the PIB passkey read.
- Until 0031: for `ble.not_paired` and `ble.pairing_failed` the ErrorPanel's primary action is **[Pair]**
  (`BleSession::pair()`, then reconnect) on Linux and Windows and **[Connect]** on macOS; Windows has a secondary
  **[Open Bluetooth settings]** (`ms-settings:bluetooth`); the body shows the profile's passkey hint (default
  123456) and "Already paired but it does not work? Remove the device in the system Bluetooth settings and pair it
  again." The device bar shows "Waiting for pairing — enter the passkey in the system dialog" after
  `pairingPrompt`. `ble.permission_denied` on macOS has [Open Privacy settings].
- "Help me fix this" for `ble.*` errors appears automatically once 0031 registers the wizards (0007 rule).

## Consequences

- 0012 can reach Done without 0008/0009; BLE is usable in the GUI for the release candidate.
- Headless Linux users without a pairing agent get no `bluetoothctl` guidance in the GUI until 0031 (the CLI
  `hwio ble pair` and the docs cover it).
