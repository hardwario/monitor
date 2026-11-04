# Packaging (plan/features/0015, plan/features/0031, ADR-0115 … ADR-0126, ADR-0177, ADR-0224)

How HARDWARIO Monitor v2 is installed, packaged and signed. The release procedure is in
[RELEASE.md](RELEASE.md).

| Package | OS | Built by | Signed |
|---|---|---|---|
| `hwio-monitor-<v>-x86_64.AppImage` | Ubuntu 22.04+ and other glibc ≥ 2.35 distributions | `packaging/linux/build-packages.sh` (container) | no (SHA256SUMS + provenance) |
| `hardwario-monitor_<v>_amd64.deb` | Ubuntu 24.04+ / Debian 13+ | same build tree, CPack DEB | no (SHA256SUMS + provenance) |
| `hwio-monitor-<v>-macos-universal.dmg` | macOS 13.3+ (arm64 + x86_64) | `macos-package` preset, `packaging/macos/sign-and-notarize.sh` | Developer ID, notarized, stapled |
| `hwio-monitor-<v>-windows-x64.msi` | Windows 10 22H2+ x64 | `windows-package` preset, installed tree, `packaging/windows/build-msi.ps1` (CPack WIX, WiX ≥ 4) | Azure Artifact Signing (executables and MSI) |

`<v>` is the version from the git tag (`cmake/HWioVersion.cmake`; build metadata `+` is written as `.` in file
names). No package contains SEGGER J-Link Software, its USB driver or nRF Util (ADR-0006, ADR-0016).

## Layout

```
cmake/HWioPackaging.cmake      install rules per OS, HWIO_PACKAGE_KIND, CPack, installed-tree CTest
cmake/HWioDeploy.cmake.in      Qt deployment at install time (Linux: explicit plugin / QML module lists;
                              macOS: macdeployqt; Windows: windeployqt), Qt catalogs of cs/de/pl/sk
cmake/HWioInstalledTreeTest.cmake  CTest Packaging.InstalledTreeSelfTest
packaging/common/icons/       the v1 icon (hwio-monitor.icns / .ico from branch main) + hicolor PNGs (extract.sh)
packaging/common/licenses/    third-party licence texts; licenses.cmake installs them + generated notices
packaging/linux/              Containerfile, build-packages.sh, build-appimage.sh, check-packages.sh,
                              smoke-clean.sh, desktop file, AppStream metainfo, udev rule, deb/, appimage/AppRun
packaging/macos/              entitlements.plist, sign-and-notarize.sh, dmg/background.png
packaging/windows/            version.rc.in (icon + VERSIONINFO), wix/ (CPack config, template, features), build-msi.ps1,
                              sign.ps1
```

`HWIO_PACKAGE_KIND` (`dev` | `appimage` | `deb` | `macos-bundle` | `msi`) is compiled into the binaries
(`hwio --version --json` → `"package"`, Settings › About, the diagnostic report). Normal builds are `dev`; the
`*-package` presets set the kind (Release, no tests, debug info for the symbol archives).

## Installed tree and self-test

`cmake --install <build> --prefix <dir>` gives a self-contained tree on every OS (Qt shared and replaceable,
ADR-0119). The CTest `Packaging.InstalledTreeSelfTest` (all presets with tests) installs into
`<build>/installed-tree`, then runs `hwio --version --json` and `hwio-monitor --self-test` (hidden option: loads
the main QML and exits 0) with `QT_QPA_PLATFORM=offscreen` and the Qt search paths cleared.

## Linux (ADR-0118, ADR-0121)

The packages are built in an Ubuntu 22.04 container (glibc 2.35, LLVM 21, **g++-13 / libstdc++ 13** — the
regression check of ADR-0177, Qt 6.8.3 from aqtinstall, CMake from Kitware, linuxdeploy / appimagetool / type-2
runtime with SHA-256 checks). Locally (Docker or Podman):

