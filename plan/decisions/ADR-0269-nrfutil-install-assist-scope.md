# ADR-0269 — Release scope of the nRF Util install assist

- **Status:** Accepted (plan 0038, D11)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

The full assist (backend, dialog, CLI, new error id) touches backend, GUI, CLI, docs ×5 and translations ×4 and needs
manual checks on Linux, Windows and macOS. rc.3 is cut on 2026-11-05.

## Options considered

1. Everything of part A in rc.3 if the three-OS checks pass, else docs + links only.
2. **Docs + links only in rc.3; the rest of part A in rc.4 (chosen by the user).**
3. Everything including part B in rc.3.

## Decision

- **A0 — rc.3:** per-OS install steps in `install.md` (5 languages); **How to install ↗** opens our docs topic;
  new **Download nRF Util ↗** button with the direct per-OS link (Flash banner, LTE wizard, Settings → nRF Util).
- **A1 — rc.4:** the Install nRF Util dialog, `findDownloaded` / `prepareExecutable` / `installDeviceCommand`,
  `NRFUTIL_HOME` in the locator, CLI `hwio nrfutil install-device` / `download-url`,
  `dependency.nrfutil_install_failed`.
- **Part B — later:** signature checks, upgrade button for `too_old`, setup wizard step, `--detect-proxy` retry.

## Consequences

rc.3 adds no new code paths that run programs; the docs mention the in-app dialog only from rc.4 on.
