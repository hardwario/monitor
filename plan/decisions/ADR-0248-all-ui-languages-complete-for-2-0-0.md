# ADR-0248 — Every UI language is complete for 2.0.0

- **Status:** Accepted
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2.0.0 planning)

## Context

The UI is translated with Qt Linguist (ADR-0027); plan 0030 adds German, Slovak and Polish next to Czech
(ADR-0208..0211). New plans for 2.0.0 (0035–0037) add many user-visible strings.

## Options considered

1. **All four non-English languages (cs, de, sk, pl) have 0 unfinished entries for 2.0.0 (chosen).**
2. Czech complete, the others best effort.

## Decision

- For 2.0.0, `check_translations` reports 0 unfinished entries for `cs`, `de`, `sk` and `pl`.
- Every plan's translation step reads "cs + de + sk + pl"; a plan is not Done while one of them has unfinished
  entries for its strings.

## Consequences

- Each plan's verification includes 960 × 600 screenshots in the longest language (usually German) to catch
  truncation.
- Native review of de / sk / pl stays an open point (plan 0030), but no entry may be left unfinished.
