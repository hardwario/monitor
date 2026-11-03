# 0031 - Code signing and notarization in separate sign jobs

- **Status:** In progress
- **Milestone:** M7 (RC preparation)
- **Author:** Pavel Hübner (with Claude Code)
- **Related ADRs:** ADR-0120, ADR-0125, ADR-0126, ADR-0223, ADR-0224
- **Depends on:** 0015 (packaging and release)

## Context

`release.yml` (plan 0015) signed inside the build jobs with secrets of an environment `release` that was never
created in `hardwario/monitor` (ADR-0223). HARDWARIO now signs all its desktop applications with one setup (Azure
Artifact Signing, a new Developer ID certificate, environment `signing`, first used in `hardwario/tower-bridge`).
This plan moves the release to that setup and keeps the signing identity away from the jobs that compile.

## Goals / Non-goals

Goals: Windows executables and MSI signed with Azure Artifact Signing; the macOS app and DMG signed with the Developer
ID, notarized and stapled; no build job sees a signing secret or an OIDC token; the MSI is built from the signed files;
PR CI tests the MSI path. Non-goals: Linux package signing (still `SHA256SUMS` + provenance), a `.pkg` installer
(no Developer ID Installer certificate), required reviewers.

## User experience

Downloads from the release open without "unidentified developer" (macOS) and show "HARDWARIO a.s." as the publisher
(Windows). SmartScreen may still warn on the first downloads until the certificate gains reputation.

## CLI

None.

## Design

See ADR-0224. `release.yml`: `gate` → `tests` → `linux` / `macos` / `windows` (unsigned) → `sign-macos` /
`sign-windows` (environment `signing`, tags only) → `publish` (completeness check, `SHA256SUMS`, attestations, draft).

| File | Change |
|---|---|
| `.github/workflows/release.yml` | build jobs without environment and secrets, `unsigned-*` artifacts; jobs `sign-macos`, `sign-windows`; `publish` without environment, checks every package |
| `.github/workflows/ci.yml` | `package-windows` builds the MSI from the installed tree with `build-msi.ps1` |
| `packaging/windows/build-msi.ps1` | new: CPack WIX on an installed tree (`CPACK_INSTALLED_DIRECTORIES`), no install scripts |
| `packaging/windows/sign.ps1` | HARDWARIO's account as default, environment overrides |
| `packaging/macos/sign-and-notarize.sh` | `notarize-app` (zip, notarize, staple the app), `verify-dmg`; notarytool JSON output, log on rejection; `APPLE_*` names; DMG staging with `ditto` |
| `packaging/README.md`, `packaging/RELEASE.md` | environment `signing`, variables and secrets (names only), Azure trust, certificate expiry and renewal, new steps |

Signing order. Windows: `hwio-monitor.exe`, `hwio.exe` (installed tree) → MSI built → MSI signed → verified (tree,
inside the MSI, MSI) → ICE validation. macOS: Mach-O in Frameworks / PlugIns / Resources → `Contents/MacOS/hwio` →
bundle → app notarized and stapled → DMG → DMG signed, notarized, stapled → verified.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Where signing runs | build jobs (ADR-0120) / separate sign jobs | separate sign jobs, environment `signing` (HARDWARIO convention) | ADR-0224 |
| Secret names | `MACOS_*` / `TRUSTED_SIGNING_*` / convention | convention: `AZURE_*`, `APPLE_*` | ADR-0224 |
| Required reviewers on `signing` | none / maintainers | none, as in the other repositories; can be added in the settings later | ADR-0224 |
| Checksums | `SHA256SUMS` / one `.sha256` per file | `SHA256SUMS` stays (ADR-0125) | ADR-0224 |

## Implementation steps

- [x] Environment `signing` (tags `v*`) with the five variables (2026-11-03)
- [x] Federated credential `github-monitor-signing` for `repo:hardwario@28539912/monitor@1444229107:environment:signing`
      (2026-11-03)
- [x] `release.yml`, `ci.yml`, `build-msi.ps1`, `sign.ps1`, `sign-and-notarize.sh`, documentation
- [x] Apple secrets `APPLE_CERTIFICATE`, `APPLE_CERTIFICATE_PASSWORD`, `APPLE_API_KEY_P8` in the environment
      (2026-11-03)
- [ ] Tag protection for `v2.*` (repository rule)
- [ ] First release tag: both sign jobs green, draft checked (below); status → Done

## Tests and verification

- `actionlint` on both workflows (it runs shellcheck on the scripts), `shellcheck` on `sign-and-notarize.sh`.
- PR CI: `package-windows` builds the MSI from the installed tree and validates it (ICE); `package-macos` signs
  ad hoc and builds the DMG with the changed script.
- First tag: the Windows job's verification lines (four executables and the MSI: Valid, HARDWARIO a.s., timestamp);
  `sign-and-notarize.sh verify-dmg` on the downloaded DMG; `sha256sum -c SHA256SUMS`; install the MSI and the DMG
  on clean machines (plan 0015 F4 matrix).

## Risks and open points

- `build-msi.ps1` relies on the same workspace path and runner image in the build and the sign job (absolute paths
  in `CPackConfig.cmake`).
- Notarization can take long; each submission waits up to 60 minutes.
