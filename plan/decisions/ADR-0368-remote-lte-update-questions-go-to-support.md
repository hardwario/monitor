# ADR-0368 — In a remote LTE update the questions go to support, the copy is always written back

- **Status:** Accepted (amends ADR-0363 and ADR-0366)
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (rc.4 testing, plan [0046](../features/0046-rc4-fixes.md))

## Context

A remote LTE update (ADR-0363) runs the guided update (ADR-0234) on the customer's Monitor. Its questions — Continue
before the erase (ADR-0366), "Continue without a copy of the application?" when the backup fails, and writing the
application back — were asked of the customer. The customer already allowed the update and should only move the
cable; the decisions belong to support.

## Decision

- **Write-back:** in a remote update the copy of the application is always written back automatically at the end
  (no question). Without a copy (empty application flash) nothing is written back and support is told.
- **Continue before the erase (ADR-0366):** support confirms it. The customer sees the explanation and the cable
  instructions without a button; support's Flash card (and `hwio remote join --lte-update`, `Continue? [y/N]`)
  asks.
- **Backup failed:** support is asked "The application could not be copied. Continue without a copy?" (same
  places). No means cancelled, nothing changed.
- No answer from support within the existing operation timeout, or support leaves: cancelled before the erase,
  nothing changed (after the erase the existing failure handling applies).
- Transport: a new tunnel message pair for an operation question from the host to the joiner and its answer (only
  from the allowed joiner that started the operation, only while that question is open); a host or joiner without
  it (feature not in the hello) keeps the ADR-0366 behaviour on the customer's side.
- A local (non-remote) update keeps ADR-0366 unchanged: the user at the device confirms.

## Consequences

- The customer is never asked a decision during a remote LTE update; they only follow the cable instructions.
- One more tunnel message type and its validation and tests.
