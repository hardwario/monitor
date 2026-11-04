# ADR-0256 — After a J-Link firmware update from a tab, the tab reconnects

- **Status:** Accepted (plan 0036)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

ADR-0246 makes the user disconnect the tab before the update ("Disconnect and update"). Afterwards the tab is
disconnected.

## Options considered

1. **When the update was started from a tab's banner and succeeded, the tab connects again to the same probe
   automatically; after a failure or Cancel it stays disconnected (chosen).**
2. The tab stays disconnected; the success toast says "Connect again".

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04).

## Consequences

- The banner flow ends where it started (console running), matching the old app's behaviour.
- Updates started from the setup wizard or a troubleshooting topic do not connect anything.
