# ADR-0233 — GUI tabs always use the console target; the J-Link connector is recognised by the chip family

- **Status:** Accepted (amends ADR-0043, ADR-0055, ADR-0057 and ADR-0168)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (rc.2 feedback, plan 0033)

## Context

In rc.2 the user picks `app` or `lte` twice: on the Choose device page (and in the device bar) as the tab's console
target, and again on the Flash page as the kind of firmware. The orange connector warning then asks the user to go
back and switch the tab. Testers found this confusing. Almost everybody works with the application MCU; the LTE MCU
is touched only to update its firmware, and that needs the J-Link moved between connectors anyway (ADR-0057, Phase 0).

## Options considered

1. **The GUI drops the target choice; a tab always uses the profile's console target, and a J-Link on the other
   connector is recognised by the chip family (chosen).**
2. Keep the choice and only add a guided LTE update (ADR-0234).

## Decision

- The Choose device page and the device bar have no target picker. A J-Link tab always opens its session on the
  profile's console target (CHESTER: `app`). Restored tabs saved with another target open on the console target.
- When the J-Link answers with the chip family of another target of the profile (CHESTER: the LTE MCU), the tab does
  not start the console. It shows a banner: the J-Link is on the LTE connector; the console runs on the application
  MCU; [Update LTE firmware…] (ADR-0234) and [Retry].
- The Flash page keeps its two kinds; the orange connector warning is removed.
- CLI and MCP keep their `target` arguments (`hwio chester lte …`, MCP `connect(…, target?)`).

## Consequences

- One place to decide what to do with the LTE MCU (the LTE update wizard).
- LTE MCU maintenance in the GUI moves behind the expert setting (ADR-0236).
- The session must identify the chip family when it connects (one FICR / CTRL-AP read, as the flash workflows do).
