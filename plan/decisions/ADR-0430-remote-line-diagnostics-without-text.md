# ADR-0430 — Remote session line diagnostics: counts and lengths, never text

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, "Approve all", plan 0061)

## Context

In real use of the remote support session the console sometimes showed blank lines instead of a command's answer
(plan 0061 item 2). The application log has nothing that tells whether a line lost its text on the host, on the
way, on the joiner, or only on the screen. Console text may contain secrets and customer data and must not go to
the application log or the diagnostics report (ADR-0186).

## Options considered

- (a) Debug-level log lines in category `hwio.remote` on both sides: per forwarded or received batch the number
  of lines and the sum of their lengths before and after sanitising; one line per `sync` (entries, truncated).
- (b) No permanent diagnostics; find the cause by reproduction only.
- (c) Log the text of the lines (rejected: ADR-0186).

## Decision

Proposed: (a).

## Consequences

- A diagnostics report from the field localises a lost line without exposing console content.
- Tests assert that these log lines contain no console text.
- A small, bounded amount of debug logging per batch (not per line).
