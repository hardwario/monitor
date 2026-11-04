# ADR-0244 — MCP in the GUI: minor defaults

- **Status:** Accepted (clarified 2026-11-04)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0014 revision session)

## Context

Plan 0014 listed smaller UI and settings choices as one "minor defaults" row, plus two questions raised in the
2026-11-03 revision (where the user cancels an AI operation, and how the look is documented).

## Decision

Accepted as proposed:
- Settings section "AI assistants (MCP)" with the layout of plan 0014; the rail indicator sits above Settings.
- AI-opened tabs carry an "AI" badge, are added at the end of the tab bar without switching the current tab, and
  are not restored on the next start.
- The consent prompt does not switch tabs (a "Show device" link does). Esc = Deny; no default button.
- Client snippets: Claude Code HTTP (`claude mcp add --transport http …` with the token), `.mcp.json` with
  `${HWIO_MCP_TOKEN}`, Claude Desktop over stdio (`hwio mcp`, a separate process).
- Settings keys `mcp/enabled` (false), `mcp/port` (47800), `mcp/consent/<category>` (`ask`).
- An "active client" made a request within the last 60 s. The activity list is kept in memory only.
- `read_memory to_file` writes into the default dump folder (cache folder + `/mcp`, ADR-0182); not configurable
  in the GUI.
- A running AI operation can be cancelled with a Cancel button next to its busy text in the device bar
  (ADR-0105); closing the tab or quitting asks as today.
- The look is documented by screenshots from Qt Quick tests (light/dark, EN/CS, 960 × 600), as in 0021 / 0028,
  and a short entry in `plan/gui/mockups/index.html`; no hand-drawn mockups.

- *Clarified 2026-11-04 (Matej Strnad):* the `speed_khz` / `auto_reconnect` arguments of MCP `connect` apply only
  to the AI-opened tab and are never stored as the probe's remembered options (`probes/<sn>/…`). The consent
  window is hidden only while the main window is hidden or closed (it stays while the main window is minimised),
  so that closing the main window still quits (ADR-0108).
- *Clarified 2026-11-04 after review (H1):* every answer of the consent window carries the id of the prompt it was
  given for (an answer for a prompt that is no longer shown is ignored). Whenever another prompt is shown the window
  is placed and raised again, the focus moves to Deny, and the Allow buttons stay disabled for **700 ms**, so that a
  double click or a repeated key never approves a prompt the user has not seen. The prompt and the grant list show
  the client's version next to its name (ADR-0241); client names and versions are sanitised (ADR-0183: escape
  sequences, controls, bidirectional controls and line breaks removed) and cut to 64 characters.

## Consequences

- None beyond plan 0014.
