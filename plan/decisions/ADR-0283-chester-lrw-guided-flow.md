# ADR-0283 — CHESTER LoRaWAN module: guided flow with the application MCU held; no "Check module" in rc.4

- **Status:** Accepted
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (plan 0040 Phase 0 findings)

## Context

Plan 0040 Phase 0 (2026-11-06): the Murata ABZ module on CHESTER answers on SWD (its own "LoRaWAN" connector) only
while the CHESTER application MCU (nRF52840) is held in reset + halt; the running application firmware drives the
module's reset / power. A reset + halt over the APP connector, then moving the cable, made the module reachable.
"Check module" (ADR-0281 D4) needs a shell command that queries the modem; the only one found
(`lrw test cmd AT+VER?`) works only after `lrw config test true`, a configuration change.

## Options considered

- Flow: **guided cable moves, as the LTE update (ADR-0234) (chosen)** / flash only, the docs explain how to hold
  the application MCU.
- Check module: **hidden in rc.4 (ADR-0281 fallback) (chosen)** / turn test mode on briefly.

## Decision

- An `LrwUpdateProcedure` (libs/device, like `LteUpdateProcedure`) drives the module operations: APP connector →
  reset + halt of the application MCU (nothing erased) → the user moves the cable to the LoRaWAN connector →
  identify the module (RDP 1: the unlock needs its own confirmation, ADR-0276) → flash / erase / unlock → the user
  moves the cable back → reset (go) of the application MCU. The GUI LoRaWAN card and `hwio chester lrw flash |
  erase | unlock` use it (interactive terminal); `--direct` skips the guidance for scripts (the caller has held the
  application MCU and the cable is on the LoRaWAN connector). `hwio chester lrw info` and `reset` connect directly
  and, when the module does not answer, say how to hold the application MCU.
- No "Check module" button in rc.4; a later plan adds it when a read-only modem query exists.
- Amended 2026-11-10 (plan 0040 D9, user): the GUI offers no guided Reset of the module; the final application
  reset restarts it anyway (the CLI `hwio chester lrw reset` stays, connecting directly).

## Consequences

- Supplements ADR-0234 (the LTE procedure now names its targets: `lte` / `lrw`, not "the first other target").
- Amends plan 0040 UX ("cable-move prompts"), D4 (hidden) and G5.
- Amended 2026-11-10: `LrwUpdateProcedure` has no Reset action; the `lrw` maintenance shows Erase chip… and
  Unlock (erase)… only.
