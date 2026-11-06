# ADR-0270 — nRF Util locator honours `NRFUTIL_HOME`

- **Status:** Accepted (plan 0038, D8)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

Nordic's launcher installs into `$NRFUTIL_HOME` when that variable is set; our locator searches only
`~/.nrfutil/bin`.

## Options considered

1. **Search `$NRFUTIL_HOME/bin` before `~/.nrfutil/bin` (chosen).**
2. Leave the locator unchanged.

## Decision

`defaultLocatorOptions()` searches `$NRFUTIL_HOME/bin` (when set) before `~/.nrfutil/bin`; the precedence of option /
environment / setting / PATH is unchanged. Amends the locator comment in `nrfutil.h` and the docs search order.
Implemented in rc.4 (ADR-0269).

## Consequences

A user with `NRFUTIL_HOME` set does not get "installed, but not found"; one line plus a test.
