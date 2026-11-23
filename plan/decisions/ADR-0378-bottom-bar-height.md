# ADR-0378 — All console bottom bars have one height (Theme.toolbarHeight)

- **Status:** Accepted
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (rc.4 issues, plan [0051](../features/0051-rc4-issues-5-11.md))

## Context

Issue #5: the command input is 48 px high (32 + 2×8), the log search bar 38 px (30 + 2×4) with hard-coded 30/26/24 px controls; side by side the separators and controls do not line up.

## Options considered

1. One theme token for all bottom bars, controls at `controlHeight` (chosen).
2. Align only by matching the numbers in both files.

## Decision

`Theme.toolbarHeight` = `controlHeight` + 2×`spacingXs` (40 px). `ShellInput`, the shell search bar and the log search bar use it; their fields and buttons use `controlHeight` (32), smaller chips are centred. No hard-coded bar heights.

## Consequences

The bars line up in every layout; a Qt Quick test checks equal height and top edge. Later bottom bars use the token.
