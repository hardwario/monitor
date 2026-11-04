# ADR-0249 — The Connect wizard is J-Link only, application MCU only, without a Device step

- **Status:** Accepted (plan 0035)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

Plan 0009 (2026-10-16) starts the Connect wizard with a Device step (profile + target `app` / `lte`). Since then a tab
always uses the console target (ADR-0233), LTE work goes through the `lte-update` wizard (ADR-0234), and the Choose
device page has a J-Link / Bluetooth switch (0012). There is one J-Link profile (CHESTER).

## Options considered

1. **J-Link only, application MCU only, no Device step (chosen).** The Cable step shows the console target's
   connection guide; a J-Link that answers from the LTE MCU leads to "move the cable" or [Update LTE firmware…].
2. J-Link + Bluetooth (a transport step; the Bluetooth branch reuses the BLE list and the pairing help of 0012).
3. Keep the 0009 Device step with the target choice.

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04). The Device step reappears automatically only when a second J-Link profile exists
(the same rule as the Choose device page, plan 0033 D1).

## Consequences

- 0009's Device step and its target cards are removed; `target.wrong_family` / `target.other_connector` on the Check
  step lead back to the Cable step or hand off to `lte-update`.
- Bluetooth users are guided by the 0012 pairing help (and later by 0032, ADR-0250).
