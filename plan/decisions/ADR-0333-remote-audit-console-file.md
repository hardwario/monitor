# ADR-0333 — Remote actions recorded in the host's console file

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

The host writes session start/end, joins/leaves (with the peer app), remote commands and every operation request with
its outcome (allowed, denied, timed out, result) into the existing console file and log, marked with the remote origin.
No separate file. The existing console file limits apply.

## Consequences

The customer can later see what support did. Saved views and diagnostic reports include it automatically.
