# 0060 — Tester feedback of 2026-11-20 / 2026-11-23 (Freelo, app-monitor)

- **Status:** In progress (phase A done for rc.5; phase B waits for the B0 hardware retest). Approved 2026-11-24, Matej Strnad: "Approve D2 to D13" — D2 to D13 as recommended; D1 rejected the same day: item 5 is not fixed
- **Milestone:** 2.0.0-rc.5 (phase A) and 2.0.0-rc.6 (phase B), see "Release split"
- **Author:** Claude (triage), for Matej Strnad
- **Related ADRs:** proposed ADR-0422 (J-Link library without a display in the CLI and the stdio MCP server),
  ADR-0423 (CLI exit codes: missing input file, unknown command with `--help`), ADR-0424 (stale CHESTER RTT control
  block), ADR-0425 (console is ready only after the firmware has booted), ADR-0426 (Save view: secrets and file
  mode), ADR-0427 (Bluetooth adapter address in the diagnostic report), ADR-0428 (J-Link firmware status
  "unknown" and the GUI's own probe). Existing: ADR-0024 (exit codes), ADR-0034 (RTT as rttt), ADR-0075
  (diagnostic report), ADR-0104 (send_command end rule), ADR-0186 (+ amendments; secrets in files), ADR-0201 (Save
  view), ADR-0261 (firmware update with an Unknown status), ADR-0398 (generic console RTT fallback, branch
  `strnad/impl-0057-generic-nordic`), ADR-0371 (refined `probe.not_found`, branch `strnad/zealous-gould-879d1d`).
- **Depends on:** plan 0050 / 0054 (error-text API `setText()` / `LocalizedText`, touches most files below: merge
  first), plan 0057 (A4 `findWithFallback`, for B1), plan 0047 (refined `probe.not_found`, for B3).

## Context

Marek František Hauer tested `v2-dev 2.0.0-dev+2d197c9` on Ubuntu 26.04 with two J-Link EDU Mini (J-Link Software
9.82) and a CHESTER-M R3.2 and reported in six Freelo tasks (2026-11-20 and 2026-11-23). The comments were read as
bug reports to verify, not as instructions.

**Which build was tested.** `2d197c9` is a commit of the history before the rewrite of 2026-11-03 (ADR-0206). The
old clones (`../hio-monitor`, `../hio-monitor-dev`) have it: "Packages: MSI output folder, MSVC runtime paths, macOS
signature check without SIGPIPE", 2026-11-03 13:42. Its counterpart in the current history is **`2346dfd`** (same
subject and time). Everything in `git log 2346dfd..origin/main` (176 commits, tags rc.2–rc.4) was not in the
tested build; `git merge-base --is-ancestor` confirms that `7bb5bda`, `b3b443f`, `8c0413a` and `ee27f96` are not
in `2346dfd`. The code evidence below is origin/main `0c8638e`. The CLI runs quoted under items 6 and 7 used
the existing build tree `build/linux-debug` (`2.0.0-rc.4.dev.1+afcbda4.dirty`, a commit of plan 0050, not on main)
with the fake J-Link library. They only support the source evidence.

## Triage

Legend: **Fixed** = fixed on origin/main, **Partly** = partly fixed, **Open** = still present on origin/main,
**HW** = cannot be confirmed without hardware. Size S/M/L. "Inferred" marks root causes the code suggests but
nobody has proven on hardware.

