# 0032 — BlePairing wizard and the setup-wizard Bluetooth step

- **Status:** Draft (split out of 0012 on 2026-11-03, ADR-0227; content = the 2026-10-10 text of 0012 G4–G6)
- **Milestone:** M5 follow-up
- **Author:** Matej Strnad (with Claude), 2026-11-03
- **Related ADRs:** ADR-0067, ADR-0076, ADR-0077, ADR-0084, ADR-0086, ADR-0088, ADR-0227
- **Depends on:** [0012](0012-m5-ble-gui-pairing.md) (BLE picker and tab), [0008](0008-m4-first-run-setup.md) (setup
  wizard), [0009](0009-m4-connect-and-troubleshooting.md) (topic table, coverage test) — both Approved, not
  implemented on 2026-11-03

## Context

0012 brings Bluetooth into the Choose-device page and the device tab with pairing help in the ErrorPanel
(ADR-0227). The guided parts decided in ADR-0084 and ADR-0088 need the concrete wizards of 0008 / 0009 and are
collected here. Revise this plan against the code once 0008 and 0009 are done.

## Scope (from the 2026-10-10 text of 0012)

### BlePairing wizard (`ble-pairing`, per tab; ADR-0084)

Steps (ADR-0067 kinds): Bluetooth · Device · Pair · Test · Done.

1. **Bluetooth** [Checks: `bluetooth`] — adapter present, on, permission (macOS). macOS not decided → [Allow
   Bluetooth access]; denied → [Open Privacy settings]. Skipped when Ok and opened from a device error.
2. **Device** [Info, slot `ble-enable`, + Embedded `ble-picker`] — "Make sure the device is powered and its
   Bluetooth is on." (profile `pairingGuide`); the BLE list of 0012 (read-only selection). Preselects the tab's
   device.
3. **Pair** [Info + Commands, slot `ble-pair-os`] — Linux: [Pair] + headless `bluetoothctl` commands; Windows:
   [Pair] + [Open Bluetooth settings]; macOS: [Connect]. Passkey hint box (profile `passkeyHintSourceText`); with a
   connected J-Link CHESTER tab: [Read passkey from that device] (PIB read, never stored or logged). "Already paired
   but it does not work?" manual note.
4. **Test** [Checks] — "Connected", "Console service found", "Paired (encrypted)", "Shell answers" (≤ 3 s).
5. **Done** — [Open console] · [Close].

Start step by error: `ble.adapter_*`, `ble.stack_unavailable` → 1; `ble.device_not_found`, `ble.connect_failed`,
`ble.disconnected` → 2; `ble.not_paired`, `ble.pairing_failed` → 3; `ble.service_not_found`, `ble.write_failed`,
`ble.write_timeout` → 4 (+ "The firmware may not have the Bluetooth console." [Flash firmware…] when 0010 is
registered). Help menu: "Pair a Bluetooth device…".

### Setup wizard Bluetooth step (amends 0008, ADR-0088)

After nRF Util: **Bluetooth (optional)** [Checks: `bluetooth`]; macOS `notDetermined` → [Allow Bluetooth access];
denied → [Open Privacy settings]; [Skip] default when not Ok; Done summary "Bluetooth ✓ / – skipped".
`ble.permission_denied` ("Help me fix this") starts the setup wizard at this step.

### Also

- Illustration slots `ble-enable`, `ble-pair-os` (ADR-0076, placeholders in LFS), mockup of the wizard.
- 0009 coverage test extended: every BlePairing code maps to a start step; `ble.permission_denied` to the setup
  step. New Embedded step name `ble-picker` (0007 contract).
- When registered, the ErrorPanel's "Help me fix this" appears for `ble.*` errors in the 0012 picker and tab; the
  0012 [Pair] action stays as the quick path.

## Decisions requiring user confirmation

To be reviewed when 0008 / 0009 are implemented (the 2026-10-10 decisions ADR-0084 and ADR-0088 stand).

## Implementation steps

- [ ] W0 Prerequisite check: 0008, 0009, 0012 done
- [ ] W1 `BlePairingWizard` (steps, OS variants, start mapping, PIB passkey read) + coverage test + GTest
- [ ] W2 Setup wizard Bluetooth step + GTest
- [ ] W3 Illustration placeholders (LFS), wizard mockup, Czech translation, accessibility
