# ADR-0423 — CLI exit codes: a missing input file is io (3); an unknown command with --help is usage (2)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, as recommended) (plan [0060](../features/0060-test-feedback-2026-11.md), D2 and D3)

## Context

Plan 0006 (exit codes of ADR-0024) gives exit 3 (io) for "file not found". Plan 0003 specified
`usage.invalid_argument` for `chester app flash ARG` when ARG is neither an existing path nor a firmware ID, and
`chester lte flash FILE` (and a given application copy path) does the same for a missing file: exit 2. py-hardwario
raises `click.BadParameter` there, also exit 2. Separately, `hwio rtt --help` prints the
main help and exits 0, because CLI11 handles `--help` before it rejects the unknown `rtt`; `hwio rtt` exits 2.
A script checking `$?` takes the command as existing.

## Options considered

1. **Missing input file → `io.file_not_found` (exit 3), message unchanged; no argument and no build output stays
   `usage`. Unknown (sub)command with `--help` → `usage.invalid_argument` (exit 2), as without `--help`.**
2. Keep both and correct plan 0006 instead (closer to py-hardwario's click exit code).

## Decision

Option 1 (accepted 2026-11-24).

## Consequences

- Scripts relying on exit 2 for a missing flash file must check 3; this is pre-2.0, and plan 0006 already
  documented 3. Plan 0003 gets a note pointing here.
- `--help` on existing commands is unchanged (exit 0).
