# 0038 — nRF Util install assist (LTE flashing prerequisite)

- **Status:** In progress (approved by the user 2026-11-05 with all recommended options; A0 for rc.3)
- **Milestone:** 2.0.0-rc.3 (part A0) / 2.0.0-rc.4 (part A1) / later (part B), see the split below (D11)
- **Author:** Matej Strnad (with Claude), 2026-11-05
- **Related ADRs:** ADR-0006, ADR-0016, ADR-0027, ADR-0042, ADR-0114, ADR-0237, ADR-0238, ADR-0248, ADR-0257 (worker
  pattern), ADR-0258, ADR-0259; accepted here: ADR-0262 … ADR-0270
- **Depends on:** [0033](0033-rc2-feedback.md) (LTE update wizard), [0037](0037-docs-links.md) (docs topic table,
  `DocsAnchors` test)
- **Amends:** [0006](0006-m3-flash-settings.md) (Settings → nRF Util section, Flash prerequisite banner),
  [0008](0008-m4-first-run-setup.md) step 4 "nRF Util (optional)" as rebaselined by
  [0035](0035-wizards-rebaseline-2-0-0.md) (only if decision D9 says so)

## Context

Team feedback on rc.2 (2026-11-05): "the installation guide for LTE flashing is too complicated". Today the user
must:

1. open Nordic's product page, find and download the `nrfutil` executable;
2. move it into a directory on the `PATH` (and `chmod +x` it on macOS / Linux);
3. open a terminal and run `nrfutil install device`.

The in-app prerequisite banners (Flash page step 1 `KindCards.qml`, LTE update wizard step 1 `LteUpdateStep.qml`)
offer only **Set path in Settings** and **How to install ↗**, which opens the Nordic product page
(`SettingsViewModel::nrfUtilInstallUrl()`), not our docs. The docs section `install.md#nordic-nrf-util-lte-modem-firmware-only`
says "Install `nrfutil` from Nordic Semiconductor, then its `device` command" without a link or per-OS steps.

The user chose **"the app helps"**: a short step-by-step guide per OS with a direct download link; the app finds the
downloaded `nrfutil` (e.g. in the Downloads folder) and installs the missing `device` command itself with a button.
Everything else in the app keeps working without nRF Util.

### Facts verified for this plan

