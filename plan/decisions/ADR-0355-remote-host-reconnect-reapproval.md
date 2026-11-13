# ADR-0355 — Remote session: join approval again after the host's own reconnect

- **Status:** Accepted
- **Date:** 2026-11-10
- **Decided by:** Matej Strnad (plan [0044](../features/0044-remote-support-session.md) implementation, asked by the
  implementing agent)

Amends ADR-0335 (join approval).

## Context

The relay sends `newClient` only to the sockets connected at that moment. While the host's own relay connection is
down (network drop, reconnect with backoff), a new participant can connect to the namespace without the host ever
learning about it. If the host simply resumed after reconnecting, that participant's `write` lines would reach the
device under the earlier approval.

## Decision

When the host's relay connection comes back after a loss and a participant had been allowed before, the host stops
passing remote commands and asks the join question again (the same text, timeout and End-session default as
ADR-0335). No sync is sent until it is answered with Allow. The host's own `newClient` after the reconnect is ignored
as before.

## Consequences

- An unannounced participant never gets command access through an old approval.
- After a short network drop the customer is asked once more; the banner says that the connection was restored.
- The amended ADR gets an "Amended by ADR-0355" note.
