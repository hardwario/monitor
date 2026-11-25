# ADR-0375 — reacquire_probe waits for the console

- **Status:** Accepted
- **Date:** 2026-11-13
- **Decided by:** Matej Strnad (plan 0050)

## Context

`reacquire_probe` returned the connection state right after queuing the RTT restart, i.e. `stopped`, although the
console was connected a moment later. Clients had to call `status` again.

## Options considered

1. Wait (bounded) until the console is connected, then return (chosen).
2. Return at once with state `starting` and point to `wait_for_connection`.

## Decision

Option 1. `reacquire_probe` is asynchronous and waits at most `Limits::reacquireWait` (10 s) for `connected` or a
terminal error state; it returns the state and `waited` (seconds). A timeout is not an error: the current state
is returned and the client may use `wait_for_connection`.

## Consequences

One call gives a usable answer; the call can take up to 10 s. The tool must not block the owner thread.
