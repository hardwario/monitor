# ADR-0373 — list_devices lists BLE devices like `ble scan`

- **Status:** Accepted
- **Date:** 2026-11-13
- **Decided by:** Matej Strnad (plan 0050)

## Context

`list_devices` (ADR-0099) returned every BLE advertiser the GUI had seen: in the office ≈140 devices without a
profile, ~25 KB, on the first call of every MCP client. `hwio ble scan` shows only devices with a known profile
unless `--all` is given.

## Options considered

1. Like `ble scan`: open devices, J-Link probes and profiled BLE devices; `all_ble=true` lists everything (chosen).
2. No BLE devices unless asked (`ble=true`).
3. Everything, BLE capped to the strongest N with a total count.

## Decision

Option 1. `list_devices` and the `hwio://devices` resource list open devices, J-Link probes and BLE devices with a
known profile; the optional argument `all_ble=true` lists every BLE device. The result reports `ble_hidden`, the
number of BLE devices left out.

## Consequences

Small default result; same rule in CLI and MCP. Clients that want unprofiled devices must pass `all_ble`.
