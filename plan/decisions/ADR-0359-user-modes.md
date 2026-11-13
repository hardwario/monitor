# ADR-0359 — User modes Basic, Advanced, Expert (Settings and expert actions), in 2.0.0-rc.4

- **Status:** Accepted
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (plan [0045](../features/0045-user-modes.md), D-1, D-2, D-2b, D-3, D-7)

## Context

Settings has seven sections and about 35 rows; most users need only a few. One switch already hides dangerous
maintenance actions (ADR-0236).

## Options considered

- Reach: Settings only / Settings + expert maintenance actions / the whole app.
- Names: Easy–Medium–Expert / Easy–Standard–Expert / Basic–Advanced–Expert.
- Where chosen: Settings only / also at the first start.
- Release: rc.4 (rc.4 waits) / rc.5.

## Decision

- Three modes **Basic / Advanced / Expert**, a GUI setting `ui/mode`; they hide Settings sections and rows by the
  table in plan 0045 and gate the expert maintenance actions (ADR-0360). Values of hidden settings are kept and used.
- Chosen in Settings › General (first row, every mode) and once at the first start in a dialog, **Advanced**
  preselected. The first-run setup wizard (plan 0008, not built yet) takes this over as its first step later.
- CLI, MCP and backend unchanged.
- Part of 2.0.0-rc.4; rc.4 is tagged only after plan 0045 passes its verification (extends ADR-0337).

## Consequences

- Docs say which mode shows each setting; support instructions must name the mode.
- Every Settings row declares its minimum mode; new rows must too.
