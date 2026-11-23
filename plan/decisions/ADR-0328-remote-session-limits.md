# ADR-0328 — Remote session limits: 30 min idle, 8 h maximum

- **Status:** Accepted; amended by ADR-0334 (enforced by Monitor only)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

A session ends after 30 minutes without console traffic in either direction or after 8 hours in total, whichever
comes first. The host is warned 2 minutes before the idle timeout and can extend it with one click (CLI: any input).
The relay enforces the 8 h maximum through `expiresAt`, and Monitor enforces both limits itself as well.

## Consequences

Forgotten sessions close on their own. Long diagnostics need a new session after 8 hours.
