# ADR-0356 — Remote session: secrets are redacted in everything the host sends to the relay

- **Status:** Accepted
- **Date:** 2026-11-10
- **Decided by:** Matej Strnad (plan [0044](../features/0044-remote-support-session.md) implementation, asked by the
  implementing agent)

Amends ADR-0329 (TLS only: what the relay sees) and applies ADR-0186 to the relay traffic.

## Context

The relay operator can read the console traffic (ADR-0329), and every participant of a session receives it, also one
that the customer has not allowed (ADR-0335). Commands with secret arguments and secret output values (the claim
token, the BLE passkey) are already kept out of console files and the command history (ADR-0186).

## Decision

The host applies the ADR-0186 rules of the shared console's profile and target to everything it sends to the relay:
`at` write lines and the `sync` transcript show a command with secret arguments as `<prefix> <redacted>`, `at` read
lines and the transcript show a secret output value as `<key>: <redacted>`. The host's remote audit markers name remote
commands in the same redacted form. The device itself receives the commands unchanged.

## Consequences

- Secrets never leave the customer's computer through a remote session.
- Support cannot read a claim token or passkey through the session; if they need one, the customer reads it out.
- The amended ADR gets an "Amended by ADR-0356" note.
