# ADR-0277 — CLI: `hwio chester lrw …` and `hwio stm32 … --chip`

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (plan 0039 planning session)

## Context

Every backend feature has a CLI command (CLAUDE.md). CHESTER targets are `chester app` and `chester lte`; chester-sdk
names the LoRaWAN subsystem `lrw` (`ctr_lrw`, shell `lrw …`).

## Options considered

1. **`hwio chester lrw flash|erase|unlock|reset|info` and `hwio stm32 chips|info|flash|erase|unlock|reset --chip ID`
   (chosen).**
2. The same with `hwio chester lora …`.

## Decision

- `chester lrw` mirrors `chester app` / `chester lte` options where they apply (`--halt`, `--yes`, probe options);
  `chester lrw flash` without FILE flashes the prepared firmware (as `chester lte flash`, ADR-0165).
- `stm32` takes `--chip ID` (a curated part id; `hwio stm32 chips` lists them) on every command except `chips`.
- `.bin` files take `--address` (default: the part's flash start).

## Consequences

- Plan 0006 command table gains both groups; `--json` output and exit codes follow plan 0006.
