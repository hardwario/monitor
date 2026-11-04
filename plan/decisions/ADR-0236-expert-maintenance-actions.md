# ADR-0236 — Erase everything, Recover and LTE MCU maintenance only with the expert setting

- **Status:** Accepted (amends ADR-0038 and ADR-0049)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (rc.2 feedback, plan 0033)

## Context

Erase everything removes the UICR and the Product Information Block (serial number, hardware version); on nRF52 the
fast erase takes the UICR with it. Testers called the buttons a loaded gun, even behind the red confirmation, and
developers rarely need them. Erase application (code only) covers the everyday case.

## Options considered

1. **Hide them unless Settings → Advanced → expert actions is on (chosen).**
2. Move them into a "⋯" menu inside Maintenance.
3. Keep them as in rc.2.

## Decision

- New Settings section **Advanced** with the switch "Show expert maintenance actions" (off by default).
- Off: Maintenance shows Reset, Reset and halt, Erase application (application MCU).
- On: also Erase everything…, Recover… (with the ADR-0038 confirmation) and the LTE MCU maintenance.
- A protected MCU (`target.protected`): the troubleshooting hand-off opens the Flash page with Recover… shown for
  that MCU this once, without changing the setting.
- The CLI and MCP are unchanged.

## Consequences

- Fewer accidental erases of the PIB / UICR.
- Documentation must say where the actions went.
