# ADR-0384 — Wizard content shows that it scrolls

- **Status:** Accepted
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (rc.4 issues, plan [0051](../features/0051-rc4-issues-5-11.md))

## Context

Issue #10: in step 7 of the guided LTE update the expanded firmware picker and its Recent list are below the visible area; the Material scroll bar appears only on hover or scroll.

## Options considered

1. Scroll the expanded section into view (chosen).
2. Scroll bar always visible while the content overflows (chosen).
3. A fade at the bottom edge while more content is below (chosen).

## Decision

`WizardDialog`: `ScrollBar.AlwaysOn` while the content is taller than the view; a 24 px fade to the dialog background at the bottom while not scrolled to the end; `ensureVisible(item)`. Step 7 scrolls the picker into view when "Firmware from the catalog or a file" is chosen.

## Consequences

Applies to all wizards. Hidden content is discoverable without hovering.
