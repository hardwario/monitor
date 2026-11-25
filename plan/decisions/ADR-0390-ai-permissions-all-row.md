# ADR-0390 — AI assistant permissions: an "All" row, rows from a list model

- **Status:** Accepted
- **Date:** 2026-11-19
- **Decided by:** Matej Strnad (2026-11-19, the mock of plan 0055 approved as shown)

## Context

**Settings › AI assistants (MCP) › Permissions** has one row per consent category (ADR-0101) with the radio buttons
**Ask** / **Always allow**. The rows were a `Repeater` over a `QVariantList` property, so changing one rule rebuilt
every row: all radio buttons flickered and the keyboard focus was lost. There was no way to set all rows at once.

## Options considered

- Control for all: **an "All" row with the same two radio buttons** / two buttons above the list.
- "Always allow" for all: **confirm once** / no question.
- Coverage: **all five categories** / all except memory and UICR writes.
- Rows: **a `QAbstractListModel` that reports only the changed row** / keep the list property and hide the rebuild.

## Decision

- The rows come from a list model (`ConsentRulesModel`) that emits `dataChanged` for the changed row only and never
  resets.
- A first row **All** sets every category at once. **Ask** applies immediately. **Always allow** asks once
  ("Always allow everything? …", Cancel / Always allow everything) and covers all five categories, including memory
  and UICR writes. The row shows a selection only when all categories agree.

## Consequences

- One click redraws one row; the focus stays where it was.
- The most permissive state is two clicks away, behind one confirmation that names what it allows.
- New strings are translated in four languages; the user docs mention the **All** row.
