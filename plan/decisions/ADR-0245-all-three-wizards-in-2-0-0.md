# ADR-0245 — All three M4 wizards ship in 2.0.0

- **Status:** Accepted
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2.0.0 planning, plan 0035)

## Context

Plans 0008 (first-run setup), 0009 (connect device + troubleshooting) and 0010 (guided flash) were approved on
2026-10-16 but not implemented. Since then the code changed under them: one target per tab (ADR-0233), the guided LTE
firmware update (ADR-0234), expert maintenance actions (ADR-0236), the BLE GUI (0012) and the `hwio` rename (0024).
Users of rc.2 meet raw `dependency.*` and `probe.*` errors with a disabled "Help me fix this" button, and the user
documentation says "the setup wizard is not available yet".

## Options considered

1. **All three wizards in 2.0.0, re-baselined against the current code by plan 0035 (chosen).**
2. Only the setup wizard in 2.0.0, the others after the release.
3. No wizard in 2.0.0.

## Decision

- 0008 first-run setup, 0009 connect + troubleshooting and 0010 guided flash ship in 2.0.0.
- 0010 covers **application firmware only**; LTE firmware is updated only through the existing `lte-update` wizard
  (ADR-0234), to which 0010 hands over.
- Plan 0035 amends 0008/0009/0010 against the current code; the three plans need re-approval with those amendments.

## Consequences

- 2.0.0 waits for three more GUI plans, their translations (ADR-0248) and their manual checks on three OSes.
- The probe picker must be reusable inside a wizard step (plan 0035, phase A) before the wizards start.
- The user documentation (plan 0029 pages) is updated in five languages where it says the wizard is not available.
