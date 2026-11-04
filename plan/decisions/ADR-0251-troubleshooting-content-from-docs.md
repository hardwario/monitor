# ADR-0251 — Troubleshooting topics are derived from the user docs and reviewed by HARDWARIO

- **Status:** Accepted (plan 0035)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

Plan 0009's topic texts were written before the user docs existed. `docs/en/troubleshooting.md` (plan 0029) now has
a "Common problems" section and an entry for every error code, translated into five languages. Open question 22
(CHESTER connector names, cable, pin-1 marking, power wording, real illustrations) is still open.

## Options considered

1. **Derive the topic texts from `docs/en/troubleshooting.md` (same facts, wizard-sized steps); HARDWARIO reviews
   them; connector names and art stay placeholders until open question 22 is answered (chosen).**
2. HARDWARIO writes the topic texts from scratch before 0009 starts.
3. Keep the 0009 texts as written in 2026-10.

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04).

## Consequences

- One source of facts: a change of a troubleshooting fact is made in the docs page and in the topic table together
  (review rule in 0035).
- 0009 can be implemented before HARDWARIO's content arrives; 0009 step T6 (content) remains a Done condition for the
  art and connector names only.
