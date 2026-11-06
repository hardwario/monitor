# ADR-0263 — Finding the downloaded nrfutil and the install links

- **Status:** Accepted (plan 0038, D12, D13)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

The app must find the download without the user typing a path, but must not scan the disk or trigger privacy prompts
unexpectedly. The in-app "How to install ↗" link pointed at Nordic's product page, not at our guide.

## Options considered

- Search the Downloads folder only on [Find download] **(chosen)** / automatically when the dialog opens / never
  (file dialog only).
- "How to install ↗" opens our docs topic `InstallNrfUtil` **(chosen)** / keeps Nordic's product page.

## Decision

- Search only on [Find download] (CLI: when no nRF Util is located), only the top level of the platform Downloads
  folder (`QStandardPaths::DownloadLocation`), names `nrfutil`, `nrfutil.exe` and browser duplicates, bounded
  (5000 entries, 10 candidates, 1–300 MB), newest first; partial downloads reported as such. [Choose file…] is always
  available.
- Direct download URLs per OS/CPU come from Nordic's install guide (Windows x64, macOS universal, Linux x86-64,
  Linux ARM64; Windows ARM64 uses the x64 build). In rc.3 the table lives in `SettingsViewModel`
  (`nrfUtilDownloadUrl`, compile-time OS + `QSysInfo::currentCpuArchitecture()`) and feeds the
  **Download nRF Util ↗** button; in rc.4 it moves to `libs/hw/nrfutil` (`downloadUrl()`) for the CLI too.
- **How to install ↗** opens our docs topic `InstallNrfUtil` in the UI language (ADR-0247); Nordic's product page is
  the fallback for unknown systems.

## Consequences

- A changed Nordic URL needs a code and docs change (the product-page link still works).
- `DocsAnchors` unchanged (existing anchor `nordic-nrf-util-lte-modem-firmware-only`).
- On macOS the Downloads privacy prompt appears only after a user action.
