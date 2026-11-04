# ADR-0224 - Signing runs in separate jobs in the environment `signing`

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Pavel Hübner (HARDWARIO's convention for signing in GitHub Actions, 2026-10-20; applied to this
  repository in plan 0031)

## Context

ADR-0120 put the signing secrets and the Azure OIDC login into the environment `release`, used by the same jobs that
compile. Those jobs run clang, the CMake code of every CPM / FetchContent dependency, Qt's deployment tools and
aqtinstall: any of them could read the `.p12` and the notarization key from the job, or request an OIDC token and
sign arbitrary files as HARDWARIO a.s.

In the meantime HARDWARIO set up one signing setup for all its repositories that sign in GitHub Actions, first used
in `hardwario/tower-bridge`: the Azure Artifact Signing account `hardwario` (profile `hardwario`), one Entra app
`github-codesign` with a federated credential per repository, a new Developer ID Application certificate
(valid until 2027-11-18), the notarization API key, and the environment `signing` with fixed variable and secret
names. ADR-0223 left the environment and the signing setup of `hardwario/monitor` to another maintainer; the
environment `release` was never created.

## Options considered

1. **Separate sign jobs in the environment `signing`; the build jobs see no secret and no OIDC token (chosen).**
2. Keep ADR-0120's layout and only create the environment `release` with the old secret names.
3. Sign on a self-hosted runner outside GitHub's hosted pool.

## Decision

- **Build jobs** (`macos`, `windows` in `release.yml`) have no environment, no secrets and no `id-token`. They upload
  unsigned outputs as `unsigned-*` artifacts: `unsigned-macos` (the bundle zipped with `ditto`), `unsigned-windows`
  (the installed tree with `CPackConfig.cmake` and `packaging/LICENSE.txt` of the build tree). Symbol archives
  (ADR-0123) stay in the build jobs.
- **Sign jobs** `sign-macos` and `sign-windows` run in the environment `signing`, which admits deployments only from
  tags `v*`, and only for tags (`if: github.ref_type == 'tag'`). Only `sign-windows` has `id-token: write`. They
  check out this repository for its packaging scripts and run packaging tools only: create-dmg (pinned, SHA-256),
  `codesign` / `notarytool` / `stapler`, CPack WIX and WiX. Never a compiler, never a dependency's build code.
- **Windows:** sign `hwio-monitor.exe` and `hwio.exe` in the installed tree (Qt DLLs and the MSVC runtime keep their
  vendors' signatures, ADR-0120), build the MSI from that tree with `packaging/windows/build-msi.ps1` (CPack with
  `CPACK_INSTALLED_DIRECTORIES`, no install scripts), sign the MSI, then verify the executables in the tree, inside
  the MSI (administrative install) and the MSI itself: valid, signed by HARDWARIO a.s., timestamped. PR CI
  (`package-windows`) builds its MSI with the same script. The sign job runs on the build job's runner image
  because `CPackConfig.cmake` holds absolute paths.
- **macOS:** sign inside-out as before (ADR-0126 unchanged), notarize and staple the **app**, build the DMG from the
  stapled app, sign, notarize and staple the DMG, then verify the DMG and the app inside it.
- **Names:** variables `AZURE_CLIENT_ID`, `AZURE_TENANT_ID`, `AZURE_SUBSCRIPTION_ID`, `APPLE_API_KEY`,
  `APPLE_API_ISSUER`; secrets `APPLE_CERTIFICATE`, `APPLE_CERTIFICATE_PASSWORD`, `APPLE_API_KEY_P8`. The Artifact
  Signing endpoint, account and profile are fixed in the workflow (they are not secret). This replaces the
  `MACOS_*` and `TRUSTED_SIGNING_*` secrets of ADR-0120. Values are not written into the repository.
- **Azure trust:** federated credential `github-monitor-signing` with the immutable subject
  `repo:hardwario@28539912/monitor@1444229107:environment:signing`.
- **Gate for people:** no required reviewers on `signing` (as in the other repositories); the draft release
  (ADR-0125) stays the human check, and tag protection (`packaging/RELEASE.md`) stays a precondition.
- The `publish` job has no environment. Checksums stay one `SHA256SUMS` (ADR-0125), written after signing.

This supersedes, in ADR-0120, the environment `release`, its required reviewers and the secret names, and in
ADR-0125 the job layout (signing inside the build jobs). Everything else in both stays.

## Consequences

- A compromised dependency, compiler plugin or Qt tool in a build job can no longer reach the signing identity.
  What remains in the sign jobs: create-dmg (pinned by hash), WiX from NuGet (pinned by version), CPack from the
  runner image.
- The first release tag is the first run of both sign jobs: the Windows trust works only for GitHub's OIDC token,
  and notarization needs the real certificate. PR CI covers the MSI-from-tree path and the ad-hoc macOS signature.
- Renewing the Developer ID certificate (2027-11-18) means a new `.p12` in every repository that signs.
- Moving the repository keeps the immutable subject; a new repository or environment needs a new federated
  credential (the Entra app takes at most 20).
