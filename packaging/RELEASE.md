# Release checklist (plan/features/0015 F3, plan/features/0031, ADR-0125, ADR-0119, ADR-0120, ADR-0206, ADR-0224)

A release is the exact output of `.github/workflows/release.yml` for a `v2.X.Y[-pre]` tag: it builds, signs,
checksums and attests the packages and creates a **draft** GitHub release. A maintainer reviews and publishes
it. Nothing in this repository creates tags or publishes releases by itself.

## One-time repository setup (maintainer, repository settings)

- [x] Environment `signing`, deployments only from tags `v*`, with the five variables of
      [README.md](README.md#signing-environment-signing-adr-0120-adr-0224) (2026-11-03).
- [x] Azure Artifact Signing: account `hardwario` and profile `hardwario` exist (HARDWARIO a.s. validated); federated
      credential `github-monitor-signing` of the app `github-codesign` for
      `repo:hardwario@28539912/monitor@1444229107:environment:signing` (2026-11-03).
- [x] Apple: Developer ID Application certificate (valid until 2027-11-18) and an App Store Connect API key for
      notarytool exist.
- [x] The three Apple secrets in the environment `signing` (`APPLE_CERTIFICATE`, `APPLE_CERTIFICATE_PASSWORD`,
      `APPLE_API_KEY_P8`, commands in [README.md](README.md#signing-environment-signing-adr-0120-adr-0224)); check
      with `gh secret list --env signing --repo hardwario/monitor` (2026-11-03).
- [ ] Tag protection (repository rule): only maintainers may create tags matching `v2.*`. Until it exists, anyone
      with write access can push a tag and have the commit signed.
- [ ] Optional: required reviewers on the environment `signing` (every release then waits for an approval of the
      two sign jobs).

## Release gates (all must hold before tagging)

- [ ] `check_translations`: 0 unfinished entries in every language (cs, de, pl, sk; ADR-0208; the gate job fails
      otherwise).
- [ ] CI is green on the commit to be tagged (`main` or the release branch), including the three
      `package-<os>` jobs.
- [ ] Manual verification matrix of plan 0015 (F4) on the release candidate: Ubuntu 22.04 AppImage, Ubuntu
      24.04 / Debian 13 .deb, Fedora AppImage, macOS 13.3 Intel and macOS 15 Apple silicon DMG, Windows 10 22H2 and
      Windows 11 MSI (install, upgrade, downgrade refusal, uninstall, silent install, SmartScreen, J-Link with
      Defender), the crash notice after `HWIO_TEST_CRASH=1` (debug build). Results are recorded in plan 0015.
- [ ] User docs match the release (plan 0029, ADR-0222): every command and option of `docs/en/cli.md` against
      `hwio … --help`, every error id of `error.h` in each `docs/<lang>/troubleshooting.md`, package names and
      minimum OS versions in `README.md` and `docs/*/install.md`.
- [ ] Release notes draft: minimum OS versions, "Install SEGGER J-Link Software 8.00 or newer separately", the
      side-by-side period with v1 (until M8).
- [ ] Product name (ADR-0237): the notes say **HARDWARIO Monitor**. The "2" appears only inside a full version number
      ("HARDWARIO Monitor 2.0.0") or as "version 2" — never "HARDWARIO Monitor 2" as a name. `product_name_check`
      (ctest) checks `README.md`, `docs/` and `packaging/release-notes/` from v2.0.0-rc.3 on.

## Tagging and publishing

1. Tag the commit: `git tag -s v2.X.Y -m "HARDWARIO Monitor 2.X.Y"` (pre-releases: `v2.X.Y-rc.N`), push the tag.
2. `release.yml` runs: gate → tests (ci.yml) → linux / macos / windows (unsigned builds) → sign-macos /
   sign-windows (environment `signing`) → publish (draft release with all assets, `SHA256SUMS`, attestations). A
   failed sign job: fix the cause and `gh run rerun <run id> --failed`.
3. Review the draft: assets (`hwio-monitor-<v>-x86_64.AppImage`, `hardwario-monitor_<v>_amd64.deb`,
   `hwio-monitor-<v>-macos-universal.dmg`, `hwio-monitor-<v>-windows-x64.msi`, `SHA256SUMS`,
   `hwio-monitor-<v>-debug-symbols-{linux,macos,windows}.zip`, ADR-0232), the generated notes, the pre-release flag
   (set automatically for tags with a `-`).
4. Spot checks: `sha256sum -c SHA256SUMS`; `gh attestation verify <file> --repo hardwario/monitor`;
   macOS `packaging/macos/sign-and-notarize.sh verify-dmg <dmg>` (the DMG and the app inside it: signature,
   stapled ticket, Gatekeeper); Windows `signtool verify /pa <msi>` and the verification lines of the
   `sign-windows` job (both executables in the tree and inside the MSI, the MSI).
5. Publish the release. The update check (plan 0016) announces it to users within 24 h (v2 releases only;
   pre-releases only to users who opted in).

## After the release

- Keep the symbol archives with the release (crash dumps are symbolized with them, ADR-0123).
- If a release must be withdrawn: unpublish it (back to draft) on GitHub; never delete or move a tag.
