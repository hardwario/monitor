# ADR-0320 — Remote session: HARDWARIO Terminal compatible, hardened relay required

- **Status:** Superseded by ADR-0334
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Context

HARDWARIO Terminal (web, Chrome, BLE only) already has a Remote Session over a Socket.IO relay on
terminal.hardwario.com. The session ID is a browser-generated number from 0 to 999 999 with no token, so
anyone who guesses it gets a shell on the device.

## Options considered

1. Compatible with the Terminal, relay hardened first (server-issued ≥128-bit ID, join secret, host token, wss, rate limits).
2. Compatible with the Terminal as it is, with mitigations in Monitor only.
3. Own relay and protocol, not interoperable with the web Terminal.

## Decision

Option 1. Monitor speaks the Terminal event protocol (`at`, `write`, `newClient`, `sync`) on a hardened relay. It
checks the relay protocol version and refuses to host or join on the unhardened protocol. The web Terminal moves to
the same hardened flow. The exact hardening spec is agreed with the backend team and recorded in a follow-up ADR.

## Consequences

- Support can join from the web Terminal or from Monitor.
- The feature depends on the backend team. Without hardening it does not ship.
- The event format becomes a shared contract between two apps and needs a versioned spec.
