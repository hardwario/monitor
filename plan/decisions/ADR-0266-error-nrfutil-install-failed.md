# ADR-0266 — Error `dependency.nrfutil_install_failed`

- **Status:** Accepted (plan 0038, D6)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

A failed `nrfutil install device` (offline, proxy, server, other) needs its own user-facing error.

## Options considered

1. **New `dependency.nrfutil_install_failed` (chosen).**
2. Reuse `network.download_failed` with an overridden message (its texts and docs talk about firmware).
3. Reuse `dependency.nrfutil_not_found` with overridden texts.

## Decision

New `ErrorCode::DependencyNrfUtilInstallFailed`, id `dependency.nrfutil_install_failed`, category dependency
(exit 4), wizard none; message "Installing nRF Util failed.", cause "There is no internet connection, a proxy blocks
files.nordicsemi.com, or nrfutil reported an error.", suggestion "Check the internet connection and try again. The
details show what nrfutil reported." Docs anchor in all five `troubleshooting.md`. Implemented in rc.4 (ADR-0269).

## Consequences

Error table, `exit_codes`, docs ×5, translations ×4; the `DocsAnchors` test covers it.
