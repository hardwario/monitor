# ADR-0379 — The log toolbar is one row; an active filter is a small ✕ next to the levels

- **Status:** Accepted
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (rc.4 issues, plan [0051](../features/0051-rc4-issues-5-11.md))

## Context

Issue #6: the log search bar is a `Flow`; the wide "✕ Filter: levels" chip (Search mode) or "Showing X of Y lines" + "Clear filter" (Filter mode) wrap the D/I/W/E/H chips to a second row and the toolbar changes height.

## Options considered

1. A small ✕ icon right of the level chips, shown only when the filter differs from the default; one-row layout (chosen).
2. The ✕ always present, disabled when inactive.
3. A highlighted border around the level group, reset by a context action.

## Decision

- `RowLayout`, never wraps; the search field fills the remaining width (minimum 120 px).
- ✕ icon button "Clear filter" (tooltip, accessible name) right of the chips, visible while `filterActive`.
- Filter mode: "Showing %1 of %2 lines" is a muted, elided suffix inside the field (tooltip full); Search mode with a text filter: suffix "Filter: %1".
- The "Filter: …" chip and the "Clear filter" button are removed (amends the 0021 decision of 2026-10-23 only in where the count is shown).

## Consequences

Stable toolbar height at 960 px in the longest language (tested). The count is less prominent but still visible.
