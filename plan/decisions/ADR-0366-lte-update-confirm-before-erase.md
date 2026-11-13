# ADR-0366 — The guided LTE update says what will happen and waits for Continue before it erases

- **Status:** Accepted (amends ADR-0234)
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (rc.4 testing, plan [0046](../features/0046-rc4-fixes.md))

## Context

With the tab already on the APP connector, the guided LTE update (ADR-0234) passes the APP step at once, backs up
and erases the application, and only then asks the user to move the cable to LTE. The user sees "not connected to
LTE" after the application is already gone. The erase must stay before the LTE step: the application firmware
resets the LTE MCU during flashing (ADR-0234, ADR-0273), and with one J-Link on APP the LTE MCU cannot be reached.

## Options considered

1. A confirmation step before anything is changed: the application will be erased, then the cable goes to LTE, then
   back to APP; Continue starts it (chosen).
2. Check the LTE MCU first (cable to LTE, back to APP, erase, LTE again): two extra cable moves.

## Decision

- After the APP connector is identified and before the backup, the procedure stops in a new step `confirm`. Its text
  lists the three moves ("The application on this CHESTER will be erased. Then you move the J-Link cable to the LTE
  connector, and at the end back to APP; the application is written back."). **Continue** goes on; **Cancel**
  ends the update with nothing changed.
- GUI wizard: the step has its own page with Continue / Cancel. CLI `lte flash --guided` and `remote host`: a
  `Continue? [y/N]` question on the terminal (no means cancelled, nothing changed; `--yes` answers yes where the
  command has it). A remote request: the customer confirms in the same way on the host (the joiner sees the phase
  `confirm`).

## Consequences

- One more click, but no surprise erase.
- The remote LTE update needs the customer's Continue in addition to Allow.
