# ADR-0244 — MCP in the GUI: minor defaults

- **Status:** Accepted
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

## Consequences

- None beyond plan 0014.
