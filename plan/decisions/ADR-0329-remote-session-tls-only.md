# ADR-0329 — Remote session: TLS to the relay, no end-to-end encryption in protocol 2

- **Status:** Accepted; amended by ADR-0334 (E2E impossible while compatible; no protocol numbers) and ADR-0356 (secrets redacted toward the relay)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Options considered

1. TLS (`wss`) to the relay only. The relay, run by HARDWARIO, can see the console content.
2. End-to-end encryption with a key in the link fragment, which the web Terminal would also have to implement.

## Decision

Option 1 for protocol 2. Secrets are kept out of history and logs as before (0025). The share dialog says that the
session passes through the HARDWARIO server. E2E can come later as protocol 3 with its own ADR.

## Consequences

Simpler for both clients. The relay operator has to treat the traffic as customer data (no logging of contents).