| Fact | Source |
|---|---|
| Direct downloads (Nordic's own install guide, "curl" variant): Windows x64 `https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-pc-windows-msvc/nrfutil.exe`; Linux x64 `…/executables/x86_64-unknown-linux-gnu/nrfutil`; Linux ARM64 `…/executables/aarch64-unknown-linux-gnu/nrfutil`; macOS (universal) `…/executables/universal-apple-darwin/nrfutil`. Manual download page: `https://www.nordicsemi.com/Products/Development-tools/nrf-util`. Windows alternative: `winget install nrfutil` (adds `nrfutil` to the PATH of new terminals). No Windows ARM64 build is listed (x64 runs under emulation). | Nordic docs "Installing nRF Util from the web (default)" (docs.nordicsemi.com, nrfutil bundle, `guides/installing.html`), read 2026-11-05 |
| The downloaded file is a launcher: on its first run it **bootstraps** the core (`self-upgrade --from-tarball …/.nrfutil/bootstrap/…`) into `$NRFUTIL_HOME` (default `~/.nrfutil`, `%USERPROFILE%\.nrfutil` on Windows), creating `~/.nrfutil/bin/nrfutil`. The bootstrap needs internet unless `NRFUTIL_BOOTSTRAP_TARBALL_PATH` is set. Error when it cannot: "Failed to bootstrap core functionality before executing command". | Nordic docs "Environment variables" / "Installing a specific version"; nRF DevZone thread 121123; the nrfutil log of the developer machine (`~/.nrfutil/logs/nrfutil.log`, first entry is the bootstrap self-upgrade) |
| `nrfutil install device` downloads `nrfutil-device` from `https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/…` into `~/.nrfutil/bin/nrfutil-device` (+ `lib/`, `share/`, `installed/`). It took ~2 s on the developer machine. Options: `--force`, `--from-offline <PATH>`, `--tarball <PATH>`, `--json` (JSON Lines), global `--detect-proxy` (libproxy). | `nrfutil install --help`, `nrfutil --help` (nrfutil 8.2.1, device 2.23.0) and the same log |
| **Our locator already searches `~/.nrfutil/bin`** (`defaultLocatorOptions().installDirs`, also `%USERPROFILE%\.nrfutil\bin`). On the developer machine `~/.nrfutil/bin` is **not** on the PATH and the app finds nRF Util there. ⇒ After the launcher has run once, **no PATH change is needed** for HARDWARIO Monitor. | `libs/hw/nrfutil/src/nrfutil.cpp` `defaultLocatorOptions`; `ls ~/.nrfutil/bin` |
| `NRFUTIL_HOME` is not honoured by our locator today. | same file |
| Minimum versions in the app: core 7.0.0, device 2.0.0 (provisional, ADR-0042 `_tbd_`). A fresh install is far newer (8.2.1 / 2.23.0). | `nrfutil.h` |
| A fresh launcher's first `--version` triggers the bootstrap (download). Our version probe has a 10 s timeout, so pointing **Custom path** at a never-run launcher can time out or fail offline today. | code + bootstrap behaviour above |

Not verified (Phase 0 spike, see Implementation steps): whether the macOS binary is signed and notarized (Gatekeeper
behaviour of a quarantined, browser-downloaded file started by our app or from Terminal); the JSON Lines of
`nrfutil install device --json` (progress events or only a final result); the Windows Authenticode signer name; the
exact behaviour of the bootstrap on Windows/macOS (we expect `~/.nrfutil/bin/nrfutil[.exe]` as on Linux).

## Goals / Non-goals

**Goals**

- G1 A per-OS guide of at most three steps in the docs (5 languages) and in the app: download (direct link) → (the app)
  install the device command → done. No PATH editing, no terminal needed for the normal case.
- G2 The app finds a downloaded `nrfutil` in the user's Downloads folder (on request), shows it, and on one click makes
  it executable and runs `nrfutil install device` with progress and Cancel. Afterwards the normal detection finds
  `~/.nrfutil/bin/nrfutil` and the LTE flash is enabled without restart.
- G3 The same for an existing nRF Util without the `device` command (`dependency.nrfutil_device_missing`): one click.
- G4 CLI equivalent (`hwio nrfutil install-device`), same backend.
- G5 Failures (offline, proxy, Gatekeeper, not executable, cancelled, timeout) → `hwio::Error` with what to do.
- G6 Tests on the fake nRF Util only; no network in tests.

**Non-goals**

- The app never downloads nrfutil itself and never bundles or copies it (ADR-0006, ADR-0016 stay; see D1).
- No automatic upgrade of a too-old nRF Util in part A (`dependency.nrfutil_too_old` keeps showing the commands);
  part B may add a button for `self-upgrade` + `upgrade device`.
- No offline / air-gapped install support beyond a docs sentence.
- No change to the modem update itself, to ADR-0042 minimums, or to the locator precedence (except `NRFUTIL_HOME`, D8).
- No MCP tool (D10).

## Scope split (decision D11: user, 2026-11-05 — option b)

**Part A0 — rc.3 (today): docs and links only, no new code path that runs a program.**

- A0.1 Docs: rewrite `install.md` § Nordic nRF Util in en/cs/de/sk/pl as short per-OS numbered steps (direct
  Nordic download → run it once with `install device` from the Downloads folder; macOS "Open Anyway" note per D4;
  Windows `winget` alternative); no PATH step; search order / `--nrfutil` / `HWIO_NRFUTIL` / upgrade in an
  "Advanced" paragraph. Anchor unchanged. `flashing.md` / `troubleshooting.md` only if they contradict.
- A0.2 In-app links: **How to install ↗** opens our docs topic `InstallNrfUtil` via `AppViewModel.docsLink`
  (language-aware, ADR-0247) instead of the Nordic product page; new **Download nRF Util ↗** opens the direct per-OS
  URL (`nrfUtilDownloadUrl`, chosen in the ViewModel: compile-time OS + `QSysInfo::currentCpuArchitecture()`) on the
  Flash banner, the LTE wizard banner and Settings → nRF Util.
- A0.3 Tests (unit + Qt Quick), translations cs + de + sk + pl (0 unfinished).

**Part A1 — rc.4: dialog, backend, CLI** (the rest of the former part A):

- A1.1 Backend `libs/hw/nrfutil`: `downloadUrl()` (the A0 table moves here), `findDownloaded()`,
  `prepareExecutable()`, `installDeviceCommand()`; locator honours `NRFUTIL_HOME` (D8).
- A1.2 Error `dependency.nrfutil_install_failed` (D6).
- A1.3 GUI: the **Install nRF Util** dialog (D7) opened from the Flash banner, the LTE wizard banner and
  Settings → nRF Util; `NrfUtilInstallViewModel` + an owned worker thread.
- A1.4 CLI: `hwio nrfutil install-device [PATH]`, `hwio nrfutil download-url` (D5).
- A1.5 Docs: "The easy way (in the app)" paragraph, `cli.md`, troubleshooting entry for the new error.
- A1.6 Translations cs + de + sk + pl (ADR-0248), tests, manual checks on 3 OSes.

**Part B — later (rc.4 / 2.0.0):** signature verification of the candidate (Windows Authenticode signer, macOS
Team ID; D3 option b), **Upgrade nRF Util** button for `too_old`, setup wizard step 4 uses the dialog (D9),
optional `--detect-proxy` retry.

## User experience

### Entry points

| Where | Today | New (part A1, rc.4; A0 only adds [Download nRF Util ↗] and retargets [How to install ↗]) |
|---|---|---|
| Flash page step 1 banner (`KindCards.qml`, LTE kind, nRF Util problem) | [Set path in Settings] [How to install ↗] | **[Install nRF Util…]** (primary) · [Set path in Settings] · [How to install ↗] (our docs) |
| LTE update wizard step 1 banner (`LteUpdateStep.qml`) | same | same as Flash |
| Settings → nRF Util (`NrfUtilSection.qml`), state Problem | [How to install ↗] [Check again] | **[Install nRF Util…]** · [How to install ↗] (our docs) · [Check again] |

For `dependency.nrfutil_too_old` the button is not shown in part A (only the commands, as today).

### Dialog "Install nRF Util" (modal, ~560 px wide, scrolls at 960 × 600)

Heading: **Install nRF Util**. Intro: "nRF Util from Nordic Semiconductor is needed only to update the LTE modem
firmware. Everything else works without it."

The dialog has two states, chosen from the current detection result:

**State 1 — nRF Util not found** (`dependency.nrfutil_not_found`):

> **1. Download nRF Util**
> "Download the nrfutil program for <Windows | macOS | Linux (x86-64) | Linux (ARM64)> from Nordic Semiconductor. Save
> it in your Downloads folder."
> [Download nRF Util ↗] — opens the direct URL in the browser. Small text: "Other systems: nRF Util download page ↗"
> (Nordic product page).
>
> **2. Install the device command**
> Before a search: "When the download has finished, click Find download." [Find download] · ghost [Choose file…]
> (FileDialog in the Downloads folder).
> Found: "Found: `~/Downloads/nrfutil` · 7.4 MB · today 10:42" (newest match; ghost [Choose another file…]).
> Not found: "No nrfutil in <Downloads path>. Is the download finished? You can also choose the file." (Chrome/Edge
> partial download `nrfutil*.crdownload` / Firefox `.part` → "The download has not finished yet.")
> Primary **[Install device command]** — enabled when a candidate is set.
> Below the button: "This runs the downloaded nrfutil once. It installs itself and the device command into
> <~/.nrfutil> from files.nordicsemi.com (internet needed, about 50 MB). You can delete the downloaded file afterwards."
>
> **3. Done** (see Running / Result).

**State 2 — nRF Util found but the device command is missing** (`dependency.nrfutil_device_missing`):

> "nRF Util <8.2.1> was found at <path>, but without its device command."
> Primary **[Install device command]** (runs the located nrfutil).

**Running:** indeterminate progress bar (determinate when `--json` gives percentages), step text "Installing nRF Util
(first run)…" / "Installing the device command…", [Cancel]. The dialog cannot be closed except through Cancel. Cancel
asks nothing; text after cancel: "Cancelled. Click Install device command to try again."

**Result:** success → "nRF Util <8.2.1> with device command <2.23.0> is ready." [Close] (primary). The dependency status
refreshes (`DependencyStatus::refresh()`), so the Flash page / LTE wizard banner disappears and Flash becomes enabled
without restart (ADR-0114 provider picks up the new location for open sessions). Failure → the standard `ErrorPanel`
(message, cause, suggestion, Learn more) + [Try again].

macOS, blocked by Gatekeeper (D4): error text "macOS blocked the downloaded nrfutil." / "It was downloaded from the
internet and macOS did not allow it to run." / "Open System Settings › Privacy & Security, click Open Anyway for
nrfutil, then click Try again." (exact wording after the Phase 0 spike; if the binary is notarized this never shows).

All strings via `qsTr` / `tr`, English source, cs + de + sk + pl (ADR-0027, ADR-0248). Plain-text labels for paths.

### Docs (en; translated to cs, de, sk, pl; anchor `nordic-nrf-util-lte-modem-firmware-only` kept)

Replace the section with:

```markdown
## Nordic nRF Util (LTE modem firmware only)

nRF Util updates the modem firmware of the LTE MCU. Everything else works without it.

**The easy way (in the app):** on the Flash page choose **LTE firmware**, click **Install nRF Util…** and follow the
two steps: download nrfutil, then **Install device command**. The app finds the download in your Downloads folder.

**By hand:**

1. Download nrfutil for your system: [Windows][nrfutil-win] · [macOS][nrfutil-mac] · [Linux x86-64][nrfutil-linux] ·
   [Linux ARM64][nrfutil-linux-arm] (other systems: [nRF Util download page][nrfutil-page]).
2. Run it once from the Downloads folder (it installs itself and the device command into `.nrfutil` in your home
   folder; internet needed):
   - Windows (PowerShell): `& "$HOME\Downloads\nrfutil.exe" install device`
   - macOS / Linux (Terminal): `chmod +x ~/Downloads/nrfutil && ~/Downloads/nrfutil install device`
3. Click **Check again** in **Settings → nRF Util**, or run `hwio doctor`.

You do not need to add nrfutil to the `PATH`: HARDWARIO Monitor also looks in `~/.nrfutil/bin`
(`%USERPROFILE%\.nrfutil\bin` on Windows). On Windows you can instead run `winget install nrfutil` and then
`nrfutil install device` in a new terminal. Command line: `hwio nrfutil install-device`.

HARDWARIO Monitor looks for nRF Util in **Settings → nRF Util → Path** (or `--nrfutil PATH`, or `HWIO_NRFUTIL`), then
on the `PATH`, then in `$NRFUTIL_HOME/bin`, `~/.nrfutil/bin` and `/usr/local/bin`. If it is too old:
`nrfutil self-upgrade` and `nrfutil upgrade device`.
```

(macOS Gatekeeper sentence added after the spike if needed.) `cli.md`: new row in the command table + `--nrfutil`
default text mentions `$NRFUTIL_HOME/bin`.

## CLI

New command group `hwio nrfutil` (D5):

```
hwio nrfutil install-device [PATH] [--yes] [--timeout SECONDS]
```

- `PATH` given → that file. Otherwise: the located nRF Util (global `--nrfutil`, `HWIO_NRFUTIL`, setting, PATH,
  install dirs) if it is found; else the newest `findDownloaded()` candidate in the Downloads folder; else
  `dependency.nrfutil_not_found` with the download URL of this OS in the suggestion and exit 4.
- When the program comes from the Downloads search (not from `PATH`/options/locator), it asks
  `Run <path> to install nRF Util and its device command? [y/N]` on a TTY; without a TTY or with `--json`, `--yes` is
  required (`usage.confirmation_required`, exit 2) — the `confirmDestructive` mechanism (ADR-0038).
- Human output: progress line (`progress_ui`), then `✓ nRF Util 8.2.1, device 2.23.0 · ~/.nrfutil/bin/nrfutil`.
- `--json`: `{"ok":true,"nrfutil":{"path":…,"version":…,"deviceVersion":…},"ranFrom":…}`; errors as every command.
- Exit codes: 0; 2 usage/confirmation; 4 not found / install failed (D6); 8 if D6 maps network failures to
  `network.*`; 9 timeout (default 300 s); 130 cancelled (Ctrl+C kills the child).
- `hwio nrfutil download-url` — prints the direct URL of this OS (optional, trivial; part A if D5 option a).
- `hwio doctor`: the `nrfutil` check's suggestion mentions `hwio nrfutil install-device` (text only).

## Design

### Backend: `libs/hw/nrfutil` (the only place that starts nRF Util)

```cpp
namespace hwio::nrfutil {

// Direct download of the launcher for this OS/CPU (compile-time OS, QSysInfo::currentCpuArchitecture at run time).
// Windows x64/arm64 -> x86_64-pc-windows-msvc; macOS -> universal-apple-darwin; Linux x86_64 / arm64 ->
// x86_64- / aarch64-unknown-linux-gnu; anything else -> nullopt (UI shows only the product page).
[[nodiscard]] std::optional<std::string_view> downloadUrl(std::string_view os, std::string_view cpuArch);
[[nodiscard]] std::optional<std::string_view> downloadUrl(); // this machine
inline constexpr std::string_view productPageUrl = "https://www.nordicsemi.com/Products/Development-tools/nrf-util";

// NRFUTIL_HOME/bin when set, else ~/.nrfutil/bin (+ %USERPROFILE%\.nrfutil\bin on Windows). Used by
// defaultLocatorOptions (D8) and for the result text.
[[nodiscard]] std::vector<std::filesystem::path> homeBinDirs();

struct DownloadCandidate {
    std::filesystem::path path;
    std::uintmax_t bytes = 0;
    std::filesystem::file_time_type modified;
    bool partial = false; // *.crdownload / *.part / *.download: download not finished
};
struct FindLimits {
    std::size_t maxEntries = 5000;   // directory entries looked at (bounded)
    std::size_t maxCandidates = 10;
    std::uintmax_t minBytes = 1ULL << 20;   // 1 MB
    std::uintmax_t maxBytes = 300ULL << 20; // 300 MB
};
// Top level of `directories` only (default: QStandardPaths::DownloadLocation), no recursion, no symlinks
// followed. Names (case-insensitive): nrfutil, nrfutil.exe, browser duplicates "nrfutil (1)", "nrfutil(1).exe",
// "nrfutil-1"; partial downloads of these. Newest first. Never executes anything. Errors: none (an unreadable
// directory gives an empty list; detail logged).
[[nodiscard]] std::vector<DownloadCandidate> findDownloaded(std::span<const std::filesystem::path> directories,
                                                            const FindLimits& limits = {});
[[nodiscard]] std::vector<std::filesystem::path> defaultDownloadDirs();

// Regular file, size within limits, (POSIX) owned by the current user and not group/world-writable; adds
// owner-execute (std::filesystem::permissions, perm_options::add) when missing. macOS: reports the
// com.apple.quarantine attribute (D4: not removed). Errors: dependency.nrfutil_not_found (detail: why).
[[nodiscard]] Status prepareExecutable(const std::filesystem::path& candidate);

struct InstallTiming {
    std::chrono::milliseconds overallTimeout{std::chrono::minutes(5)};
    std::chrono::milliseconds terminateGrace{std::chrono::seconds(5)};
    std::chrono::milliseconds poll{100};
    std::size_t maxOutputBytes = 1024ULL * 1024; // per stream
};
struct InstallProgress {
    std::optional<int> percent; // 0..100 when nrfutil reports it
    std::string text;           // last task description (not translated; may be empty)
};
// Runs `<nrfutil> install device --json` (argument list, no shell, inherited environment so proxy variables and
// NRFUTIL_HOME apply). The first run of a fresh launcher bootstraps the core first (same process). On success runs
// checkVersions() on the *located* nrfutil (defaultLocatorOptions with `settings`) and returns it.
// Errors: D6 (install failed: network/bootstrap/other), timeout, cancelled, dependency.nrfutil_not_found (cannot
// start / blocked by Gatekeeper, detail), dependency.nrfutil_* from the final check.
[[nodiscard]] Result<NrfUtilInfo> installDeviceCommand(const std::filesystem::path& nrfutil,
                                                       const LocatorOptions& afterwards,
                                                       const std::function<void(const InstallProgress&)>& progress,
                                                       std::stop_token stop, InstallTiming timing = {});

[[nodiscard]] std::vector<std::string> installArguments(); // {"install","device","--json"} (tests)
}
```

Implementation notes:

- Process handling reuses the file-local `StreamCapture` and the terminate→kill logic; `NrfUtilModemUpdater` is **not**
  changed (rc.3 risk). The common wait loop is copied into a second file-local function, refactoring later.
- JSON Lines parsed with the existing `parseJsonEvent` (tolerant); non-JSON bootstrap lines are only kept in the tail.
- Failure classification from the stderr/stdout tail (case-insensitive substrings, documented in a table in the
  code): "bootstrap", "dns", "connect", "timed out", "proxy", "certificate", "http", "403/404/5xx" → network-ish
  cause; otherwise generic. Exit code + tail go to `detail`.
- After success, the result path is the **located** nRF Util, not the downloaded file; the app never stores the
  Downloads path as the `nrfutil/path` setting (D2). If locating fails after a successful install (unexpected layout,
  e.g. `NRFUTIL_HOME` set in a shell but not for the app), the error is `dependency.nrfutil_not_found` with detail
  "installed, but not found in: …" and the suggestion to set the path in Settings.
- Logging: `lcFlash` info lines for start, result, path; no URLs or proxy settings with credentials are logged
  (the environment is never logged).

### Locator change (D8)

`defaultLocatorOptions()` puts `$NRFUTIL_HOME/bin` (when set) before `~/.nrfutil/bin`. Precedence otherwise unchanged
(ADR-0114 provider unaffected).

### Error cases

| Case | Error id | User message (translated) | Suggestion |
|---|---|---|---|
| No download found / chosen file is not a usable file | `dependency.nrfutil_not_found` (detail: reason) | "nRF Util was not found." | "Download nrfutil and click Find download, or choose the file." (UI text, not the error default) |
| Cannot start (no exec permission after chmod failed, wrong CPU "Exec format error", Gatekeeper kill) | `dependency.nrfutil_not_found` with overridden message "The downloaded nrfutil cannot be started." | per OS (D4) | |
| Bootstrap / download failed (offline, proxy, server) | D6: `dependency.nrfutil_install_failed` (new) **or** `network.download_failed` with overridden message | "Installing nRF Util failed." / cause "There is no internet connection, a proxy blocks files.nordicsemi.com, or the server failed." | "Check the internet connection and try again. Behind a proxy, run the command from the guide in a terminal with --detect-proxy." |
| Other non-zero exit | same as above, generic cause "nrfutil reported an error (see details)." | | "Try again; if it fails again, create a diagnostic report." |
| Timeout (5 min) | `timeout` | default | |
| Cancel | `cancelled` | default | |
| Device command still missing after success | `dependency.nrfutil_device_missing` | default | |

### GUI (app/gui)

- `NrfUtilInstallViewModel` (QObject, QML_ELEMENT, uncreatable): properties `state` (NotFound, DeviceMissing, Ready,
  Searching, Running, Succeeded, Failed, Cancelled), `downloadUrl`, `productPageUrl`, `docsUrl`,
  `downloadsFolder`, `candidatePath`, `candidateText`, `searchText`, `progress` (-1 = indeterminate), `stepText`,
  `resultText`, `error` (ErrorInfo); invokables `find()`, `choose(QUrl)`, `install()`, `cancel()`. Owned by
  `FirmwareServices` (std::unique_ptr, declared after `DependencyStatus`), so the Flash page, the LTE wizard and
  Settings share one install at a time.
- `NrfUtilInstallWorker`: one owned `std::jthread` per run (ADR-0257 pattern), created by `install()`; result handed to
  the GUI thread with a queued call; `cancel()` = `request_stop()`; destructor stops + joins (kills the child). Progress
  coalesced (≤ 10 updates/s). `findDownloaded()` runs on the same kind of short worker (directory listing may be slow
  on network-redirected Downloads), never on the GUI thread.
- On success: `DependencyStatus::refresh()`.
- QML: `app/gui/qml/flash/NrfUtilInstallDialog.qml` (Popup/Dialog, design-system components, `CommandBlock` not
  needed); buttons added to `KindCards.qml`, `LteUpdateStep.qml`, `NrfUtilSection.qml`. `nrfUtilInstallUrl`
  properties now return the docs topic URL (`DocsTopic::InstallNrfUtil`, language-aware).
- Opening the dialog from inside the LTE wizard is allowed (modal over the wizard); the wizard re-evaluates
  `prerequisiteText` on `DependencyStatus::nrfUtilChanged` (already connected).

### Threading and bounds

- One install at a time per app (second `install()` while Running is ignored; the button is disabled).
- Child output ≤ 1 MiB per stream, tail 4 KiB; directory scan ≤ 5000 entries, ≤ 10 candidates; timeout 5 min;
  cancel → terminate, kill after 5 s.
- No vendor binary is shipped, copied or stored; only the user's file is run, after an explicit click (GUI) or a
  confirmation / `--yes` (CLI).

### MCP

None. No tool may run a downloaded program (D10). The existing `firmware_tools` error texts are unchanged.

## Decisions (resolved: user, 2026-11-05)

| # | Question | Options | Recommendation | Decision (by user) | ADR |
|---|---|---|---|---|---|
| D1 | How does nrfutil get onto the machine? | (a) the user downloads it in the browser via our direct link, the app runs it once **(user's choice)**; (b) the app downloads it from files.nordicsemi.com itself (one button, but the app becomes a downloader of an unsigned vendor binary; ADR-0006 change; proxy/TLS handling in our code); (c) docs only | **(a)** | **(a)** — user, 2026-11-05 | ADR-0262 |
| D2 | Use the download in place or copy it? | (a) run it in place once; Nordic's bootstrap installs the real core into `~/.nrfutil/bin`, which our locator already searches; never store the Downloads path; (b) copy it to `~/.nrfutil/bin` ourselves (fights with Nordic's own layout/self-upgrade); (c) store the Downloads path as Custom path (breaks when the user cleans Downloads) | **(a)** | **(a)** — user, 2026-11-05 | ADR-0262 |
| D3 | How do we make sure the file is Nordic's nrfutil before running it? | (a) explicit user action with the path shown + sanity checks (name, size 1–300 MB, regular file, owner, not group/world-writable) + post-run `--version` check; (b) (a) + platform signature check: Windows Authenticode signer "Nordic Semiconductor ASA" (WinVerifyTrust in `libs/hw/nrfutil`), macOS `codesign` Team ID; Linux has no signature; (c) pin SHA-256 hashes (impossible: Nordic updates the file) | **(a) in rc.3, (b) in part B** | **(a) in rc.4, (b) in part B** — user, 2026-11-05 | ADR-0264 |
| D4 | macOS: exec bit and quarantine | (a) app adds owner +x (after the click); does **not** remove `com.apple.quarantine`; if Gatekeeper blocks, show "Open Anyway" instructions; (b) app removes the quarantine attribute after the click (`removexattr`); (c) only instructions for Terminal (`chmod`, `xattr -d`) | **(a)**; revisit after the Phase 0 spike shows whether Nordic's binary is notarized (if it is, nothing extra shows) | **(a)** — user, 2026-11-05 | ADR-0265 |
| D5 | CLI surface | (a) `hwio nrfutil install-device [PATH] [--yes]` (+ `hwio nrfutil download-url`); (b) `hwio doctor --fix` (runs fixes for failed checks; broader, later for USB rules too); (c) no CLI (violates "every backend feature gets a CLI command") | **(a)** | **(a)** — user, 2026-11-05 | ADR-0267 |
| D6 | Error for a failed install | (a) new `dependency.nrfutil_install_failed` (category dependency, exit 4, docs anchor ×5, 3 translated texts ×4); (b) reuse `network.download_failed` with an overridden message (exit 8, no new anchor, but its default texts and docs talk about firmware); (c) reuse `dependency.nrfutil_not_found` with overridden texts | **(a)** — clearer docs, "Learn more" lands on the right entry | **(a)** — user, 2026-11-05 | ADR-0266 |
| D7 | Where does the guided UI live? | (a) one modal dialog opened from the Flash banner, LTE wizard banner and Settings (later the setup wizard step); (b) inline panel in Settings only, banners open Settings (leaves the flow); (c) a new wizard `nrfutil-install` in the wizard framework (heavier, more strings, more tests) | **(a)** | **(a)** — user, 2026-11-05 | ADR-0268 |
| D8 | Honour `NRFUTIL_HOME` in the locator? | (a) yes, `$NRFUTIL_HOME/bin` before `~/.nrfutil/bin`; (b) no | **(a)** (one line + test; without it a user with `NRFUTIL_HOME` gets "installed, but not found") | **(a)** — user, 2026-11-05 | ADR-0270 |
| D9 | Setup wizard (0008 step 4, 0035 in progress) | (a) step 4 gets [Install nRF Util…] opening the same dialog, in part B / when 0035 lands; (b) do it now inside 0035's implementation; (c) leave step 4 as planned (commands only) | **(a)** | **(a)** — user, 2026-11-05 | — (plan amendment) |
| D10 | MCP | (a) no tool; (b) read-only `nrfutil_status` tool | **(a)** — the status is already in diagnostics; running a downloaded program must stay a human action | **(a)** — user, 2026-11-05 | ADR-0262 (consequence) |
| D11 | rc.3 scope | (a) A1–A6 today if the manual checks on 3 OSes pass by the release cut, else A1+A2 only and the rest in rc.4; (b) A1+A2 only in rc.3, A3–A6 in rc.4; (c) everything incl. part B in rc.3 | **(a)** with the fallback; **(b)** if nobody can test macOS + Windows today | **(b): rc.3 = docs + links only (A0); dialog, backend, CLI, new error id in rc.4 (A1); part B later** — user, 2026-11-05 | ADR-0269 |
| D12 | "How to install ↗" target | (a) our docs topic `InstallNrfUtil` (localized, matches the app); (b) keep the Nordic product page | **(a)**; the Nordic page stays as "Other systems" in the dialog/docs | **(a)** — user, 2026-11-05 | ADR-0263 |
| D13 | Find download: when does the app look into Downloads? | (a) only when the user clicks [Find download] (on macOS this may show the "access Downloads folder" privacy prompt; it then appears after a user action and is understandable); (b) automatically when the dialog opens; (c) never, only [Choose file…] | **(a)** | **(a)** — user, 2026-11-05 | ADR-0263 |
| D14 | Show the button for `too_old` too? | (a) no in part A (commands only, as today); (b) yes, running `self-upgrade` + `upgrade device` | **(a)**; (b) in part B | **(a)** — user, 2026-11-05 | — |

## ADR texts (files `plan/decisions/ADR-0262` … `ADR-0270`, Accepted 2026-11-05)

### ADR-0262 — nRF Util install assist runs the user's download once; no bundling, no copying

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Context:** rc.2 feedback: installing nRF Util (download, PATH, chmod, `nrfutil install device`) is too
  complicated. ADR-0006/0016: vendor tools are never shipped. Nordic's downloaded `nrfutil` is a launcher that
  bootstraps its core into `$NRFUTIL_HOME` (default `~/.nrfutil`) on first run; our locator already searches
  `~/.nrfutil/bin`.
- **Options:** (1) user downloads via our direct link, the app runs it once with `install device`; (2) the app
  downloads nrfutil itself; (3) docs only.
- **Decision:** (1). The app runs the downloaded file **in place**, once, only after an explicit user action (GUI
  click; CLI confirmation or `--yes`). It never copies the file and never stores its Downloads path as the
  `nrfutil/path` setting; afterwards the normal locator finds `~/.nrfutil/bin/nrfutil`. No MCP tool runs it.
- **Consequences:** ADR-0006/0016 unchanged (nothing shipped). The app depends on Nordic's bootstrap layout; if
  Nordic changes it, the app reports "installed, but not found" and the user can set the path. New functions in
  `libs/hw/nrfutil` only.

### ADR-0263 — Finding the downloaded nrfutil and the install links

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Context:** The app must find the download without the user typing a path, but must not scan the disk or trigger
  privacy prompts unexpectedly. The in-app "How to install" link pointed at Nordic's product page.
- **Options:** search on request / on dialog open / never (file dialog only); link to our docs / Nordic.
- **Decision:** Only on [Find download] (CLI: when no nRF Util is located), only the top level of the platform
  Downloads folder (`QStandardPaths::DownloadLocation`), names `nrfutil`, `nrfutil.exe` and browser duplicates,
  bounded (5000 entries, 10 candidates, 1–300 MB), newest first; partial downloads reported as such. [Choose file…]
  is always available. Direct download URLs per OS/CPU are a table in `libs/hw/nrfutil` (from Nordic's install
  guide); "How to install ↗" opens our docs topic `InstallNrfUtil`; Nordic's product page is the fallback link.
- **Consequences:** a changed Nordic URL needs a code change (the product page link still works); `DocsAnchors`
  unchanged (existing anchor).

### ADR-0264 — Trust check before running a downloaded nrfutil

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Decision:** rc.3: explicit user action with the full path shown; sanity checks (regular file, not a
  symlink, 1–300 MB, owned by the current user and not group/world-writable on POSIX); after the run, the located
  nrfutil must pass `checkVersions()`. Part B: Windows Authenticode signer and macOS Team ID check before running;
  a mismatch is an error, not a warning. No hash pinning.
- **Consequences:** rc.3 trusts the user's choice like a terminal would; part B adds platform code in
  `libs/hw/nrfutil` (WinVerifyTrust / Security.framework or `codesign` subprocess).

### ADR-0265 — Executable bit and macOS quarantine

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Decision:** after the click the app adds owner-execute to the file (POSIX); it never removes the
  `com.apple.quarantine` attribute. When macOS blocks the program, the error tells the user to allow it in System
  Settings › Privacy & Security. Revisited after the Phase 0 spike.
- **Consequences:** no security marker is removed silently; one extra manual step if Nordic's binary is not
  notarized.

### ADR-0266 — Error `dependency.nrfutil_install_failed`

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Decision:** new `ErrorCode::DependencyNrfUtilInstallFailed`, id `dependency.nrfutil_install_failed`,
  category dependency (exit 4), wizard none; message "Installing nRF Util failed.", cause "There is no internet
  connection, a proxy blocks files.nordicsemi.com, or nrfutil reported an error.", suggestion "Check the internet
  connection and try again. The details show what nrfutil reported." Docs anchor in all five `troubleshooting.md`.
- **Consequences:** error table, `exit_codes`, docs ×5, translations ×4; `DocsAnchors` test covers it.

### ADR-0267 — CLI `hwio nrfutil install-device`

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Decision:** `hwio nrfutil install-device [PATH] [--yes] [--timeout S]` and
  `hwio nrfutil download-url`; running a file found in Downloads needs the `[y/N]` confirmation or `--yes`; `--json`
  as all commands. `hwio doctor` keeps checking only (no `--fix`).
- **Consequences:** CLI docs table, e2e tests with the fake nRF Util.

### ADR-0268 — One "Install nRF Util" dialog

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Decision:** a modal dialog backed by `NrfUtilInstallViewModel` (owned by `FirmwareServices`, one
  install at a time, owned worker thread per run as in ADR-0257), opened from the Flash banner, the LTE wizard banner,
  Settings → nRF Util and later the setup wizard.
- **Consequences:** one QML component and one VM to test; no new wizard id.

### ADR-0269 — rc.3 scope of the nRF Util install assist

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Decision:** rc.3 ships only docs + links (A0); the dialog, backend functions, `NRFUTIL_HOME`, CLI and the
  new error id ship in rc.4 (A1). Signature checks, upgrade button and setup wizard integration later (part B).

### ADR-0270 — Locator honours `NRFUTIL_HOME`

- **Status:** Accepted · **Date:** 2026-11-05 · **Decided by:** Matej Strnad, 2026-11-05
- **Decision:** `defaultLocatorOptions()` searches `$NRFUTIL_HOME/bin` (when set) before
  `~/.nrfutil/bin`; precedence of option / env / setting / PATH unchanged. Amends the locator comment in `nrfutil.h`
  and the docs search order.

## Implementation steps

Part A0 (rc.3)

- [x] A0.1 Docs `install.md` § Nordic nRF Util rewritten in en, cs, de, sk, pl (per-OS steps, direct links, no PATH
  step, Advanced paragraph); anchor kept; `flashing.md` / `troubleshooting.md` checked (no contradiction, unchanged).
- [x] A0.2 `nrfUtilInstallUrl` properties replaced by `nrfUtilDownloadUrl` (`SettingsViewModel::nrfUtilDownloadUrlFor`,
  delegated by `FlashViewModel` and `LteUpdateWizard`); **How to install ↗** → `AppViewModel.docsLink("install-nrf-util")`;
  **Download nRF Util ↗** in `KindCards.qml`, `LteUpdateStep.qml`, `NrfUtilSection.qml`. Error texts unchanged
  (consistent with the docs).
- [x] A0.2b Review fixes (2026-11-05): Windows VC++ Redistributable hint (`VCRUNTIME140.dll`), `winget install --id
  NordicSemiconductor.nrfutil`, macOS `xattr -d com.apple.quarantine` alternative, "too old" commands with the full
  `~/.nrfutil/bin` path; prerequisite banner buttons in `KindCards.qml` / `LteUpdateStep.qml` wrap (Flow).
- [x] A0.3 Tests: `SettingsNrfUtilDownload.UrlPerOsAndArch`, `tst_flash.qml` (buttons + docs link); translations.
- [x] A0.3b clang-tidy-21 on the changed `.cpp` files (zero findings).
- [ ] A0.4 Manual: the links open the right pages on Linux, Windows, macOS (user); verify the Slovak macOS label
  for "Open Anyway" (**Napriek tomu otvoriť**) on a Slovak Mac.

Known in A0 (D14): **Download nRF Util ↗** is shown for every nRF Util problem, also `too_old` and
`device_missing` (the banner has no state for it); accepted, revisited with the A1 dialog.

Part A1 (rc.4)

- [ ] P0 Spike (≤ 30 min per OS, by the user or on the CI-less test machines): fresh user / `NRFUTIL_HOME=$(mktemp -d)`:
  download the launcher from the direct URL with a browser, run `install device --json` from the app's process
  context (a throwaway `QProcess` snippet or Terminal); record: bootstrap location, JSON lines, duration, Gatekeeper
  behaviour (macOS, quarantined), SmartScreen behaviour (Windows, Mark-of-the-Web, started via CreateProcess), offline
  error text; Windows: whether a machine without the Microsoft Visual C++ Redistributable reports `VCRUNTIME140.dll`
  missing or exits silently. Update the plan's facts table; stop and ask if the layout differs from `~/.nrfutil/bin`.
- [ ] A1 Docs en: (`install.md` § nRF Util rewrite: done in A0) "easy way" paragraph, `flashing.md` link text, `troubleshooting.md` § LTE modem firmware
  needs nRF Util + `dependency.nrfutil_*` entries (+ `dependency.nrfutil_install_failed` if D6a), `cli.md` (command
  row, `--nrfutil` default, exit-code table unchanged). Then cs, de, sk, pl; anchors identical.
- [x] A2 Done in A0: `nrfUtilInstallUrl` → docs topic URL (`AppViewModel.docsLink`); `nrfUtilDownloadUrl` property;
  tests updated.
- [ ] A3 `libs/hw/nrfutil`: `downloadUrl`, `homeBinDirs` + `NRFUTIL_HOME` in `defaultLocatorOptions`,
  `findDownloaded`, `prepareExecutable`, `installDeviceCommand`, failure classification; header comment updated;
  fake nRF Util modes (below); unit tests.
- [ ] A3c `error.cpp` suggestions of `dependency.nrfutil_device_missing` / `dependency.nrfutil_too_old` (and the
  doctor text, A5) must work without PATH: full `~/.nrfutil/bin/nrfutil` path or the new CLI command, as the docs
  in A0 (review 2026-11-05; not changed in rc.3).
- [ ] A3b Error code (D6) in `error.h` / `error.cpp` / `exit_codes`, docs anchors ×5.
- [ ] A4 `NrfUtilInstallViewModel` + worker in `FirmwareServices`; `NrfUtilInstallDialog.qml`; buttons in
  `KindCards.qml`, `LteUpdateStep.qml`, `NrfUtilSection.qml`; refresh after success; GMock/fake tests + Qt Quick test.
- [ ] A5 CLI `hwio nrfutil install-device`, `hwio nrfutil download-url`; doctor suggestion text; e2e tests.
- [ ] A6 `update_translations`; translate cs, de, sk, pl (0 unfinished, ADR-0248); `check_translations`.
- [ ] A7 clang-tidy, clang-format, qmllint, ASan/UBSan + TSan runs of the new tests; manual checks (below).
- [ ] A8 Status → Done for part A1; part B stays open in this plan.

Part B (later)

- [ ] B1 Signature verification (D3b). - [ ] B2 Upgrade button for `too_old` (D14b). - [ ] B3 Setup wizard step 4
  uses the dialog (D9a). - [ ] B4 Optional `--detect-proxy` retry offer. - [ ] B5 Refactor the shared process loop of
  `NrfUtilModemUpdater` and `installDeviceCommand`.

## Tests and verification

**Fake nRF Util** (`tests/fake_nrfutil`), new `HWIO_FAKE_NRFUTIL_MODE` values for `install device`:
`install-ok` (writes `$NRFUTIL_HOME/bin/nrfutil` + marks the device command installed, prints JSON lines with
progress), `install-offline` (prints the bootstrap error, exit 1), `install-fail`, `install-hang`, `install-flood`;
`nodevice` until installed. All tests set `NRFUTIL_HOME` to a temp dir; nothing touches the network or the real home.

Unit (`libs/hw/nrfutil/tests`):

- `NrfUtilDownload.UrlPerOsAndArch` (all four URLs + unknown arch → nullopt).
- `NrfUtilFind.FindsNewestAndDuplicates`, `.IgnoresOtherNamesAndSymlinks`, `.ReportsPartialDownloads`,
  `.RespectsLimits`, `.UnreadableDirectoryIsEmpty`.
- `NrfUtilPrepare.AddsOwnerExecute`, `.RejectsWorldWritableAndForeignOwner` (POSIX), `.RejectsTooSmall`.
- `NrfUtilInstall.Succeeds` (then located in `$NRFUTIL_HOME/bin`), `.OfflineMapsToInstallFailed`, `.FailureKeepsTail`,
  `.TimesOutAndKills`, `.CancelKills`, `.OutputFloodIsCapped`, `.InstalledButNotFound`, `.Arguments`.
- `NrfUtil.LocatorUsesNrfUtilHome`.
- `DocsAnchors.*` (existing) covers a new error id.

GUI: `NrfUtilInstallViewModelTest` (states, one install at a time, cancel, refresh after success, retranslate);
`tst_settings.qml` / `tst_flash.qml` (buttons visible only in Problem/not-too-old states, dialog flows with the fake);
screenshots at 960 × 600 in German (ADR-0248) for the dialog states.

CLI e2e: `install-device` with PATH, with a Downloads candidate (+ `--yes`, without `--yes` and no TTY → exit 2),
offline → exit 4, cancel → 130, `--json` shape; `download-url`.

Sanitizers/tidy: new tests in the ASan/UBSan and TSan presets (worker thread, stop/join).

**Manual checks (by the user, each on Linux, Windows 11, macOS — fresh account or `NRFUTIL_HOME` pointed at an empty
directory, no nrfutil on PATH):**

1. Flash page → LTE firmware → banner → **Install nRF Util…** → Download → browser saves to Downloads → Find download
   → Install device command → success text → banner gone, Flash enabled, no restart. Then an actual LTE flash with a
   CHESTER (HIL, may be after the release).
2. Same from the LTE update wizard and from Settings → nRF Util.
3. Offline (Wi-Fi off) → install → `Installing nRF Util failed` with Learn more → docs entry in the UI language.
4. Cancel during the install → Cancelled; Try again works (`--force` not needed).
5. nrfutil present without device (`nrfutil uninstall device`) → dialog state 2 → one click → ready.
6. macOS: quarantined browser download → behaviour matches D4 text; Windows: no SmartScreen dialog, or the documented
   one.
7. CLI: `hwio nrfutil install-device` in a terminal (confirmation), `hwio doctor` shows ✓ afterwards.
8. Docs pages in 5 languages render on GitHub; links from the app land on the section in the UI language.

## Risks and open points

- **rc.3 today:** A3–A6 touch backend, GUI, CLI, docs ×5 and translations ×4 and need three-OS manual checks. Realistic
  only with focused work; hence the A1+A2 fallback (D11). The modem update path itself is not modified.
- **Nordic changes** (bootstrap layout, URLs, JSON) would break the assist but not the modem update; the error says
  "installed, but not found" and the product-page link stays.
- **macOS Gatekeeper / Windows SmartScreen** unknown until P0; may add one manual step (D4).
- **Proxy / corporate networks:** we only pass the environment through; `--detect-proxy` not offered in part A.
- **Running a user-chosen file** is code execution on the user's behalf; mitigated by explicit action, path display,
  sanity checks (D3), no MCP access (D10). Signature check in part B.
- **Version probe on a never-run launcher** (existing Custom path flow) may time out at 10 s because it bootstraps;
  the dialog avoids it by running `install device` first. The Custom path flow is unchanged in part A (documented).
- **macOS privacy prompt** for the Downloads folder on [Find download] (D13); denial → empty result + [Choose file…].
- **Concurrency:** the install is offered only while nRF Util is not usable, so no modem update can run at the same
  time.

## Time estimate (implementation agent)

| Part | Work | Estimate |
|---|---|---|
| P0 | spike on 3 OSes (user) | 0.5–1.5 h |
| A1 | docs en + 4 translations | 1.5 h |
| A2 | link changes + tests | 0.5 h |
| A3/A3b | backend + fake + unit tests + error id | 3 h |
| A4 | VM, worker, dialog, buttons, GUI tests, screenshots | 3–4 h |
| A5 | CLI + e2e | 1.5 h |
| A6/A7 | translations, tidy/format/sanitizers, fixes | 1.5 h |
| — | manual checks on 3 OSes (user) | 1–2 h |
| **Total part A** | | **≈ 12–15 h agent work + 2–3 h user checks**; A1+A2 alone ≈ 2 h |
| Part B | signatures, upgrade button, setup wizard step, refactor | ≈ 1–1.5 days |

## Sources

- Nordic: "Installing nRF Util from the web (default)", "Installation locations", "Environment variables affecting all
  commands", "Installing a specific version of nRF Util" (docs.nordicsemi.com, nrfutil bundle), read 2026-11-05.
- nRF DevZone thread 121123 "nrfUtil Offline Install Failing" (bootstrap error text, `NRFUTIL_BOOTSTRAP_TARBALL_PATH`).
- Local: `nrfutil --help`, `nrfutil install --help`, `~/.nrfutil/logs/nrfutil.log` (nrfutil 8.2.1, device 2.23.0).
