# ADR-0440 — Device information file: content, format, name and secrets

- **Status:** Accepted
- **Date:** 2026-11-25
- **Decided by:** Matej Strnad (2026-11-25, "Approve as recommended", plan 0063 D1–D7)

## Context

Users want a record of a CHESTER (what `info show`, `config show` print) in a file named by the device's serial
number, without typing the commands and renaming a saved console output by hand (plan 0063).

## Options considered

See the decision table of plan 0063 (D1–D7): the command list (fixed in the profile / firmware-dependent state
commands / user-defined), the format (text / JSON / both), secrets (hidden by default with an opt-in / always
written / always hidden), the file name (serial and time / serial only / with the device name), the destination
(save dialog / fixed folder), the transports (J-Link and Bluetooth / J-Link only) and the CLI command name.

## Decision

- The commands are a list in the device profile (`infoFileCommands`); CHESTER: `info show`, `config show`. They
  run as silent shell queries.
- Plain UTF-8 text: a header (device name, serial number, firmware, date, Monitor version), then per command a
  line `### <command>` and its output as printed; "(no answer)" for a command without an answer.
- Secrets are hidden with the console file's rules (`device::ConsoleRedaction`) unless the user asks for them
  ("Include secrets" in the GUI, `--with-secrets` in the CLI). The file is owner-only in both cases (ADR-0426).
- Name `<serial>_<YYYY-MM-DD_HHMMSS>.txt`; without a serial number `unknown-<probe serial or Bluetooth address>_…`.
- GUI: the system save dialog with the name prefilled and the last folder remembered. CLI:
  `hwio chester app info save [PATH]` and `hwio ble info save [PATH]`.
- J-Link and Bluetooth tabs. Never in a remote support tab (plan 0044: no silent query to a customer's device).
  No MCP tool.

## Consequences

- A new device gets the feature by listing its commands in its profile.
- A file written with secrets is as sensitive as the device's keys; the docs say so.
- The record is not a backup format: there is no restore from it.
