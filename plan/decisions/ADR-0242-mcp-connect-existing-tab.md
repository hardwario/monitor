# ADR-0242 — MCP connect on a probe with a user tab connects that tab

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0014 revision session)

## Context

ADR-0098: in the GUI the devices are the tabs, and `connect` opens a new tab after consent. A probe can already
have a tab of the user that is Disconnected (restored at start, or disconnected by the user). A second tab for the
same probe cannot connect (probe lock).

## Options considered

1. **Connect the existing tab after consent (chosen).**
2. Refuse with an error ("the device has a tab; ask the user to connect it").

## Decision

Option 1.
- `connect` for a probe with a disconnected tab asks for the `connect` category (the prompt says "Connect the tab
  <title>") and then connects that tab with its own options. The tab stays the user's: no "AI" badge, and
  `disconnect` from the AI gives `mcp.tool_not_allowed`.
- A probe without a tab gets a new AI-opened tab (ADR-0098); the `connect` arguments `profile`, `target`,
  `speed_khz` and `auto_reconnect` apply only to such a new tab.
- A probe whose tab is connected returns `already_open` (no prompt), as in the CLI.

## Consequences

- The AI cannot close a tab it did not open, even when it connected it.
