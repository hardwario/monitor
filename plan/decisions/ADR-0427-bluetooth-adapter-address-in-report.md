# ADR-0427 — The diagnostic report hides the computer's Bluetooth adapter address

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, as recommended) (plan [0060](../features/0060-test-feedback-2026-11.md), D7)

## Context

The Bluetooth check shows "Adapter ready (name, address)". The report (ADR-0075) redacts the home directory, user
and host name, but not the adapter address, which identifies the computer.

## Options considered

1. **Redact the local adapter addresses in the whole report (`<adapter address>`); keep them on screen and in
   `hwio doctor`.**
2. Never show the address.
3. Keep it.

## Decision

Option 1 (accepted 2026-11-24). Addresses of devices (CHESTER) stay: support needs them.

## Consequences

- `RedactionContext` gains the adapter addresses (from the same adapter query the check makes).
