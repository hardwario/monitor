# ADR-0260 — Who owns an AI-opened tab: only its client may close it; a user change hands it over

- **Status:** Accepted
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (review of the plan 0014 implementation)

## Context

Plan 0014 lets an AI client open a device tab over MCP (`connect`, ADR-0098, ADR-0242) and close it again
(`disconnect`). Two cases were open after the implementation review: whether any AI client may close a tab another
AI client opened, and what happens to an AI-opened tab when the user starts working with it (chooses another device
or changes its connection options). Plan 0014 (clarified 2026-11-04) also keeps the `speed_khz` /
`auto_reconnect` arguments of `connect` out of the probe's remembered options.

## Options considered

1. Any AI client may close any AI-opened tab / **only the client that opened it (chosen)**.
2. An AI tab stays the AI's whatever the user does / **a user change hands the tab over to the user (chosen)**.

## Decision

- **Close:** an AI client may close only the AI-opened tabs it opened itself. "Itself" is the client identity of
  the grants (`RequestContext::clientKey`, ADR-0241: the legacy session id, or `clientInfo.name` of a modern
  client). Any other tab (the user's, or another client's) gives `mcp.tool_not_allowed`. The user can close every
  tab.
- **Hand-over:** when the user changes the device of an AI tab (Choose another probe / device) or any of its
  connection options (the ⋯ Advanced popover), the tab becomes the user's: `openedBy` is cleared (no "AI" badge),
  it is saved in `ui/tabs` and restored, and no AI client can close it any more.
- **Remembered options:** only the options the user actually changed are remembered for the probe
  (`probes/<sn>/…`, `ble/<id>/…`); the session overrides of the AI's `connect` (`speed_khz`, `auto_reconnect`) are
  never remembered, also not after a hand-over (they stay in effect for that tab's session).

## Consequences

- `DeviceTabViewModel` keeps the opening client's key next to `openedBy` and gets `handOverToUser()`;
  `ConnectionViewModel` keeps the rememberable options separately while session overrides apply.
- A legacy client that reconnects with a new session cannot close a tab its earlier session opened (accepted: the
  user can).
