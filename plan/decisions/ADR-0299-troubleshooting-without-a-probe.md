# ADR-0299 — Troubleshooting on a tab without a J-Link probe

- **Status:** Accepted (plan 0009)
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (2026-11-09, question Q3 of the 0009 implementation)

## Context

Help → Troubleshoot connection… can be opened on an empty Choose device tab or on a Bluetooth tab. Topics with
Diagnose steps need a J-Link session; `DeviceTabViewModel::ensureSession` fails there ("no probe chosen"), and the
wizard is modal, so the user cannot choose a probe on the page.

## Options considered

1. **Insert the Embedded `probe-picker` step before the topic's first Diagnose step (as in the Connect and Flash
   wizards) and set the tab to the chosen probe (chosen).**
2. The Diagnose rows read "Not checked: no J-Link chosen" with the hand-off [Connect a device].
3. Show only the topics that need no probe.

## Decision

Option 1 for a tab without a probe. On a Bluetooth tab the J-Link topics use option 2: the Diagnose rows are Skipped
with "Not checked: no J-Link chosen" and the step offers [Connect a device] (`open-wizard:connect-device`).

## Consequences

- The probe step is not shown again by Back (the tab has its probe from then on).
- A Bluetooth user is sent to the Connect wizard, which is J-Link only (ADR-0249).
