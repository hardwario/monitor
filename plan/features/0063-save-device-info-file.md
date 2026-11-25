# 0063 — Save a device's information to a file named by its serial number

- **Status:** Approved (2026-11-25, Matej Strnad: "Approve as recommended" — D1 to D8)
- **Milestone:** 2.0.0-rc.5 (decision D8)
- **Author:** Claude, for Matej Strnad
- **Related ADRs:** ADR-0440 (device information file: content, format, name, secrets). Existing: ADR-0166
  (`info show`), ADR-0175 (silent shell queries), ADR-0186 and ADR-0426 (secrets in files, owner-only files),
  plan 0028 D7 (console file named from `info show`), ADR-0332 / plan 0044 (never a silent query to a remote host's
  device).
- **Depends on:** `device::ConsoleRedaction` (plan 0060, PR #21) for D3.

## Context

Request of 2026-11-25 (Matej Strnad): "save info about CHESTER into the file named by serial number. There should
be info show, config show etc".

Today the user gets this only by typing the commands in the console and using "Save output…", then renaming the
file by hand. The pieces exist: silent shell queries on J-Link and Bluetooth (`ShellQueryQueue`, `runAndWait`),
the parsed `info show` with the serial number (`DeviceInfo`), and the secret-hiding rules of the console file.

## Goals / Non-goals

Goals:

- One action (GUI and CLI) that asks the connected CHESTER a fixed list of read-only commands and writes their
  answers into one file whose name contains the device's serial number.
- The same result from the GUI and the CLI (one backend function).

Non-goals:

- No restore from the file (it is a record, not a backup format), no comparison of two files.
- No new MCP tool (an AI client can already send the commands and read the answers).
- Not for a remote support tab: Monitor never sends silent queries to a customer's device (plan 0044).

## User experience

- **GUI:** Console page, "More" menu next to "Save output…": **Save device information…**. Enabled when the tab is
  connected (J-Link or Bluetooth) and idle. A progress line "Reading device information… (2 of 4)" with Cancel;
  then the system save dialog with the file name prefilled (D4) in the last used folder (first time:
  Documents/HARDWARIO Monitor). After saving: an inline notice "Device information saved to <path>" with **Show in
  folder**.
- The commands run as silent queries: they do not appear in the console, as `info show` on connect does not.
- **Errors:** the device does not answer a command → the file is still written, that section says "(no answer)",
  and the notice says which commands gave no answer. `info show` without a serial number → the name falls back to
  the J-Link serial or Bluetooth address with the prefix `unknown-` and the notice says so. Disconnect in the
  middle → nothing is written, error "The device disconnected while its information was read."
- New strings translated to cs, de, pl, sk.

## CLI

`hwio chester app info save [PATH]` (and `hwio ble info save [PATH]`):

- `PATH` omitted: the file is written to the current directory with the generated name; `PATH` a directory: into
  it; `PATH` a file name: exactly that.
- Human output: the path written and the commands without an answer. `--json`: `{"path": …, "serial": …,
  "commands": [{"command": …, "answered": true, "lines": N}, …]}`.
- `--with-secrets` as decided in D3. Exit codes: 0; 3 when the file cannot be written; the existing connect and
  timeout codes otherwise.
- py-hardwario has no such command; the name follows the existing `hwio chester app info`.

## Design

- **`libs/device`:** `struct DeviceInfoFileOptions { bool withSecrets; }`, `class DeviceInfoCollector` — runs the
  profile's command list through a `ShellQueryQueue` one by one (the session's worker thread rules unchanged,
  results by queued signal), bounded: at most 16 commands, 2000 lines and 256 KiB per command (longer answers are
  cut with a marker line), 10 s per command. Result: `DeviceInfoDocument { serial, deviceName, firmware, time,
  sections[] }`. `[[nodiscard]] std::string formatDeviceInfo(const DeviceInfoDocument&)` and
  `[[nodiscard]] std::string deviceInfoFileName(const DeviceInfoDocument&)` are pure and unit-tested.
- **Profile:** new field `infoFileCommands` in the device profile (CHESTER: D1). New device support = its own
  list, no code change.
