# ADR-0360 — Expert mode replaces "Show expert maintenance actions"

- **Status:** Accepted (amends ADR-0236)
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (plan [0045](../features/0045-user-modes.md), D-2)

## Decision

The switch `advanced/expertActions` is removed. Erase everything, Recover and the LTE MCU maintenance are shown in
Expert mode only. The protected-MCU hand-off of ADR-0236 (Recover shown once for that MCU) works in every mode. The
old key is read only to preselect the mode for upgrading users (ADR-0362) and removed once the mode is chosen.

## Consequences

One concept instead of two; a user who wants the expert actions also sees the timing options and the support server.
