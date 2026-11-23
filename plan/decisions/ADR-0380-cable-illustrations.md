# ADR-0380 — Schematic cable illustrations for the APP and LTE connectors

- **Status:** Accepted
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (rc.4 issues, plan [0051](../features/0051-rc4-issues-5-11.md))

## Context

Issue #7: the guided LTE update's step 4 shows the `chester-lte-cable` slot, but all wizard images are placeholders (ADR-0076); APP and LTE look the same. Real photos/art are a content task.

## Options considered

1. Schematic SVG line drawings now, photos later (chosen).
2. Wait for real art.
3. User-supplied photos.

## Decision

`chester-app-cable` and `chester-lte-cable` (light and `-dark`, 480×270, no text, ADR-0076): CHESTER board outline with both programming connectors at their real positions, the J-Link flat cable on the connector of the step, that connector in the accent colour, the other muted. Files in Git LFS. Connector positions are checked against a real board before merge.

## Consequences

The steps become distinguishable at once; the slots stay, so photos can replace the files without code changes.
