# ADR-0369 — Support's result of a remote operation names it and does not linger

- **Status:** Accepted (amends plan 0044 C2/C4 joiner Flash card)
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (rc.4 testing, plan [0046](../features/0046-rc4-fixes.md))

## Context

After a remote operation, support's Flash card showed "Done (reported by the customer's Monitor)." until the next
operation, in that remote tab, without saying what was done or when.

## Decision

- The result names the operation and the local time, e.g. "Application firmware flashed on the customer's device
  (14:02).", "Customer's device reset (14:02).", "LTE firmware updated, application written back (14:02)."; errors
  keep the error message with the operation name.
- A success disappears when support leaves the Flash page or starts another operation. An error stays until
  dismissed (×) or another operation starts.
- Every result is also written as a status line into support's console of that remote tab (kept in the console
  log and file), so the record remains after the card clears.
- CLI `hwio remote join` already prints the result line; it gets the same operation names.

## Consequences

- No stale "Done" on the card; the history is in the console.
