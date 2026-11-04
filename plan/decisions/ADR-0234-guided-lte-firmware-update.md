# ADR-0234 — LTE firmware is updated by a guided procedure that stops the application MCU first

- **Status:** Accepted (amends ADR-0055, ADR-0174 and plan 0010)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (rc.2 feedback, plan 0033)

## Context

While the LTE MCU is being flashed, the application firmware notices that the modem does not answer and pulls its
RESET pin, so the LTE flash fails. The documented procedure (docs.hardwario.com, CHESTER, "LTE modem over J-Link")
is: J-Link on APP → `hardwario chester app erase` → J-Link on LTE → `hardwario chester lte flash` → J-Link back on
APP → flash the application. Halting the application MCU instead of erasing it is fragile: any reset (supercapacitor
boards, power glitch) starts the application again.

## Options considered

1. **A guided procedure that erases the application, updates the LTE MCU and then restores the application
   (chosen).**
2. The same with a halt instead of the erase.
3. Keep the Flash page as is and only document the procedure.

## Decision

- The LTE kind of the Flash page starts the **LTE firmware update** wizard (wizard framework, plan 0007):
  APP connector → keep a copy of the application → erase application (code only, UICR and PIB kept) → LTE
  connector → update the LTE MCU → APP connector → write the application back → reset.
- Each connector step waits until the J-Link answers with the expected chip family; the user does not confirm it by
  hand.
- The details (backup, CLI form) are in plan 0033.

## Consequences

- The LTE update works on boards where it used to fail randomly.
- The device runs its previous application afterwards without a catalog download.
- The tab's console is held (no auto-reconnect) while the wizard runs.
