# ADR-0322 — Remote session scope: console plus flash, erase, recover, reset

- **Status:** Accepted; amended by ADR-0334 and ADR-0336 (tunnel instead of `hwio:*` events)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

A remote joiner can send console commands and see device output (any peer). A **Monitor** joiner can also request
flash (ADR-0323), erase, recover and reset of the `app` / `lte` targets through Monitor-only `hwio:*` events, which
the web Terminal ignores. Out of scope: UICR/PIB writes, nRF Util modem update, file transfer, MCP over the relay.
Only one remote operation runs at a time.

## Consequences

- The host-side operation handling is a security boundary. It needs strict validation and a security review.
- Web Terminal joiners get console only (`remote.operation_unsupported_peer` when a Monitor joiner's host is the web Terminal).
