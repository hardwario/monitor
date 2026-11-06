# ADR-0268 — One "Install nRF Util" dialog

- **Status:** Accepted (plan 0038, D7)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

The install assist is needed on the Flash page, in the LTE update wizard, in Settings → nRF Util and later in the
setup wizard.

## Options considered

1. **One modal dialog opened from all entry points (chosen).**
2. An inline panel in Settings only; banners open Settings.
3. A new wizard `nrfutil-install`.

## Decision

A modal dialog backed by `NrfUtilInstallViewModel` (owned by `FirmwareServices`, one install at a time, owned
worker thread per run as in ADR-0257), opened from the Flash banner, the LTE wizard banner, Settings → nRF Util and
later the setup wizard (D9, part B). Implemented in rc.4 (ADR-0269).

## Consequences

One QML component and one ViewModel to test; no new wizard id.
