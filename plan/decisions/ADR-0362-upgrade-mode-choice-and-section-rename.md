# ADR-0362 — Upgrading users choose a mode too; the Settings section "Advanced" becomes "Expert"

- **Status:** Accepted
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (plan [0045](../features/0045-user-modes.md), D-5, D-6)

## Decision

- Users upgrading from rc.3 or older see the first-start mode dialog once; it preselects Expert when
  `advanced/expertActions` was on, otherwise Advanced. Closing the dialog stores the preselection.
- The Settings section "Advanced" (Support server) is renamed **Expert**, so it does not clash with the mode name; it
  is visible in Expert mode only.

## Consequences

Every existing user sees one dialog after the update; the release notes announce it.
