# ADR-0271 — Only a running AI operation blocks the J-Link firmware update

- **Status:** Accepted
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (integration of plans 0014 and 0036)

## Context

ADR-0132 and ADR-0246 block the J-Link firmware update of the GUI "while MCP holds the probe" (ADR-0105) with the
tooltip "Used by an AI assistant". Plan 0036 left the check as a hook (`ProbeFirmwareViewModel::setAiHoldsProbe()`)
until the GUI MCP server of plan 0014 existed. When both were integrated it was open what "holds" means: an AI
operation sets the tab busy (`setBusy("ai", …)`), but an AI-opened tab can also stay connected and idle, and
`send_command` sets no busy page.

## Options considered

1. Block also while an AI tab (or any tab the AI uses) is connected, and while `send_command` runs.
2. **Block only while an AI operation runs on a tab of the probe (busy page `"ai"`) (chosen).**

## Decision

- The update is blocked with "Used by an AI assistant" only while a J-Link tab of the probe has the busy page `"ai"`
  (`GuiDeviceProvider::aiOperationRuns()`, installed by `McpController`).
- An idle AI-opened tab and a running `send_command` do not block. The banner's [Disconnect and update…] disconnects
  an idle AI tab exactly like a user's tab (ADR-0246); a wizard request names it as any other tab ("Disconnect tab
  %1 first.").
- The reverse stays as plan 0036 built it: an MCP `connect` of a probe reserved for the update gets the registry's
  `probe.open_failed` ("The firmware of the J-Link %1 is being updated.").

## Consequences

- No new state or strings; the check follows the existing busy mechanism of ADR-0105.
- An AI client may find its idle tab disconnected by the user's update; its next call gets `mcp.device_not_open`
  (or connects again after the update) — accepted, the user decides.
- An AI `send_command` running while the user starts the update ends with the session (the console is shared,
  ADR-0105).