```sh
docker build -t hwio-monitor-build:22.04 -f packaging/linux/Containerfile packaging/linux
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:$PWD" -w "$PWD" hwio-monitor-build:22.04 \
    packaging/linux/build-packages.sh build/packages        # .deb, then AppImage (same build tree)
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:$PWD" -w "$PWD" hwio-monitor-build:22.04 \
    packaging/linux/check-packages.sh build/packages        # glibc ceiling, lintian, desktop, AppStream
packaging/linux/smoke-clean.sh build/packages               # clean ubuntu:22.04 / 24.04 containers
```

In a git worktree the main repository's `.git` directory must be mounted too (the version comes from
`git describe`). The full test suite in the container: `cmake --preset linux-debug -B build/c2204-debug &&
cmake --build build/c2204-debug && ctest --test-dir build/c2204-debug` (with `QT_QPA_PLATFORM=offscreen`).

- **.deb:** `/opt/hardwario-monitor/{bin,lib,plugins,qml,translations,licenses}` (private Qt, `$ORIGIN/../lib`),
  links `/usr/bin/hwio-monitor` and `/usr/bin/hwio`, desktop file, metainfo, hicolor icons, man pages, the J-Link
  udev rule `/usr/lib/udev/rules.d/60-hardwario-jlink.rules` (postinst/postrm reload udev; errors are ignored in
  containers). Dependencies from `dpkg-shlibdeps` (incl. `libstdc++6 (>= 13.1)`, `libxcb-cursor0`) plus `bluez`.
  lintian must be clean; overrides only for `/opt` and the private Qt (`deb/lintian-overrides`).
