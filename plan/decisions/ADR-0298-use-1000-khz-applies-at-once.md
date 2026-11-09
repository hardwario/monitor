# ADR-0298 — [Use 1000 kHz] in troubleshooting applies to the next session at once

- **Status:** Accepted (plan 0009)
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (2026-11-09, question Q2 of the 0009 implementation)

## Context

The troubleshooting topics `no-connect` and `program-failed` have [Use 1000 kHz], which sets the tab's per-probe speed
(ADR-0052, `ConnectionViewModel::setAutoSpeed(false)` + `setSpeedKhz(1000)`, remembered per probe). The speed is applied
when a session opens, so the Diagnose that follows (and a flash retry) would still run at the old speed on an open
session.

## Options considered

1. **Store the per-probe speed; close the wizard's own probe-only session so that the next Diagnose opens a new one;
   reconnect a connected tab (chosen).**
2. Store it; close the wizard's probe-only session but never reconnect a connected tab (the speed applies at the
   next connection).
3. Only store it.

## Decision

Option 1. The toast says "J-Link speed set to 1000 kHz for this probe."; a probe-only session is closed
(`disconnectDevice`), any other session of the tab is closed and the tab connects again (`disconnectDevice` +
`connectDevice`). Nothing happens to the session while the tab is busy.

## Consequences

- The next check in the topic uses 1000 kHz.
- A connected console reconnects once (a marker in the console, as a manual reconnect).
