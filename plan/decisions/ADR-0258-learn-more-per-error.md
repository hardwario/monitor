# ADR-0258 — "Learn more" link per error id in the ErrorPanel

- **Status:** Accepted (plan 0037)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

`docs/<lang>/troubleshooting.md` has an `<a id="<error id>">` anchor for every error code in all five languages
(plan 0029). The ErrorPanel shows message, cause, suggestion, details and an optional wizard button.

## Options considered

1. **A "Learn more ↗" link in every ErrorPanel (and WarningBanner details) that opens
   `troubleshooting.md#<error id>` in the UI language (chosen).**
2. Only the Help → Documentation link.

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04).

## Consequences

- A CTest asserts that every id of `errorTable` has the anchor in each language's `troubleshooting.md`; adding an
  error code then requires its docs entry in five languages.
