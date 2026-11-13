# ADR-0331 — CLI: hwio remote host / hwio remote join

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

`hwio remote host` (the device selection options of `chester app console` / `ble console`, `--relay`,
`--deny-operations`, `--json`) and `hwio remote join <link>` (interactive console, `--command CMD`,
`--flash FIRMWARE_ID | --erase | --recover | --reset` with `--target app|lte`, `--relay`, `--json`). English only.

## Consequences

The names describe the function, not the support use case, so other uses (a colleague's help) fit as well.
