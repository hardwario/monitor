# ADR-0364 — The remote tab's autocomplete uses the command list sent by a Monitor host

- **Status:** Accepted (amends ADR-0336)
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (rc.4 demo feedback, plan [0044](../features/0044-remote-support-session.md) § Remote
  autocomplete)

## Context

Autocomplete learns the device's command list after the attach's `info show` (plan 0026). A remote tab never attaches,
so it had no autocomplete.

## Options considered

1. The host sends the command list it learned; no extra commands on the device (chosen).
2. The joiner learns through the session (`help`, `<cmd> -h`), visible as remote commands in the customer's console.

## Decision

- A Monitor host sends its learned command tree in a tunnel message `commands` after the join is allowed, and again
  when its tree grows. When the joiner needs the children of a known command, it sends a tunnel `learn` request
  (allowed joiners only, a known path only, rate-limited); the host learns it with its silent query (as for its own
  autocomplete) and sends the updated tree.
- The joiner validates the tree as untrusted input (bounded: size, depth, node count, name charset) and uses it for
  completion and suggestions, together with its own command history.
- A web Terminal host sends no tree: suggestions come from the history only.

## Consequences

- Support gets the same completion as the customer without noise in the customer's console.
