# ADR-0267 — CLI `hwio nrfutil install-device`

- **Status:** Accepted (plan 0038, D5)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

Every backend feature also gets a CLI command (ADR-0017).

## Options considered

1. **`hwio nrfutil install-device [PATH] [--yes]` and `hwio nrfutil download-url` (chosen).**
2. `hwio doctor --fix`.
3. No CLI.

## Decision

`hwio nrfutil install-device [PATH] [--yes] [--timeout S]` and `hwio nrfutil download-url`; running a file found in
Downloads needs the `[y/N]` confirmation or `--yes` (ADR-0038); `--json` as all commands. `hwio doctor` keeps
checking only (no `--fix`). Implemented in rc.4 (ADR-0269).

## Consequences

CLI docs table, e2e tests with the fake nRF Util.
