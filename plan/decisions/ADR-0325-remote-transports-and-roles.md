# ADR-0325 — Remote session: J-Link RTT and BLE, host and join roles

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

Any connected J-Link RTT or BLE console can be shared (host). Monitor can also join a session (joiner): it opens a
device tab of a new `Transport::Remote`, backed by a `RemoteConsoleSession`. The console and flash pages reuse it.
The CLI gets both roles.

## Consequences

- Monitor covers more than the web Terminal (J-Link customers too) and gives support Monitor's full console UI.
- `ConsoleSession` gains a third transport. Code that switches on the transport must handle `Remote`.
