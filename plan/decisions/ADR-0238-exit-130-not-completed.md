# ADR-0238 — Exit code 130 also means "not completed"

- **Status:** Accepted (amends ADR-0024, row 130)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (plan 0034, B1)

## Context

`hwio chester lte flash --guided` (plan 0033, ADR-0234) ended with exit code 0 when the LTE MCU was updated but the
application was not written back (no copy: an empty or unreadable application). The application MCU is then empty
and scripts take the run for a complete update. The exit-code table of ADR-0024 has no category for an operation
that ended without completing; 130 (cancelled) was documented as Ctrl+C / SIGTERM.

## Options considered

1. **`ErrorCode::Cancelled`, exit code 130 (chosen).** The same as the other incomplete end of `--guided` (the user
   declines to continue without a copy); the meaning of 130 is widened.
2. A new Target-category error id (e.g. `target.application_missing`), exit code 6: a new public error id with
   translations and a troubleshooting entry.
3. Exit code 1 (internal): wrong, it is not a bug.

## Decision

Exit code 130 means "cancelled or not completed (Ctrl+C, SIGTERM, guided procedure ended without restoring the
application)". `--guided` without the application written back returns `ErrorCode::Cancelled` with the message
"The LTE firmware was updated. The application MCU is empty." and the suggestion "Flash an application with
`hwio chester app flash`."

## Consequences

- `plan/0006-cli.md` and `docs/en/cli.md` describe the wider meaning; no new error id, no new translation.
- Scripts can no longer read 130 as "the user pressed Ctrl+C" only.