- **AppImage:** AppDir from the install tree, linuxdeploy without its Qt plugin for the remaining libraries
  (`libdbus-1` stays bundled), libstdc++ / libgcc_s in `usr/optional` with the checkrt in `AppRun` (used only when
  newer than the system's), static type-2 runtime, no update information (ADR-0116). `AppRun` starts `hwio` when
  called through a link named `hwio` or with `hwio` as the first argument, otherwise the GUI. Without FUSE:
  `--appimage-extract-and-run` or `APPIMAGE_EXTRACT_AND_RUN=1`.

## macOS (ADR-0120, ADR-0126)

```sh
cmake --preset macos-package && cmake --build --preset macos-package
cmake --install build/macos-package --prefix "$PWD/build/macos-install"   # macdeployqt (absolute prefix)
packaging/macos/sign-and-notarize.sh sign "build/macos-install/HARDWARIO Monitor.app" -    # ad hoc
packaging/macos/sign-and-notarize.sh dmg  "build/macos-install/HARDWARIO Monitor.app" out.dmg -
```

Signing is inside-out (frameworks, plugins and QML plugins, then `Contents/MacOS/hwio`, then the bundle), with the
hardened runtime, `--timestamp` (not ad hoc) and `entitlements.plist` (only
`com.apple.security.cs.disable-library-validation`, so that SEGGER's JLinkARM library and its copies load). The
`verify` step checks the strict signature, the hardened runtime and the entitlements of both binaries and that
the Bluetooth usage descriptions survived (ADR-0086). The DMG is made with create-dmg (pinned release archive,
SHA-256 checked).

The release (job `sign-macos`, ADR-0224) signs with the Developer ID, then `notarize-app` zips the bundle with
`ditto`, notarizes it with `xcrun notarytool` (App Store Connect API key) and staples the ticket to the app, so the
copy in `/Applications` carries it too. The DMG is built from the stapled app, signed, notarized and stapled
(`notarize`), and `verify-dmg` mounts it and checks the DMG and the app inside it (`codesign --verify --strict`,
`stapler validate`, `spctl`). A rejected submission prints Apple's `notarytool log`.

## Windows (ADR-0117, ADR-0120)

From a VS developer prompt (the MSVC runtime DLLs are copied app-local from `VCToolsRedistDir`), with CMake ≥ 3.30
and WiX ≥ 4 (`dotnet tool install --global wix --version 5.0.2` and
`wix extension add -g WixToolset.UI.wixext/5.0.2`):

```powershell
cmake --preset windows-package; cmake --build --preset windows-package
cmake --install build/windows-package --prefix "$PWD/build/windows-install"        # windeployqt (absolute prefix)
packaging/windows/sign.ps1 -DryRun -Files build/windows-install/hwio-monitor.exe,build/windows-install/hwio.exe
packaging/windows/build-msi.ps1 -BuildDir build/windows-package -Tree build/windows-install -OutDir build/packages
wix msi validate build/packages/hwio-monitor-<v>-windows-x64.msi
```

`build-msi.ps1` runs CPack WIX with the build tree's `CPackConfig.cmake` but packages the installed tree as it is
(`CPACK_INSTALL_CMAKE_PROJECTS` empty, `CPACK_INSTALLED_DIRECTORIES`): no compiler, no windeployqt and no
dependency's CMake code runs. That is how the release signs the executables first and then builds the MSI from
the signed files in the sign job (ADR-0224); PR CI uses the same script on the unsigned tree.
`CPackConfig.cmake` holds absolute paths of the configure run, so the script runs in the same workspace path.

The MSI is per-machine (`%ProgramFiles%\HARDWARIO\Monitor`), with a fixed UpgradeCode (major upgrades; a
downgrade shows "A newer version of HARDWARIO Monitor is already installed."), the features "Add hwio to PATH"
(on) and "Desktop shortcut" (off; `msiexec /i <msi> /qn ADDDESKTOPSHORTCUT=1`) and a Start-menu shortcut with the
AppUserModelID `com.hardwario.monitor`. The release (job `sign-windows`) signs `hwio-monitor.exe` and `hwio.exe` in
the installed tree, builds the MSI from it, signs the MSI, extracts it (`msiexec /a`) and checks that all four
executables and the MSI have a valid HARDWARIO a.s. signature with a timestamp. Qt DLLs and the MSVC runtime keep
their vendors' signatures.

## CI (ADR-0125)

- `ci.yml` (pull requests, `main`): `package-linux`, `package-macos`, `package-windows` build unsigned packages
  after the normal jobs, run the checks above and upload them as artifacts for 7 days.
- `release.yml` (tags `v2.*.*`): gate (version = tag, 0 unfinished translations in every language,
  `HWIO_TEST_OVERRIDES=OFF`, the whole `ci.yml` through `workflow_call`); build jobs `linux`, `macos`, `windows`
  (unsigned bundle and installed tree as `unsigned-*` artifacts, symbol archives); sign jobs `sign-macos`, `sign-windows`; `publish`
  (every package present, `SHA256SUMS`, build provenance attestations, **draft** release). See
  [RELEASE.md](RELEASE.md).
- Only the sign jobs run in the environment `signing` and see its secrets or an OIDC token. They run this
  repository's packaging scripts and the packaging tools (create-dmg, CPack, WiX), never a compiler or a
  dependency's build code (ADR-0224).

## Signing (environment `signing`, ADR-0120, ADR-0224)

The GitHub environment **`signing`** admits deployments only from tags matching `v*`; only the jobs `sign-macos` and
`sign-windows` use it. The same setup (names, Azure app, Apple certificate) is used by every HARDWARIO repository
that signs in GitHub Actions.

| Name | Kind | Use |
|---|---|---|
| `AZURE_CLIENT_ID` | variable | client id of the Entra app `github-codesign` (OIDC federated credentials, no client secret) |
| `AZURE_TENANT_ID` | variable | HARDWARIO's Azure tenant |
| `AZURE_SUBSCRIPTION_ID` | variable | subscription of the Artifact Signing account |
| `APPLE_API_KEY` | variable | key id of the App Store Connect API key used by notarytool |
| `APPLE_API_ISSUER` | variable | issuer id of the Apple team `C32QFQ68V9` |
| `APPLE_CERTIFICATE` | secret | "Developer ID Application: HARDWARIO a.s. (C32QFQ68V9)" with its private key, `.p12`, base64 |
| `APPLE_CERTIFICATE_PASSWORD` | secret | password of the `.p12` |
| `APPLE_API_KEY_P8` | secret | content of the API key's `.p8` file |

The values are set in the environment (repository settings); ask Pavel Hübner for them and for the key files.

**Windows (Azure Artifact Signing):** account `hardwario`, endpoint `https://weu.codesigning.azure.net/`,
certificate profile `hardwario` (Public Trust, `CN=HARDWARIO a.s., O=HARDWARIO a.s., L=Liberec, S=Liberecký, C=CZ`).
These names are fixed in `release.yml` and are the defaults of `sign.ps1`. Microsoft issues certificates valid for about three days and rotates them itself, so every signature carries an
RFC 3161 timestamp (`http://timestamp.acs.microsoft.com`); nothing to renew. The job logs in with GitHub OIDC
through the federated credential `github-monitor-signing` of the app `github-codesign`, subject
`repo:hardwario@28539912/monitor@1444229107:environment:signing` (the repository sends the immutable subject with
owner and repository ids; a rename keeps it, a new repository or environment needs a new credential, otherwise
the login fails with `AADSTS700213`). The app's only role is "Artifact Signing Certificate Profile Signer" on the
profile `hardwario`.

**macOS:** the Developer ID Application certificate expires on **2027-11-18**. Renewal: the team's Account Holder
creates a new certificate from a certificate signing request in developer.apple.com (the App Store Connect API
cannot create Developer ID certificates), then the new `.p12` goes into `APPLE_CERTIFICATE` and
`APPLE_CERTIFICATE_PASSWORD` of every repository that signs. The `.p12` must be readable by `security import`
(LibreSSL `/usr/bin/openssl`, or OpenSSL 3 with `-legacy`) and include the intermediate "Developer ID
Certification Authority" (G2). The API key also serves other build systems: never revoke it as part of a rotation
here. There is no Developer ID Installer certificate, so no `.pkg`.

Whoever has the `.p12`, its password and the `.p8` as files sets the secrets without the values passing through a
terminal log, then deletes the files:

```sh
base64 -i <path>/HARDWARIO-Developer-ID-Application.p12 | gh secret set APPLE_CERTIFICATE --env signing --repo hardwario/monitor
gh secret set APPLE_CERTIFICATE_PASSWORD --env signing --repo hardwario/monitor < <path>/password.txt
gh secret set APPLE_API_KEY_P8 --env signing --repo hardwario/monitor < <path>/AuthKey_<key id>.p8
gh secret list --env signing --repo hardwario/monitor
```

PR builds never see these values and are never signed.

## Troubleshooting

- **Container build fails at apt.llvm.org / the PPA:** the LLVM 21 and g++-13 packages must stay available for
  jammy (risk in plan 0015); pin a mirror or rebuild the cached image when they move.
- **`hwio-monitor --self-test` fails in the installed tree:** run it with `QT_DEBUG_PLUGINS=1` and check
  `qml/` against `HWIO_LINUX_QML_MODULES` / `HWIO_LINUX_QT_PLUGINS` (cmake/HWioPackaging.cmake).
- **AppImage does not start without FUSE:** `--appimage-extract-and-run`.
- **notarytool: Invalid:** the sign job prints `xcrun notarytool log <submission id>`; it names the file and the
  problem (usually an unsigned Mach-O, a missing hardened runtime or a missing secure timestamp).
- **`security import` fails with a MAC error:** the `.p12` uses OpenSSL 3 defaults; rebuild it with LibreSSL or
  `-legacy`.
- **`AADSTS700213 … presented assertion subject 'repo:…'`:** the federated credential does not match the subject
  GitHub sends; compare the subject in the error with the one above and with the federated credentials of
  `github-codesign` (`az ad app federated-credential list`).
- **SmartScreen still warns ("More info", "Run anyway"):** a new certificate has no reputation yet; not a signing
  failure.
- **A failed sign job:** fix the cause and rerun only the failed jobs (`gh run rerun <run id> --failed`); the
  release stays a draft until a maintainer publishes it.
- **WiX errors about extensions:** the WiX extension version must match the WiX tool version (`WIX_VERSION`).
