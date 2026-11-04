# ADR-0250 — The BlePairing wizard (plan 0032) is deferred after 2.0.0

- **Status:** Accepted (plan 0035)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

ADR-0245 ships the setup, connect/troubleshooting and flash wizards in 2.0.0. Plan 0032 (BlePairing wizard and the
setup-wizard Bluetooth step) depends on 0008/0009 and is still a Draft copied from 2026-10-10. 0012 already gives
Bluetooth users inline pairing help in the ErrorPanel (ADR-0227). Twelve `ble.*` codes name the BlePairing wizard and
`ble.permission_denied` names FirstRunSetup (for the Bluetooth step that 0032 adds).

## Options considered

1. **Defer 0032 after 2.0.0 (chosen).** The 0009 coverage test excludes codes whose wizard is BlePairing (the
   button stays hidden while that wizard is not registered); `ble.permission_denied` gets no wizard until 0032 adds
   the setup step (its ErrorPanel keeps [Open Privacy settings]).
2. Ship 0032 in 2.0.0 as a fourth wizard (after 0008 and 0009).

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04).

## Consequences

- 2.0.0 ships all three M4 wizards (ADR-0245) but not the Bluetooth pairing wizard; the troubleshooting topic list
  gets a manual "Bluetooth device not found or pairing fails" topic derived from the docs.
- 0032 restores `ble.permission_denied` → FirstRunSetup and extends the coverage test when it is implemented.