- **Secrets:** each answer line goes through `ConsoleRedaction` unless `withSecrets` (D3).
- **Writing:** `QSaveFile`, owner-only (`restrictToOwner`, ADR-0426), UTF-8, LF.
- **GUI:** `ConsoleViewModel::saveDeviceInfo()` + progress properties; no logic in QML. **CLI:** thin command on
  the same collector.
- **Errors:** `io.write_failed` (existing), `target.not_connected` (existing), new `device.info_incomplete` only as
  a notice text, not an error code.

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D1 | Which commands ("info show, config show etc")? | **(a) `info show`, `config show` and nothing else for now; the list lives in the CHESTER profile and is easy to extend (recommended)**; (b) also state commands that depend on the firmware variant (for example `lte state`, `lrw state`): commands a firmware does not have give "(no answer)" sections; (c) the user's own extra commands from Settings | (a), as recommended (2026-11-25) | ADR-0440 |
| D2 | File format | **(a) Plain text: a header (device name, serial number, firmware, date, Monitor version), then per command a line `### <command>` and its output as printed (recommended: readable, and `config show` output stays copy-and-paste commands)**; (b) JSON; (c) both files | (a), as recommended (2026-11-25) | ADR-0440 |
| D3 | Secrets (`config show` prints LoRaWAN keys, `info show` the claim token and Bluetooth passkey) | **(a) Hidden as in console files by default; GUI checkbox "Include secrets" in the save step and CLI `--with-secrets` write them; the file is owner-only either way (recommended)**; (b) always written in full (a complete record, but every such file is a secret); (c) always hidden | (a), as recommended (2026-11-25) | ADR-0440 |
| D4 | File name | **(a) `<serial>_<YYYY-MM-DD_HHMMSS>.txt`, for example `2159018267_2026-11-25_141502.txt`: files of one device sort by time and nothing is overwritten (recommended)**; (b) `<serial>.txt`, overwritten each time (asks before overwriting in the GUI); (c) `<device name>_<serial>_<date>.txt` | (a), as recommended (2026-11-25) | ADR-0440 |
| D5 | Where the file goes in the GUI | **(a) The system save dialog, name prefilled, last folder remembered (recommended)**; (b) always a fixed folder (Documents/HARDWARIO Monitor/devices) without a dialog, set in Settings | (a), as recommended (2026-11-25) | ADR-0440 |
| D6 | Transports | **(a) J-Link and Bluetooth tabs; not remote tabs (recommended)**; (b) J-Link only | (a), as recommended (2026-11-25) | ADR-0440 |
| D7 | CLI command name | **(a) `hwio chester app info save [PATH]` and `hwio ble info save [PATH]` (recommended)**; (b) `hwio chester app snapshot` | (a), as recommended (2026-11-25) | ADR-0440 |
| D8 | Release | **(a) rc.5, as a pull request on top of PR #21 (needs its secret-hiding class) (recommended)**; (b) rc.6 | (a), as recommended (2026-11-25) | — |

## Implementation steps

- [ ] 1 Profile field `infoFileCommands`; `DeviceInfoCollector`, `formatDeviceInfo`, `deviceInfoFileName` with unit
      tests (scripted shell: answers, no answer, cut answer, disconnect, secrets hidden / included, no serial).
- [ ] 2 CLI commands with end-to-end tests over the fake J-Link and the fake BLE scenario (human, `--json`, PATH
      forms, exit codes).
- [ ] 3 GUI: view-model action, progress, save dialog, notice; Qt Quick tests; disabled in a remote tab.
- [ ] 4 Translations (cs, de, pl, sk), docs `console.md` and `cli.md`, release notes.

## Tests and verification

Unit, CLI end-to-end and Qt Quick tests as listed; `linux-debug`, `linux-asan`, `linux-tsan`, `linux-tidy` (zero
findings), `all_qmllint`, `check_translations` (0 unfinished), clang-format, `tools/check-includes.sh`. Manual:
a real CHESTER over J-Link and over Bluetooth on Linux, Windows and macOS; the file opens in a text editor; the
file mode on Linux and macOS.

## Risks and open points

1. `config show` of a large application can be long; the bounds above cut it with a visible marker.
2. A firmware whose shell is busy (logging storm) may time out; the file then says "(no answer)".
3. With D3 (a) and "Include secrets" the file is as sensitive as the device's keys; the docs say so.