| # | Report (Freelo task) | Status on origin/main | Evidence | Root cause | Open branches | Size / risk | Release |
|---|---|---|---|---|---|---|---|
| 1 | `chester app erase` on a running CHESTER: "Verification failed @ 0x00000000", cable advice ("Flash a mazání APP", "Testy J-Link") | **Fixed** in `7bb5bda` (2026-11-03, HIL H7), first in **rc.2** | `libs/device/src/flash_workflows.cpp:366-387` (`runErase()` resets and halts before `eraseCode()`, "as runFlash() does"); GTest `Workflow.CodeEraseResetsAndHaltsFirst` (`libs/device/tests/workflow_test.cpp:443`); plan 0003 lines 553-566 (re-verified on hardware: erase on a running board 16 s, `HilFlash.EraseCodeKeepsThePibAndPibDecodes` 3 of 3) | The running firmware leaves the flash so that the J-Link RAMCode cannot erase at 0x0 until a reset. `runFlash()` reset and halted, `runErase()` did not | none | — | done (rc.2) |
| 2 | After flashing Clime v4.0.1 → Serial RS-485 v4.0.1: "RTT buffer Terminal not found" until a power cycle ("Flash a mazání APP", "Testy J-Link") | **Open (HW)**. `b3b443f` (H6, rc.2) re-arms the RTT search, which does not help with this case | `libs/hw/jlink/src/rtt_channel.cpp:222-232` (Terminal by name only; not found → stop, re-search until the deadline, then `target.rtt_buffer_not_found`); `chester.cpp:164-165` (`rttTerminal = "Terminal"`); `error.cpp:407-416` (cause "The firmware is older or different.", suggestion "Use firmware with the CHESTER shell." — no power-cycle hint) | Inferred, as the tester says: the old firmware's control block stays in RAM (retained across reset). Zephyr's STRONG_CHECK init keeps a block whose ID is valid, so the descriptors (name pointers into the old firmware's rodata) survive, and buffer 0's name no longer reads "Terminal". Re-searching finds the same block | `impl-0057` A4 adds `findWithFallback()` (names, then buffer 0/1), only for the `nrf` profile, CHESTER `nullopt` (ADR-0398) | M / medium (RTT attach path; H6 regression risk) | rc.6 |
| 3 | First answer right after a reset is empty with no error: MCP `reset` → `wait_for_connection` (elapsed 0) → `send_command` empty; `chester app command` during a reboot exits 0 with no output; GUI silent `info show`; HIL `InfoShowAndCommandTreeThroughSilentQueries` after a rebooting test ("Testy J-Link") | **Open (HW)** — in-session part (MCP, GUI) open by code reading; the fresh-process CLI part may have changed with `b3b443f` (rc.2): plan 0002 H6 notes record `reset` + `app command "kernel uptime"` in a new process 10 of 10 OK after that fix | `session_worker.cpp:1295` (reset is not a "light" operation) and `:1362` (`restoreAfterOperation()` re-attaches RTT and reports Connected *before* `finishOperation()`), `:980-1049`; `console_tools.cpp:649-651` (`wait_for_connection` finishes at once when Connected); `command_collector.cpp:146-205` (CLI collects for a fixed time; empty output is success); `chester.cpp:182-184` + open question 25 (no CHESTER shell prompt → silence rule, ADR-0104); `tests/integration/hil_test.cpp:197`; plan 0002 H6 notes (bootloader block at `0x20000410` found first after a reset, then the application's at `0x2000CB30`) | Candidates (inferred, not proven): (a) the attach right after a reset succeeds against a control block that is valid in RAM while the application's shell is not up yet (the application's own block from before the reset, or the bootloader's block re-searched away within ~one retry), so Connected is reported during boot and the line written then is lost; (b) the shell re-initialises its RTT buffers at start and drops a line already written. Nothing distinguishes "no answer" from "answer without output" (no prompt known) | none | M / medium (session state machine) | rc.6 |
| 4 | `HWIO_HIL=1 ctest -L hil` runs `HilFlash.EraseCodeKeepsThePibAndPibDecodes`, which leaves the board erased without `HWIO_HIL_APP_HEX` ("Testy J-Link") | **Open** | `tests/integration/hil_flash_test.cpp:140-150` (no skip; flashes back only `if (appImage())`); `tests/integration/CMakeLists.txt:1-11` (documents `HWIO_HIL_DESTRUCTIVE` for erase --all / recover only) | Test gating | none | S / none (test only) | rc.5 |
| 5 | J-Link EDU Mini opens SEGGER's "Terms of use" window at every connect; CLI and MCP wait for a click with no limit; without `DISPLAY`/`WAYLAND_DISPLAY` it connects in 0.3 s ("Detekce J-Linků", "Testy J-Link") | **Open (HW for macOS/Windows)** | `libs/hw/jlink/src/jlink_probe.cpp:34-57` (sent before/after Open: `DisableAutoUpdateFW`, `SuppressControlPanel`, `SetBatchMode = 1`, `HideDeviceSelection = 1`, `DisableInfoWinFlashDL`; the same list was in `2d197c9`); `jlink_library.cpp` resolves no dialog API other than `JLINK_SetHookUnsecureDialog`; the SEGGER headers of the old app (`hio-monitor` `include/jlink/JLinkARMDLL.h`) declare no hook for this window; no `DISPLAY` handling anywhere (`grep`) | The library shows the EDU notice when it finds a display; `SetBatchMode` does not suppress it (tester's observation). `JLINKARM_Open` blocks the session thread until the window is closed | none | S code / **legal decision** | rc.5 if D1 is approved |
| 6 | Missing file for `chester app flash` → exit 2 (`usage.invalid_argument`), plan 0006 says 3 (io); same for `chester lte flash` with a zip ("Výstup --json a návratové kódy") | **Open** — spec conflict | `app/cli/src/flash_commands.cpp:937-951` (`resolveFlashInput`: not a file and not an ID → `usage`) and `:703-707` (`lte flash FILE` → `usage`); plan 0003 line 133 specifies `usage.invalid_argument` "path does not exist"; plan 0006 "Exit codes" row 3 "file not found"; `mcp_command.cpp:125-127` uses `io.file_not_found` for a missing directory; py-hardwario raises `click.BadParameter` (`hardwario/chester/cli/validate.py:22`), a click usage error with exit 2. Run: `hwio chester app flash /neexistuje.hex` → exit 2 | The two plans disagree; the code follows plan 0003 (and py-hardwario) | none | S / low (exit-code contract) | rc.5 |
| 7 | `hwio rtt --help` prints the main help and exits 0 (`hwio rtt` exits 2) ("Výstup --json a návratové kódy") | **Open** | `app/cli/src/cli_app.cpp:102-110` (`CLI::CallForHelp` → `ShowHelp`, extras never checked); `cli_app.cpp:86-87` (`require_subcommand(0, 1)`, `fallthrough(true)`). Run: `hwio rtt --help` → exit 0 | CLI11 throws `CallForHelp` before it rejects the unknown positional | none | S / low | rc.5 |
| 8 | The diagnostic report contains the computer's Bluetooth MAC address ("Testy J-Link") | **Open** | `libs/diag/src/environment.cpp:441-447` (check text "Adapter ready (name, address)"); `libs/diag/include/hwio/diag/report.h:4-7` (redaction covers home, user and host name only) | The adapter address is part of the check result, which goes into `summary.txt` / `summary.json` unredacted | none | S / low | rc.5 |
| 9 | `hwio-monitor --version` prints nothing and opens the whole window ("Testy J-Link") | **Open** | `app/gui/src/main.cpp:114-124` (only `--self-test` is looked at; no `QCommandLineParser`) | Not implemented | none (`wizards-rc5` only adds `StartupCheck` to `main.cpp`) | S / low (Windows console attach) | rc.5 |
| 10 | `-n` with a J-Link that is not connected while another is: "the USB cable is disconnected or the J-Link is not powered"; better list the connected ones ("Detekce J-Linků") | **Open** | `jlink_probe.cpp:83-113` (message "No J-Link probe with serial number %1 found." + default cause/suggestion of `probe.not_found`, `error.cpp:323-330`) | Default cause is about an empty list | `zealous-gould-879d1d` (plan 0047, ADR-0371) refines `probe.not_found` **only when no probe is listed** (plan 0047 line 156); this case is not covered | S / low (new translated texts) | rc.6 (after 0047) |
| 11 | "Save view…" file has the claim token and BLE passkey in clear text and mode 0664; the automatic console file redacts them and is 0600 ("Export RTT logu") | **Open** | `app/gui/src/viewmodels/console_view_model.cpp:730-760` (`QSaveFile`, default mode, `writeRows()` unredacted); automatic file: `console_file_sink.cpp:310`, `:433` (`matchSensitiveOutput`), `owner_only.h` (`restrictToOwner`, 0600; private header of `libs/device`); ADR-0186 amendment U2 ("the console view on screen is unchanged") | Save view was not part of ADR-0186 | `mcp-client-fixes` / `impl-0054` change only error texts in this file | S / low (security) | rc.5 |
| 12 | EDU Mini: firmware status always "unknown" (`GetEmbeddedFWString` -1); `jlink update-firmware` fails with "could not be updated … disconnect and reconnect" (exit 5, "checksum 0", firmware unchanged); `hwio doctor` shows a green tick ("Update FW J-Linku") | **Partly**: (a) the false failure is **fixed** by ADR-0261 / `8c0413a` (plan 0036, **rc.3**); (b) the -1 may be fixed by `ee27f96` (**rc.4**) — **HW**; (c) green "Not known" in doctor is **by design** (review R11) | (a) `jlink_probe.cpp:358-362` (Unknown before: checksum 0 → `Current`, CLI "is up to date", `jlink_commands.cpp:173-186`); `FakeLibrarySession.UnknownFirmwareStatusIsJudgedByTheVendorCall`. (b) `ee27f96`: the ID was passed as a short C string and the library compares 23 bytes with `memcmp`; `firmwareId("J-Link EDU Mini V1 compiled …")` = "J-Link EDU Mini V1" (18 chars) → bytes past the string compared → likely a mismatch, -1 (inferred). (c) `environment.cpp:353-354`, `:375-377` (`okResult` "Not known") | (b) inferred: vendor `memcmp` past our string | none | S / low | (a) done rc.3; (b) retest; (c) rc.6 |
| 13 | GUI diagnostics: "Not checked: the J-Link is in use" while the GUI's own console holds the probe ("Update FW J-Linku") | **Open** | `environment.cpp:334-337` (`ProbeLock::owner()` → `inUse`, not opened), `:370-373`; the session already reads the status at open: `session_worker.cpp:353-374` (`m_firmwareChecked`, `firmwareStatus()`; only Outdated/Newer are reported) | The check cannot open a held probe and does not ask the process's own session | none | M / low | rc.6 |
| — | Probe selection in the GUI with two probes not verified yet ("Detekce J-Linků") | not a defect; manual check pending | `hil_test.cpp` `TwoChestersWithTwoProbesInOneProcess` covers the backend only | — | — | — | manual check before 2.0.0 |

## Goals / Non-goals

Goals: fix every Open item above; ask for a hardware retest of items 2, 3 and 12(b) on a current build; make
the answers to the tester factual (what is fixed in which release).

Non-goals: no change of the flash or erase method (item 1 is done); no new RTT buffer configuration options; no
change of the J-Link firmware-update flow beyond texts (ADR-0261 stays); no GUI redesign of the diagnostics page.

## User experience

- **Item 2.** When the console cannot find "Terminal" after a flash, the error says what to do:
  message "RTT buffer \"Terminal\" not found." (unchanged), cause "The device still has the RTT data of the
  previous firmware in its memory, or the firmware has no CHESTER shell.", suggestion "Switch the device off and
  on (disconnect the battery or the power supply), then connect again." (new texts, five languages). With D4-B the
  console attaches by index in that situation and logs `RTT: buffer 'Terminal' not found, using up/down buffer 0`.
- **Item 3.** After every RTT attach (at open, after Reset / flash in GUI, CLI and MCP, after an automatic
  reconnect) the console shows "Connected" only once the terminal and log have been quiet for 300 ms (at most 3 s
  while the firmware keeps printing; D5). `hwio chester app command` that got no output at all prints
  `Warning: the device sent no output.` on stderr (exit code unchanged, D5).
- **Item 5.** **Rejected by the user (Matej Strnad, 2026-11-24): "do not do this. Ignore this comment. We will not use EDU in production." Item 5 is not fixed; ADR-0422 is Rejected.** (Was: `hwio …` (Linux) no longer opens SEGGER windows. Phase B (optional, D1): a J-Link open that takes
  longer than 5 s prints once on stderr `Waiting for the J-Link library… If a SEGGER window is open (for example
  "Terms of use" of a J-Link EDU), answer it.` (CLI English, ADR-0027).)
- **Item 8.** The diagnostic report shows `Adapter ready (hci0, <adapter address>)`; the screen in the GUI and
  `hwio doctor` keep the address (D7).
- **Item 9.** `hwio-monitor --version` prints `HARDWARIO Monitor 2.0.0-rc.5 (<git hash>)` and exits 0;
  `hwio-monitor --help` prints the options (`--version`, `--help`) and exits 0; no window. Both texts are English
  and not translated (they appear before the language is loaded); this extends ADR-0027's "CLI English" rule to the
  GUI's command-line output, part of D11.
- **Item 10.** `No J-Link with serial number 801040228 is connected.` cause `Connected J-Links: 801053289,
  801040227.` suggestion `Use one of these serial numbers, or connect the J-Link 801040228.`
- **Item 11.** "Save view…" writes `claim token: <redacted>` / `ble passkey: <redacted>` and creates the file owner-only
  (0600 on Unix). The screen and Copy are unchanged (D6).
- **Item 12.** `hwio doctor` / GUI diagnostics: "J-Link firmware: Could not be verified" with the neutral (skipped)
  status instead of a green "Not known". `hwio jlink update-firmware` on an Unknown probe with checksum 0: `The
  J-Link library has no newer firmware for J-Link 801053289 (its version could not be compared).` exit 0.
- **Item 13.** GUI diagnostics with an open console on J-Link X: the firmware row uses the status the console read
  when it opened the probe ("Up to date" / "Outdated" / "Could not be verified").

## CLI

- Exit codes (D2, D3, ADR-0423): `chester app flash ARG` where ARG is neither an existing path nor a 32-digit ID →
  `io.file_not_found` (exit 3), same message; `chester lte flash FILE` with a missing FILE → `io.file_not_found`
  (exit 3). `hwio <unknown> --help` (and `hwio chester <unknown> --help`) → `usage.invalid_argument` (exit 2), the
  CLI11 text "The following argument was not expected: rtt". `--help` on a known command is unchanged (exit 0).
- `jlink update-firmware` human text for Unknown (item 12); `--json` gains `"verified": false` when the status was
  Unknown (new optional field; ADR-0428).
- `chester app command`, `ble command`, `remote join --command`: stderr warning when no output line arrived; `--json`
  gains `"noOutput": true` (new optional field) (D5).
- All CLI commands (Linux): the J-Link library runs without a display (D1); the waiting notice after 5 s is phase B.
- MCP: `send_command` result gains `"no_output": true` when nothing arrived (D5); `wait_for_connection` unchanged
  (it becomes correct once Connected means "booted").

## Design

### A1 — HIL erase test gating (item 4)
`hil_flash_test.cpp` `EraseCodeKeepsThePibAndPibDecodes`: `GTEST_SKIP()` unless `HWIO_HIL_APP_HEX` is set (the test
flashes it back) or `HWIO_HIL_DESTRUCTIVE=1` (D12). Header comments of both files and `CMakeLists.txt` updated;
`CLAUDE.md` command line unchanged.

### A2 — CLI exit codes (items 6, 7; ADR-0423)
- `flash_commands.cpp` `resolveFlashInput()`: the "does not exist" branch builds `makeError(ErrorCode::IoFileNotFound,
  argument)` with the same message; the no-argument "no build output" case stays `usage` (nothing was given). `lte
  flash` `:703-707` the same. The application-copy errors (`--restore`-style paths, `flash_commands.cpp:265-305`,
  "The application copy %1 does not exist.") follow the same rule for a given path that does not exist; "no copy of
  the device" stays `usage`. The message text is not translated (CLI English).
- `cli_app.cpp` `parse()`: on `CLI::CallForHelp` / `CallForAllHelp`, if the parsed (sub)command has unconsumed
  positionals (`remaining()` of the deepest parsed app, or an equivalent CLI11 query to confirm during
  implementation), return `usageError("The following argument was not expected: <first>")` instead of `ShowHelp`.
- Plan 0003 line 133 and plan 0006 get a note pointing to ADR-0423; docs (`docs/*/cli*.md` exit-code tables) checked.

### A3 — GUI `--version` / `--help` (item 9)
`app/gui/src/main.cpp`: before `QGuiApplication` is created (no display needed, no single-instance activation),
scan `argv` for exactly `--version` or `--help`; print English text to stdout (version from `hwio::buildInfo()`),
return 0. Windows (GUI subsystem): `AttachConsole(ATTACH_PARENT_PROCESS)` first, inside `platform_win.cpp`; when
there is no parent console nothing is shown (documented). Other arguments unchanged (Qt's own `-platform` etc.).

### A4 — Save view (item 11; ADR-0426)
- `ConsoleViewModel::saveView()`: after `QSaveFile::open()`, set owner-only permissions before the first write
  (move `restrictToOwner()` from `libs/device/src/owner_only.h` to a public helper, e.g.
  `hwio/core/owner_only.h`, so `app/gui` can use it; `libs/device` keeps using it). Note: `QSaveFile` writes a
  temporary file and renames it; the permission must be set on the temporary file (`setPermissions()` on the open
  `QSaveFile` does that); a test checks the final mode.
- Redaction: `writeRows()` gets the session's `sensitiveOutputKeysOf(profile, target)` and
  `sensitiveCommandPrefixes`, and writes lines through the same matcher the console file uses
  (`matchSensitiveOutput`, the echo redaction of ADR-0186's amendment). The matcher stays in `libs/device`
  (public header), the view model only passes the lines.

### A5 — Bluetooth adapter address in the report (item 8; ADR-0427)
`RedactionContext` gains `bluetoothAddresses` (the local adapters' addresses, from the same `IBleApi::adapter()`
call the check makes; empty when Bluetooth is off). `redact()` replaces each (case-insensitive, `:`-separated and
bare hex form) with `<adapter address>`; summary, logs and settings in the report are covered. On-screen check text
unchanged. Device (CHESTER) addresses are kept: support needs them.

### A6 — J-Link library without a display (item 5; ADR-0422)

> **Rejected by the user (Matej Strnad, 2026-11-24): "do not do this. Ignore this comment. We will not use EDU in production." Item 5 is not fixed; ADR-0422 is Rejected.** This step is not implemented.
- `app/cli/src/main.cpp`, first statement of `main()` (before `QCoreApplication`, before any thread — `unsetenv` is
  not thread-safe), Linux only: `unsetenv("DISPLAY")`, `unsetenv("WAYLAND_DISPLAY")` unless
  `HWIO_JLINK_DIALOGS=1`; one info log line later says so. The CLI uses no QtGui (ADR-0004); the only child
  process is nRF Util (`libs/hw/nrfutil/src/nrfutil.cpp`, `QProcess`), a console tool; no `xdg-open` /
  `QDesktopServices` in `app/cli` or `libs` (grep). The stdio MCP server is `hwio mcp` and is covered.
- GUI: unchanged (the window is visible to the user); GUI-hosted MCP (HTTP) inherits the GUI's behaviour.
- macOS / Windows: not changed (no environment switch exists); to be checked by hand with an EDU probe.
- Waiting notice → phase B (B7; not done either, same rejected decision D1): `jlink info` / `jlink update-firmware` open the probe synchronously on the main
  thread (`jlink_commands.cpp:113`, `:171`), so a Qt timer cannot fire while `JLINKARM_Open` blocks; it needs a
  small watchdog `std::jthread` owned by the command (stop token cancels it, writes one line to stderr through a
  mutex-guarded writer), designed in B7.

### B0 — Hardware retest (items 2, 3, 12b) before B1/B2/B4
On a build with phase A: (1) flash Clime v4.0.1 → Serial RS-485 v4.0.1, open the console, `--log-level debug`,
note the control block address and the buffer names/sizes the library reports; read RAM at that address
(`hwio chester app mem read`) to see whether the old block is at the new firmware's address (STRONG_CHECK theory)
or elsewhere; (2) `hwio chester app reset && hwio chester app command "kernel uptime"` ×10, note empty answers and
the time from reset to the first boot line; (3) `hwio jlink info -n <EDU>` with J-Link 9.82 and 9.84: is the
firmware status still Unknown after `ee27f96`? The results decide D4 and the D5 thresholds.

### B1 — Stale CHESTER RTT control block (item 2; ADR-0424)
Recommended combination (D4): (a) new error texts with the power-cycle hint (above); (b) **late** index fallback
for the CHESTER app target: `RttNames::terminalIndexFallback = 0`, `loggerIndexFallback = 1` from plan 0057, but
`RttChannel` uses the fallback only after the name has been missing for `fallbackAfter` (default 5 s, i.e. after
several re-armed searches, so H6's bootloader block — which the application replaces within about one retry, plan
0002 H6 notes — is still skipped by name); a block whose buffer 0 has size 0 is never taken. Needs plan 0057's A4
merged (same function). Option (c), clearing stale "SEGGER RTT" IDs in RAM after a flash, is evaluated in B0 only.

### B2 — Ready after boot (item 3; ADR-0425)
- Every successful `attachRtt()` of the console target (at open, in `restoreAfterOperation()` after reset Go /
  flash Go / recover / LTE bundle step 2, and in auto-reconnect): the worker reads RTT but reports `Connected`
  only when no line has arrived for `bootQuiet` (300 ms), or after `bootSettleMax` (3 s) while lines keep coming.
  An idle running device is Connected 300 ms after the attach; a booting one once its boot output stops. Lines are
  forwarded as usual meanwhile; `sendLine()` during the settle is queued (existing command queue, its bounds) and
  written on Connected. This covers the in-session reset (MCP, GUI) and the fresh-process cases (CLI command during
  a reboot, the HIL test after a rebooting test). Variant D5-A1 (settle only after Monitor's own reset) avoids the
  300 ms at open but leaves the fresh-process cases to the marker below.
- `CommandCapture` / `ResponseCollector`: a capture that ended by timeout with no line after the echo sets
  `noOutput`; MCP `send_command` adds `no_output: true`; the CLI prints the warning (D5). Exit codes unchanged.
- `DeviceInfoWatcher` benefits without change (it queries on Connected).
- Numbers 300 ms / 3 s are placeholders to confirm in B0.

### B3 — `probe.not_found` with other probes listed (item 10)
`JLinkProbe::open()` `:97-111`: when `serialNumber` is given and `probes` is not empty, set cause and suggestion as
in "User experience" (connected serial numbers, at most 8 listed, network probes excluded). Built on plan 0047's
error-refinement helper once merged, so both cases live in one place.

### B4 — Firmware status Unknown (item 12c; ADR-0428)
`environment.cpp`: `unknown > 0` → `skippedResult(check, "Could not be verified")` (neutral) instead of
`okResult("Not known")`; JSON `status: "skipped"`. CLI `jlink update-firmware` text and `verified` field (above).
GUI firmware dialog: the Unknown confirmation text of ADR-0261 stays.

### B5 — GUI diagnostics use the GUI's own session (item 13; ADR-0428)
`DeviceSession` keeps the firmware status it read at open (today only Outdated/Newer are emitted): add
`ProbeFirmwareNotice` for every status, stored in `m_probeFirmware`. `diag::CheckOptions` gains an optional
`std::function<std::optional<FirmwareStatus>(std::uint32_t)> ownSessionFirmware` (GUI passes a lookup over its
`SessionRegistry`; CLI passes nothing). In the probe loop, a probe held by a lock whose owner PID is this process
uses the lookup instead of counting as `inUse`.

## Decisions requiring user confirmation

| # | Question | Options | Recommendation | Decision (by user) | ADR |
|---|---|---|---|---|---|
| D1 | J-Link EDU "Terms of use" window (item 5) | A: CLI and `hwio mcp` run the library without a display on Linux (unset `DISPLAY`/`WAYLAND_DISPLAY` at start; `HWIO_JLINK_DIALOGS=1` keeps them) in rc.5, + a "waiting for the J-Link library" notice after 5 s on all OSes in rc.6 · B: only the notice (rc.6) · C: document only | **A** — the window cannot be answered in scripts and AI sessions; SEGGER's library itself proceeds without a display. But it hides a vendor licence notice in the CLI: you decide whether that is acceptable for HARDWARIO | **rejected** (2026-11-24): not done, EDU is not used in production | ADR-0422 |
| D2 | Missing input file for `app flash` / `lte flash` / application copy (item 6) | A: `io.file_not_found`, exit 3 (follow plan 0006) · B: keep `usage` exit 2 (plan 0003, and py-hardwario's `click.BadParameter` also exits 2) and correct plan 0006 | **A** — plan 0006's table is the documented public contract and says "file not found" = 3; py-hardwario compatibility is about command and option names (principle 1), its exit codes are not reproduced elsewhere either | **as recommended** (2026-11-24) | ADR-0423 |
| D3 | Unknown command with `--help` (item 7) | A: exit 2 with "The following argument was not expected" · B: keep exit 0 | **A** — scripts must not see a non-existent command as existing | **as recommended** (2026-11-24) | ADR-0423 |
| D4 | Stale RTT block after flashing another firmware (item 2) | A: hint only (power-cycle text) · B: hint + **late** fallback to buffer 0/1 for CHESTER app after 5 s without "Terminal" · C: immediate fallback as in 0057 · D: clear old RTT IDs in RAM after our flash | **B** (after B0 confirms the cause); C risks attaching to the bootloader block again (undoing H6); D only covers flashes done by Monitor and writes target RAM — evaluate in B0. Note: rttt 1.9.2 / py-hardwario are name-only too, so B is a deliberate deviation from ADR-0034 (the old Qt app used fixed 0/1) | **as recommended** (2026-11-24) | ADR-0424 |
| D5 | Empty first answer after a reset (item 3) | A: every RTT attach reports Connected once RTT has been quiet for 300 ms (max 3 s) + "no output" marker (CLI warning, MCP `no_output`), exit codes unchanged · A1: the same settle only after Monitor's own reset / flash (no 300 ms at open; fresh-process cases only get the marker) · B: A, and `chester app command` with no output exits 9 (timeout) · C: marker only | **A** — covers MCP, GUI, the CLI during a reboot and the HIL test; 300 ms per attach is the price. A non-zero exit would break scripts whose commands legitimately print nothing (no prompt known, open question 25) | **as recommended** (2026-11-24) | ADR-0425 |
| D6 | "Save view…" (item 11) | A: redact the secrets as in console files + owner-only file · B: owner-only only (save what is seen) · C: redact only | **A** — Save view is what users attach to support requests (ADR-0075); the value stays visible on screen and in Copy | **as recommended** (2026-11-24) | ADR-0426 |
| D7 | Bluetooth adapter address (item 8) | A: redacted in the report, kept on screen and in `hwio doctor` · B: never shown · C: keep | **A** — the report leaves the computer, the screen does not | **as recommended** (2026-11-24) | ADR-0427 |
| D8 | J-Link firmware "unknown" (item 12c) | A: neutral "Could not be verified" (skipped) in doctor / GUI; CLI update text says not compared; `verified:false` · B: warning · C: keep green "Not known" | **A** — not a problem, but not a pass either; a warning would alarm every EDU user | **as recommended** (2026-11-24) | ADR-0428 |
| D9 | GUI diagnostics with its own console open (item 13) | A: use the status the GUI's session read at open · B: keep "Not checked" | **A** | **as recommended** (2026-11-24) | ADR-0428 |
| D10 | `-n` with a J-Link that is not connected (item 10) | A: list the connected serial numbers (cause/suggestion), together with plan 0047 · B: keep | **A** | **as recommended** (2026-11-24) | — (minor) |
| D11 | GUI `--version` (item 9) | A: `--version` and `--help` print English text and exit before the window; Windows attaches to the parent console · B: `--version` only | **A** (the English-only output is an exception to "every user-visible string translatable", like the CLI) | **as recommended** (2026-11-24) | — (minor) |
| D12 | HIL erase-code test gating (item 4) | A: run when `HWIO_HIL_APP_HEX` is set (it restores the board) or `HWIO_HIL_DESTRUCTIVE=1` · B: only `HWIO_HIL_DESTRUCTIVE=1` (tester) | **A** — the test is safe with a HEX to restore; without one it is destructive | **as recommended** (2026-11-24) | — (minor) |
| D13 | Release split | phase A in rc.5, phase B in rc.6 (below) · everything in rc.6 | **split** | **as recommended** (2026-11-24) | — |

## Release split

Filter for rc.5 (close to release): no new translatable strings, no change in `session_worker` / RTT attach, provable
by unit or fake-J-Link tests. Phase A passes it (CLI texts are English only, ADR-0027; "<adapter address>" is a
redaction token, not a UI string).

- **rc.5 (phase A):** items 4, 5 (if D1 = A or B), 6, 7, 8, 9, 11. Base: main after plans 0050 / 0054 are merged
  (they rewrite error construction in `flash_commands.cpp`, `cli_app.cpp`, `console_view_model.cpp`); otherwise
  rebase A2/A4 on them.
- **rc.6 (phase B):** items 2, 3, 10, 12c, 13, plus the B0 retest. B1 after plan 0057 (A4), B3 after plan 0047.
- **Already done:** item 1 (rc.2), item 12a (rc.3), possibly item 12b (rc.4, retest).

## Implementation steps

Phase A (rc.5)
- [x] A1 HIL gating of `EraseCodeKeepsThePibAndPibDecodes` (D12); comments in `hil_flash_test.cpp`, `CMakeLists.txt`.
- [x] A2 `io.file_not_found` for missing flash inputs; unknown command with `--help` → exit 2 (D2, D3). Tests:
      CLI e2e (`app/cli/tests/e2e_test.cpp`, parsing in `cli_app_test.cpp`) `FlashMissingFileExits3`,
      `LteFlashMissingFileExits3`, `UnknownCommandWithHelpExits2`, `KnownCommandHelpExits0`,
      `NestedUnknownCommandWithHelpExits2`; `--json` envelope code `io.file_not_found`.
- [x] A3 GUI `--version` / `--help` (D11). Test: CMake/ctest test running `hwio-monitor --version` with
      `QT_QPA_PLATFORM` unset and no display, expecting exit 0 and the version line; Windows CI smoke.
- [x] A4 Save view owner-only + redaction (D6). Tests: GUI view-model test (existing console view-model test file)
      `SaveViewIsOwnerOnly` (Unix mode 0600), `SaveViewRedactsInfoShowSecrets`, `SaveViewRedactsSensitiveCommandEcho`;
      `owner_only` helper move keeps `ConsoleFileSink` tests green.
- [x] A5 Adapter address redaction (D7). Tests: `Redact.BluetoothAdapterAddress` (both forms), report test that
      `summary.txt` has `<adapter address>` with the fake BLE adapter.
- [ ] ~~A6~~ (not done: D1 rejected 2026-11-24) CLI without display (D1). Tests: fake J-Link library records `getenv("DISPLAY")` at `JLINKARM_Open` into
      its scenario log (test-only); CLI e2e `JLinkOpenRunsWithoutDisplay`, `JLinkDialogsEnvKeepsDisplay`.
- [x] A7 Docs (five languages where user docs change: CLI exit codes, Save view, `--version`), release notes rc.5,
      plan 0003 / 0006 notes, `CLAUDE.md` HIL line if needed.

Phase B (rc.6)
- [ ] B0 Hardware retest (items 2, 3, 12b) and record results here; confirm D4 / D5 numbers.
- [ ] B1 RTT late fallback + power-cycle texts (D4). Tests: `Rtt.ChesterFallsBackToBuffer0AfterTheNameIsMissing`,
      `Rtt.BootloaderBlockIsNotTakenBeforeFallbackAfter` (block without Terminal replaced by a named one within
      the window), `Rtt.FallbackSkipsZeroSizedBuffer0`; fake J-Link scenario "stale block with garbage names" in
      a CLI e2e console test.
- [ ] B2 Ready after boot + no-output signal (D5). Tests (fake J-Link scenario with boot output after reset):
      `SessionWorker.ResetReportsConnectedAfterBootQuiet`, `…AfterBootSettleMax`, `…LineSentDuringSettleIsWrittenAfter`;
      `…AttachAtOpenWaitsForQuiet`, `…IdleDeviceConnectedAfterBootQuiet`; MCP `send_command` `no_output`; CLI
      `CommandWithoutOutputWarns`; HIL `Hil.CommandRightAfterResetAnswers` (one in-process `DeviceSession`: reset
      + `kernel uptime` ×10) and `Hil.CommandInNewProcessDuringRebootAnswers` (`kernel reboot cold`, then a new
      session at once).
- [ ] B3 `probe.not_found` with connected probes listed (D10). Tests: `Probe.NotFoundListsConnectedSerialNumbers`,
      network probes excluded, empty list unchanged (0047 path).
- [ ] B4 Unknown firmware status presentation (D8). Tests: diag check `ProbeFirmwareUnknownIsSkipped`; CLI e2e
      `UpdateFirmwareUnknownChecksum0` text and `verified:false`.
- [ ] B5 GUI diagnostics via own session (D9). Tests: diag check with an `ownSessionFirmware` lookup → status
      reported, not "in use"; GUI test with a fake-J-Link session open.
- [ ] ~~B7~~ (not done: D1 rejected 2026-11-24, "We will not use EDU in production") Waiting notice for a blocking J-Link open (D1, all OSes): watchdog thread design; fake scenario with a
      delayed Open (6 s) → notice on stderr once, for a session command and for `jlink info`.
- [ ] B6 Translations (cs, de, pl, sk; `check_translations` 0 unfinished), docs, release notes rc.6.

## Tests and verification

- Every step: `ctest --preset linux-debug`, `linux-asan`, `linux-tsan` (B2 changes worker timing), `linux-tidy`
  zero findings, `clang-format-21`, `all_qmllint` (A4 if QML changes), `check_translations` (B), CI on macOS and
  Windows (A3).
- Manual on hardware (tester or HIL rig): items 2, 3, 5 (EDU Mini on Linux; macOS and Windows: note whether the
  window appears and whether the notice helps), 12b, 13, two-probe selection in the GUI.
- Builds follow the global rule: `nice -n 19 ionice -c 3`, one preset at a time, at most 5 jobs, at most 3 streams.

## Reply drafts to the tester (Czech; not posted — for the user to send)

**Update FW J-Linku (33130844)**
> Díky. Chybové hlášení po `hwio jlink update-firmware` je opravené od 2.0.0-rc.3: když verzi firmwaru nejde
> porovnat a knihovna nic nezapíše (checksum 0), příkaz skončí úspěšně. Stav „neznámý“ u EDU Mini mohla
> způsobovat chyba v předávání ID firmwaru knihovně, opravená v rc.4. Prosím zkus `hwio jlink info -n 801053289`
> na rc.4 nebo novějším a napiš, jestli je stav pořád „neznámý“. Plánujeme (rc.6): v `hwio doctor` a v diagnostice
> GUI bude místo zelené fajfky neutrální „Nelze ověřit“ a hláška update-firmware řekne, že verzi nešlo porovnat.
> Diagnostika v GUI vezme stav firmwaru ze spojení, které GUI samo drží, takže nebude psát „J-Link se používá“.

**Výstup --json a návratové kódy (33130885)**
> Obojí platí i na aktuálním kódu (ověřeno bez hardwaru). Navrhujeme: chybějící soubor u `chester app flash` a `chester lte flash`
> vrátí `io.file_not_found` (exit 3) a neznámý příkaz s `--help` skončí chybou s exit 2. Cíl je 2.0.0-rc.5, pokud
> to schválíme.

**Export RTT logu a device info (33130851)**
> Díky. Kód to potvrzuje: „Uložit výpis…“ zatím ukládá claim token a passkey čitelně a s právy podle umask. Navrhujeme pro
> rc.5: soubor bude čitelný jen pro vlastníka (0600) a claim token i passkey v něm budou `<redacted>` jako
> v automatickém logu. Na obrazovce a při kopírování zůstanou vidět.

**Flash a mazání APP (33130846)**
> Mazání aplikace na běžícím CHESTERu je opravené od 2.0.0-rc.2: erase před mazáním sám resetuje a zastaví CPU
> (ověřeno na hardwaru). „RTT buffer Terminal not found“ po přehrání jiného firmwaru jsme bez hardwaru
> nezopakovali. Podle kódu to na aktuální verzi pořád hrozí: oprava z rc.2, která hledá RTT blok znovu, tenhle
> případ nejspíš neřeší. Navrhujeme pro rc.6 (čeká na schválení): hláška poradí vypnout a zapnout zařízení
> a konzole po několika sekundách bez bufferu „Terminal“ vezme buffer 0/1. Fallback nebude okamžitý, aby se
> konzole nepřipojila k RTT bloku bootloaderu. Před tím bychom rádi postup zopakovali s `--log-level debug`.
> Pokud máš podrobný postup, pošli ho prosím.

**Detekce jednoho i více J-Linků (33130843)**
> Okno SEGGER „Terms of use“ u EDU Mini: měnit to nebudeme (rozhodnutí 2026-11-24), J-Link EDU se v produkci
> nepoužívá. Hlášku pro J-Link, který není připojený, navrhujeme změnit v rc.6: vypíše sériová
> čísla připojených J-Linků. Výběr ze dvou sond v GUI je pořád
> potřeba ověřit ručně.

**Testy J-Link (33130842)**
> Díky za souhrn. Testovaný build 2d197c9 je starší než rc.2 (ze 3. 11. před přepsáním historie).
> - Erase na běžícím CHESTERu: opraveno v rc.2.
> - RTT „Terminal not found“ po jiném firmwaru: bez hardwaru nezopakováno, podle kódu nejspíš platí; návrh opravy
>   pro rc.6 (viz úkol Flash a mazání).
> - Prázdná první odpověď po resetu: bez hardwaru nezopakováno. Podle kódu se konzole po resetu hlásí jako
>   připojená dřív, než firmware naběhne. Navrhujeme pro rc.6: „připojeno“ až když RTT na chvíli utichne (boot
>   doběhl) a prázdná odpověď bude označená (varování v CLI, `no_output` v MCP). Část s CLI v novém procesu
>   mohla změnit už oprava z rc.2, prosím zkus to na novější verzi.
> - HIL test mazání: v rc.5 poběží jen s `HWIO_HIL_APP_HEX` (vrátí aplikaci) nebo s `HWIO_HIL_DESTRUCTIVE=1`.
> - Exit 2 u chybějícího souboru, `hwio rtt --help` → 0, MAC adresa Bluetooth v diag reportu a
>   `hwio-monitor --version`: platí i na aktuálním kódu, opravy navrhujeme pro rc.5 (MAC adresa bude v reportu
>   skrytá). Okno „Terms of use“ u J-Link EDU měnit nebudeme (EDU se v produkci nepoužívá).
> Podrobný postup k RTT a k resetu by se hodil.

## Risks and open points

- Items 2, 3, 5 and 12b were not reproduced (no hardware in this triage).
- Item 2's and item 3's causes are inferred from the code and the tester's description; B0 must confirm them before
  B1/B2. If the stale block is at another address than the new firmware's, D4-B would attach to a dead block: B1's
  test against a fake "old block below the new one" scenario and B0's RAM read cover that.
- D1 hides a SEGGER licence notice in the CLI; legal acceptability is the user's call. `unsetenv` must run before any
  thread (QtCore may start threads later); a test asserts it is the first thing in `main()` (review).
- B2 changes when sessions report Connected; GUI tests and MCP tests that wait for Connected after a reset may need
  longer timeouts; TSan run required.
- Plans 0050 / 0054 / 0057 / 0047 touch the same files (error texts, `flash_commands.cpp`, `rtt_channel.cpp`,
  `jlink_probe.cpp`, `environment.cpp`); merge order above. Note for the user: ADR numbers 0371 / 0372 are used by
  both `strnad/impl-0049-regs-halt` and `strnad/zealous-gould-879d1d`.
- Not mapped to code: the tester's offer to send "details and procedure" (to request for items 2 and 3); the
  two-probe GUI selection is a pending manual check, not a defect.
