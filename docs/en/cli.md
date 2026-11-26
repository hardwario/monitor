# Command line: `hwio`

[Documentation](index.md) › Command line

`hwio` is the command-line tool of HARDWARIO Monitor. It does what the desktop app does with a J-Link, without a
window, and more: the Bluetooth LE console, the MCP server for AI assistants, low-level target commands, and output
for scripts (`--json`). Its commands follow py-hardwario's `hardwario` command, so most scripts keep working with the
command name changed ([Migrating from py-hardwario](cli-migration.md)).

`hwio` and the app share the settings, the command history, the console files and the J-Link library. The output
is always in English.

Where `hwio` is after installation: [Installing](install.md) (Windows: on the `PATH`; .deb: `/usr/bin/hwio`;
AppImage: `hwio-monitor-….AppImage hwio`; macOS: inside the app bundle, see **Settings → About → Command line**).

## Everyday use

```sh
hwio doctor                                  # check the installation
hwio jlink list                              # the connected J-Link probes
hwio chester app console                     # the console of the application MCU
hwio chester app command "info show"         # one shell command, print the answer
hwio chester app info                        # firmware name and version, serial number, ...
hwio chester app info save                   # info show + config show into <serial>_<date>_<time>.txt
hwio chester app flash firmware.hex          # flash the application firmware
hwio chester lte flash                       # flash the HARDWARIO LTE firmware
hwio chester lrw flash                       # flash lora-modem to the LoRaWAN module (guided)
hwio ble scan                                # CHESTER devices nearby
hwio ble console -a F1:22:33:44:55:66        # the console over Bluetooth
hwio --json chester app pib read | jq .data  # for scripts
```

`--help` after any command lists its options.

## Global options

These work with every command, before or after it.

| Option | Meaning |
|---|---|
| `-h`, `--help` | Show the help and exit |
| `--version` | Show the version and exit (`--version --json` also gives the Qt version, compiler and package type) |
| `--json` | Machine-readable output: one JSON object per line on stdout ([JSON output](#json-output)) |
| `--log-level LEVEL` | Show log messages on stderr from this level up: `debug`, `info`, `success`, `warning`, `error`, `critical`. The log file always gets everything |
| `--jlink-library PATH` | The SEGGER J-Link library to use. Default: `$HWIO_JLINK_LIBRARY`, the setting of the app, or the installed J-Link Software ([Installing](install.md#segger-j-link-software)) |
| `--nrfutil PATH` | nRF Util for the modem firmware. Default: `$HWIO_NRFUTIL`, the setting of the app, the `PATH`, or `~/.nrfutil/bin` |

### Choosing the probe

Commands that use a J-Link take these options (on the group, such as `hwio chester app -n …`, or on the command):

| Option | Meaning |
|---|---|
| `-n`, `--jlink-sn SERIAL_NUMBER` | The J-Link serial number. Default: the first probe found |
| `--jlink-speed auto\|KHZ` | J-Link clock speed: `auto` (default) or 5 to 50000 kHz |

## J-Link probes

| Command | Does |
|---|---|
| `hwio jlink list` | Lists the connected probes: serial number, product, connection, firmware, and which program uses it |
| `hwio jlink info [-n SN]` | Shows one probe and whether its firmware is up to date |
| `hwio jlink update-firmware [-n SN] [--yes]` | Updates the probe's firmware to the one bundled with the J-Link library (asks first) |

## CHESTER: application MCU (`hwio chester app`)

### Console

```sh
hwio chester app console [--reset] [--latency MS] [--history-file PATH] [--console-file PATH]
                         [--auto-reconnect] [--no-suggestions] [--linger S]
```

Starts the console: the device's shell and its log, with the commands you type sent to the shell.

| Option | Meaning |
|---|---|
| `--reset` | Reset the application firmware before attaching |
| `--latency MS` | How often the console (RTT) is read, in ms (default 50) |
| `--history-file PATH` | Command history file (default: the history shared with the app) |
| `--console-file PATH` | Console file; `''` for none. Default: a new file per session in the [console folder](console.md#console-files) |
| `--auto-reconnect` | Keep attaching again after the link drops, instead of staying disconnected until <kbd>F4</kbd> |
| `--no-suggestions` | No hints from the command history for this run |
| `--linger S` | Without a terminal or with `--json`: keep collecting output for S seconds after the input ends (default 1) |
| `--coredump-file PATH` | Accepted for compatibility with py-hardwario; no effect |

**Interactive** (in a terminal): <kbd>Enter</kbd> sends the line, <kbd>↑</kbd>/<kbd>↓</kbd> walk the history,
<kbd>Ctrl</kbd>+<kbd>R</kbd> searches it, grey hints come from the history, <kbd>Tab</kbd> completes the device's
commands, <kbd>F4</kbd> reconnects, <kbd>Ctrl</kbd>+<kbd>C</kbd> clears the line (on an empty line it quits),
<kbd>Ctrl</kbd>+<kbd>D</kbd> or <kbd>Ctrl</kbd>+<kbd>Q</kbd> quits. Device output goes to stdout, status lines
(`-- connected --`, `Console file: …`) to stderr.

**Non-interactive** (stdin is a file or a pipe): each input line is sent as a command; at the end of the input the
console waits `--linger` seconds and quits.

```sh
echo "info show" | hwio chester app console
hwio --json chester app console < commands.txt > session.ndjson
```

### One command

```sh
hwio chester app command [--reset] [-t SECONDS] [--console-file PATH] COMMAND
```

Sends the command (several commands: one per line) and prints the answer, collected for `-t` seconds (default 1)
after the command was echoed. Writes a console file per run unless `--console-file ''`.

### Information

| Command | Does |
|---|---|
| `hwio chester app info` | Shows what the device reports with `info show`: firmware name and version, product, hardware, serial number, … |
| `hwio chester app info save [--with-secrets] [PATH]` | Saves the device information (`info show`, `config show`) to a text file, see below |
| `hwio chester app pib read` | Reads the Product Information Block from the UICR |
| `hwio chester app uicr read [--format hex\|bin] [--force] FILE` | Saves the UICR customer area (128 bytes) to FILE, or to stdout with `-` |
| `hwio chester app catalog-info ID` | Shows the name, version, upload time and SHA-256 of a catalog firmware |
| `hwio chester app check` | Checks the connection step by step: probe, power, connection, MCU type, console. Never writes to the device |

<a id="device-information-file"></a>
#### Device information file

`hwio chester app info save` (and `hwio ble info save` over Bluetooth) asks the device `info show` and `config show`
(not shown in a console) and writes the answers into one text file, the same file as **Save device information…** in
the app ([Console](console.md#save-device-information)):

- `PATH` omitted: the file `<serial number>_<YYYY-MM-DD_HHMMSS>.txt` in the current folder, for example
  `2159018267_2026-11-25_141502.txt`; `PATH` a folder: that name in the folder; any other `PATH`: exactly that file.
  An existing file is never replaced (`io.file_exists`, exit code 3).
- Secrets (claim token, BLE passkey, LoRaWAN keys, LTE passwords) are written as `<redacted>`; `--with-secrets`
  writes them as the device prints them. On Linux and macOS only your user can read the file either way.
- A command without an answer within 10 s is written as `(no answer)`; the file is still written (exit code 0) and
  the output names the command. A device without a serial number gets `unknown-<J-Link serial number or Bluetooth
  address>_…`.
- Output: the path written. With `--json`:
  `{"path":"…/2159018267_2026-11-25_141502.txt","serial":"2159018267","commands":[{"command":"info show","answered":true,"lines":11},{"command":"config show","answered":true,"lines":24}]}`
  (`serial` is `null` when the device reported none).

### Flashing

```sh
hwio chester app flash [--halt] [--allow-uicr] [--no-cache] [HEX_FILE_OR_ID]
hwio chester app flash --backup latest|PATH [--force]
hwio chester app backups [--serial N]
```

Flashes a HEX file, or a 32-digit firmware ID from the catalog. Without an argument it looks for the build output in
`build/` or `build/zephyr/` of the current folder (`merged.hex`, `zephyr.hex`). The UICR and the Product Information
Block are kept ([Flashing](flashing.md#uicr-and-the-product-information-block)).

| Option | Meaning |
|---|---|
| `--halt` | Leave the CPU halted after flashing |
| `--allow-uicr` | Also program the image's UICR records (the PIB is kept) |
| `--no-cache` | Download a catalog firmware even when it is in the cache |
| `--backup latest\|PATH` | Flash an application copy saved by the guided LTE update: `latest` is the newest copy of the connected device (by the serial number in its PIB); `PATH` a copy's file (or its name in the backups folder) |
| `--force` | With `--backup`: also flash a copy of another device (refused otherwise) |

`hwio chester app backups` lists the application copies of the guided LTE update, newest first: time, serial number,
size and path (`--json`: `directory`, `serial`, `backups[]` with `time`, `serial`, `path`, `size`). Without
`--serial` it lists the copies of the connected device when a J-Link reaches it, else all copies. The backups folder
keeps the newest 10 copies ([Privacy and stored data](privacy.md)).

| Command | Does | Asks first |
|---|---|---|
| `hwio chester app erase` | Erases the application firmware; keeps the UICR and the PIB | no |
| `hwio chester app erase --all [--yes]` | Erases all flash and the UICR; the PIB is lost | yes |
| `hwio chester app reset [--halt]` | Resets the MCU (`--halt`: and keeps the CPU halted) | no |
| `hwio chester app recover [--yes]` | Removes read-back protection by erasing everything ([Recover](flashing.md#recover)) | yes |

### Low-level target commands

| Command | Does |
|---|---|
| `status` | Whether the CPU is halted, its core ID and the J-Link device |
| `halt`, `go` | Halt the CPU or let it run, without a reset |
| `regs [--halt]` | The CPU registers. The CPU must be halted, or `--halt` halts it for the read and lets it run again |
| `zones` | The memory zones the J-Link library reports |
| `mem read [-l LEN] [-w 8\|16\|32] [-o FILE] ADDR` | Print memory as a hexdump (default 64 bytes, at most 16 MiB), or save the raw bytes to a new FILE |
| `mem write [-i FILE] [-w 8\|16\|32] [--yes] ADDR [HEX]` | Write bytes (for example `0102aabb`) to RAM or peripheral registers, at most 64 KiB (asks first) |
| `flash-write [-i FILE] [--allow-uicr] [--yes] ADDR [HEX]` | Program bytes into flash, at most 1 MiB; the rest of each flash page is kept (asks first). The PIB stays protected |

`-l` is `--length`, `-w` `--width` (bits), `-o` `--output`, `-i` `--input`. Addresses and lengths are decimal or `0x` hex. Writing memory can make the device run any code, which can also
erase its flash and the PIB.

## CHESTER: LTE MCU (`hwio chester lte`)

| Command | Does | Asks first |
|---|---|---|
| `hwio chester lte flash [--guided] [--allow-uicr] [--no-cache] [FILE]` | Flashes the HARDWARIO LTE firmware (downloaded and verified) when no FILE is given, or a CHESTER LTE bundle `.zip`, a Nordic modem firmware `.zip` or an application `.hex` | no |
| `hwio chester lte erase [--yes]` | Erases all flash and the UICR of the LTE MCU; the PIB is lost | yes |
| `hwio chester lte reset` | Resets the LTE MCU and lets it run | no |
| `hwio chester lte recover [--yes]` | Erases everything, then programs the image that keeps the MCU debuggable | yes |
| `hwio chester lte check` | Checks the connection step by step: probe, power, connection, MCU type | no |

`status`, `halt`, `go`, `regs`, `zones`, `mem read`, `mem write` and `flash-write` work as for the application MCU.
The LTE MCU has no console. Modem firmware needs nRF Util.

`--guided` runs the [LTE firmware update](flashing.md#lte-firmware-update) of the app in the terminal: start with the
J-Link on the APP connector; it says what will happen and asks `Continue? [y/N]` (no: exit code 130, nothing
changed), then saves a copy of the application, erases the application, asks you to move the J-Link
to the LTE connector, updates the LTE MCU, asks you to move it back and writes the application back. Each step
continues by itself as soon as the J-Link reaches the right MCU; the copy's path is printed at the end. Ctrl+C after
the erase asks `Write the application back first? [Y/n]`: yes (or Enter, or the end of the input) waits for the J-Link
on the APP connector, writes the copy back and ends with exit code 130 and "LTE firmware not updated. The application
was written back."; `n` leaves the application MCU erased. When there is
no copy to write back (an empty or unreadable application), the command ends with exit code 130 and the application
MCU stays empty: flash an application with `hwio chester app flash`. It needs an
interactive terminal (not with `--json`). Without `--guided` the command flashes only the LTE MCU, as py-hardwario's
`hardwario chester lte flash` does, and leaves the application running; it first warns (human output only) that the
application firmware can reset the LTE MCU during the update. When that happens the command fails with
[`target.reset_during_update`](troubleshooting.md#target.reset_during_update): use `--guided`. With `--guided` a
failed step keeps its own error (the reset reason is only logged); too low supply voltage reported by nRF Util is
[`target.low_voltage`](troubleshooting.md#target.low_voltage).

<a id="lrw"></a>
## CHESTER: LoRaWAN module (`hwio chester lrw`)

The Murata Type ABZ module of a CHESTER with LoRaWAN ([LoRaWAN module](flashing.md#lorawan-module)). It has its own
LoRaWAN connector and answers there only while the application MCU is stopped. It has no console.

| Command | Does | Asks first |
|---|---|---|
| `hwio chester lrw info` | Shows DEV_ID, flash size and the read-out protection level (`rdp`) of the module; read only | no |
| `hwio chester lrw flash [--address ADDR] [--no-cache] [--direct [--halt]] [FILE]` | Flashes lora-modem for CHESTER 1.4.1 (downloaded from [hardwario/lora-modem](https://github.com/hardwario/lora-modem) and SHA-256 verified, then cached) when no FILE is given, or a `.hex` / `.bin` file, and verifies it | only to unlock a protected module (guided) |
| `hwio chester lrw erase [--yes] [--direct]` | Erases the whole module (flash and data EEPROM) | yes |
| `hwio chester lrw unlock [--yes] [--direct]` | Removes read-out protection level 1; the module erases its flash and data EEPROM, and the original Murata firmware is lost | yes |
| `hwio chester lrw reset [--halt]` | Resets the module (`--halt`: and keeps its CPU halted) | no |

| Option | Meaning |
|---|---|
| `--address ADDR` | Start address of a `.bin` file (default: the flash start, `0x08000000`) |
| `--no-cache` | Download the prepared firmware even when it is cached (no effect with a FILE) |
| `--direct` | No guidance, for scripts: the application MCU is already stopped and the J-Link is on the LoRaWAN connector |
| `--halt` | `flash`: leave the module halted after flashing (needs `--direct`); `reset`: keep it halted |
| `--yes` | Do not ask for confirmation |

Without `--direct`, `flash`, `erase` and `unlock` guide you as the app does: start with the J-Link on the APP
connector; the command stops the application MCU (reset and halt), asks you to move the J-Link to the LoRaWAN
connector, runs the operation, asks you to move it back and starts the application MCU again. Each step continues by
itself as soon as the J-Link reaches the right MCU. A module with read-out protection level 1 (a factory module with
the Murata firmware) is unlocked first after a question (`flash`) or with the erase (`erase`). The guided commands
need an interactive terminal (not `--json`); in a script they fail with a usage error that names `--direct`. Ctrl+C
cancels while the command waits for a cable; the application MCU then stays stopped until CHESTER is reset.

`--direct`, `info` and `reset` connect to the module right away. Before them, stop the application MCU and move the
cable yourself; afterwards start it again:

```sh
hwio chester app reset --halt              # J-Link on the APP connector: stop the application MCU
# move the J-Link cable to the LoRaWAN connector
hwio chester lrw info
hwio chester lrw unlock --direct --yes     # a factory module (RDP 1) only
hwio chester lrw flash --direct            # lora-modem for CHESTER 1.4.1
hwio chester lrw flash --direct lora-modem.bin --address 0x08000000
# move the J-Link cable back to the APP connector
hwio chester app reset                     # the application runs again
```

When the module does not answer ([`target.connect_failed`](troubleshooting.md#target.connect_failed)), the
application MCU is probably running: the error suggests `hwio chester app reset --halt`. The probe options (`-n`,
`--jlink-speed`) work as for the other CHESTER commands. The chip of the module is also `stm32l0-cat5-192k` for
[`hwio stm32`](#stm32).

<a id="stm32"></a>
## STM32 chips (`hwio stm32`)

Flashes, erases, unlocks and resets an STM32L0 chip on the J-Link's SWD pins ([Flashing STM32
chips](flashing.md#stm32-chips)). Every command except `chips` needs `--chip ID`, the chip you expect; when the
connected chip is another, the command fails with [`target.wrong_chip`](troubleshooting.md#target.wrong_chip) and
names the chip it found.

| Command | Does | Asks first |
|---|---|---|
| `hwio stm32 chips` | Lists the chips that can be chosen: chip ID, name, DEV_ID, data EEPROM size and the chips each entry covers | no |
| `hwio stm32 info --chip ID` | Shows DEV_ID, REV_ID, flash size and the read-out protection level (`rdp`) of the connected chip; read only, works on a protected chip too | no |
| `hwio stm32 flash --chip ID [--address ADDR] [--erase-all [--yes]] [--allow-eeprom] [--halt] FILE` | Flashes a `.hex` or `.bin` file and verifies it | with `--erase-all` |
| `hwio stm32 erase --chip ID [--yes]` | Erases the whole chip (flash and data EEPROM) | yes |
| `hwio stm32 unlock --chip ID [--yes]` | Removes read-out protection level 1; the chip erases its flash and data EEPROM | yes |
| `hwio stm32 reset --chip ID [--halt]` | Resets the chip (`--halt`: and keeps the CPU halted) | no |

| Option of `flash` | Meaning |
|---|---|
| `--address ADDR` | Start address of a `.bin` file (default: the flash start, `0x08000000`) |
| `--erase-all` | Erase the whole chip (flash and data EEPROM) first; otherwise only the written sectors are erased |
| `--allow-eeprom` | Also program the file's data EEPROM records; without it a file that writes the data EEPROM is refused ([`firmware.out_of_range`](troubleshooting.md#firmware.out_of_range)) |
| `--halt` | Leave the CPU halted after flashing |

Chip IDs are those of `hwio stm32 chips`, for example `stm32l0-cat3-64k` (STM32L0 cat. 3, 64 KiB: STM32L051 / L052 /
L053 / L062 / L063, the CHESTER-S1 controller) or `stm32l0-cat5-192k` (Murata Type ABZ). On a chip with read-out
protection level 1, `flash` and `erase` fail with [`target.protected`](troubleshooting.md#target.protected):
run `hwio stm32 unlock` first. Level 2 ([`target.permanently_protected`](troubleshooting.md#target.permanently_protected))
cannot be removed. The probe options (`-n`, `--jlink-speed`) work as for the CHESTER commands.

```sh
hwio stm32 chips
hwio stm32 info --chip stm32l0-cat3-64k
hwio stm32 flash --chip stm32l0-cat3-64k firmware.hex
hwio stm32 flash --chip stm32l0-cat3-64k --address 0x08000000 firmware.bin
hwio stm32 unlock --chip stm32l0-cat5-192k --yes
```

## Bluetooth LE (`hwio ble`)

| Command | Does |
|---|---|
| `hwio ble scan [--timeout S] [--all] [--profile ID]` | Lists CHESTER devices nearby (5 s; `--all`: every device), strongest signal first. RSSI is the average over the scan; JSON also has `rssiLast`, the last measured value |
| `hwio ble console (-a ID \| --name N) [--auto-reconnect] [--history-file PATH] [--console-file PATH]` | The console over Bluetooth |
| `hwio ble command (-a ID \| --name N) [-t SECONDS] [--console-file PATH] COMMAND` | One command; ends at the prompt or after `-t` seconds (default 3) |
| `hwio ble info save (-a ID \| --name N) [--with-secrets] [PATH]` | Saves the [device information file](#device-information-file) over Bluetooth |
| `hwio ble pair (-a ID \| --name N)` | Asks the system to pair; it asks for the passkey (Linux, Windows) |

`-a`/`--address ID` is the Bluetooth address (on macOS the device UUID shown by `hwio ble scan`); `--name N` is a part
of the advertised name, which exactly one device found by a 5-second scan must match. See [Bluetooth LE](bluetooth.md).

<a id="remote-support-hwio-remote"></a>
## Remote support (`hwio remote`)

| Command | Does |
|---|---|
| `hwio remote host [-n SN] [--jlink-speed S] [--latency MS] [--console-file PATH] [--auto-reconnect] [--relay URL] [--deny-operations]` | Shares the CHESTER console over J-Link with HARDWARIO Support: prints the 6-digit session code and the link, then streams the console |
| `hwio remote host --ble (-a ID \| --name N) [...]` | The same for a Bluetooth console (no operations) |
| `hwio remote join CODE\|LINK [--relay URL] [--linger S]` | Joins a session: the console (interactive on a terminal, plain lines or `--json` NDJSON otherwise) |
| `hwio remote join CODE\|LINK --command CMD [-t S]` | One command after the customer allowed you; the response as in `chester app command` |
| `hwio remote join CODE\|LINK --flash ID \| --erase \| --recover \| --reset [--target app\|lte]` | Asks the customer's HARDWARIO Monitor for one operation (flash: catalog firmware ID, application core only) |
| `hwio remote join CODE\|LINK --lte-update` | Asks the customer's HARDWARIO Monitor to update the LTE firmware to its recommended version (the guided update: the customer moves the J-Link cable between the APP and LTE connectors); no firmware ID, no `--target` |

`hwio remote host` asks on the terminal (stderr, answer `y` to allow) before it allows a join or an operation, and any
other input line keeps sharing after the idle warning. Without a terminal on stdin joins are never allowed: the session
ends at the first join. `--deny-operations` denies every operation request without asking. An allowed LTE update runs
the terminal flow of `hwio chester lte flash --guided` (with `--nrfutil` if given): follow its prompts and move the
cable when it asks; if it ends without writing the application back, support gets `remote.application_not_restored`.
When the joiner answers the update's questions (HARDWARIO Monitor 2.0.0-rc.4 or newer), support confirms **Continue**
and **Continue without a copy** instead of you, and the copy is written back without a question; without an answer
within 60 seconds the update is cancelled before anything changes.
The host learns the device's command list as `chester app console` does and sends it to an allowed HARDWARIO Monitor
joiner, whose interactive console completes commands with Tab from it (subcommands are read by the host's silent
`<command> -h`, not shown in the console). Ctrl+C ends sharing
(exit code 130). With `--json` the host prints `{"type":"remote","event":"sharing","code","link","relay"}`, the
questions (`"event":"question"`) and `{"type":"remote","event":"ended","error"}` between the console events.

The server is `https://terminal.hardwario.com` unless the setting `remote/relayUrl` or `--relay URL` names another one
(https only). A link to another server needs confirmation on a terminal and is refused without one. A denied request or
a join that was not allowed exits with 130 (`remote.operation_denied`, `remote.join_not_allowed`). The result line of
an operation names it and the time (*Customer's device reset (14:02).*). On a terminal, `--lte-update` asks you the
customer's Monitor's questions (*… Continue? [y/N]*, *The application could not be copied. Continue without a copy?
[y/N]*; with `--json` also `{"type":"remote","event":"question","kind":"continue"|"continueWithoutCopy","text"}`); no,
or no answer within 60 seconds, cancels the update before anything changes. Without a terminal the customer answers
them. See [Remote support](remote-support.md).

## Other commands

| Command | Does |
|---|---|
| `hwio doctor` | Checks the J-Link Software, nRF Util, USB access, programs holding a probe, the probes and their firmware, the folders and Bluetooth |
| `hwio diag report [-o\|--output FILE]` | Writes a diagnostic report for HARDWARIO support ([Privacy](privacy.md#diagnostic-report)) |
| `hwio console history clear [--yes]` | Deletes the saved command history of all devices (app and command line) |
| `hwio update check [--prerelease]` | Asks GitHub for the newest release; nothing is downloaded or installed |
| `hwio mcp [options]` | The MCP server for AI assistants ([MCP server](mcp.md)) |

`hwio doctor` prints one line per check:

```text
✓ J-Link Software              8.10 · /opt/SEGGER/JLink_V810
✓ nRF Util                     8.2.1, device 2.23.0
! USB access to J-Link         Accessible to every account
✓ Other programs using J-Link  None found
✓ J-Link probes                1 found: <serial>
✓ J-Link firmware              Not known
✓ Settings and log folders     Writable
✓ Bluetooth                    Adapter ready (<adapter>, <address>)

1 warning. Run `hwio diag report` to create a report for HARDWARIO support.
```

`!` is a warning, `✗` a failure; each comes with what to do. With `--json` each check has an `id` (`jlink-software`,
`nrfutil`, `usb-access`, `probe-holders`, `probes`, `probe-firmware`, `app-dirs`, `bluetooth`), and the exit code is
that of the first failed check.

## Confirmations

`erase --all`, `lte erase`, `recover`, `stm32 erase`, `stm32 unlock`, `stm32 flash --erase-all`, `mem write`,
`flash-write`, `jlink update-firmware` and `console history clear` ask `[y/N]` in a terminal. Without a terminal, or
with `--json`, they do not ask: add `--yes`, otherwise they fail with
[usage.confirmation_required](troubleshooting.md#usage.confirmation_required). `flash` (without `--erase-all`),
`chester app erase` without `--all`, and `reset` never ask.

<kbd>Ctrl</kbd>+<kbd>C</kbd> cancels a running command cleanly (exit code 130); the probe is released.

## JSON output

With `--json`, `hwio` prints one JSON object per line on stdout, an *envelope*:

```json
{"data":{...},"ok":true,"schema":1}
{"error":{"category":"probe","cause":"...","code":"probe.not_found","detail":"...","message":"...","suggestion":"...","wizard":null},"ok":false,"schema":1}
```

- `schema` changes only with incompatible changes.
- `error.code` is the stable error id ([Troubleshooting](troubleshooting.md)); `category` gives the exit code.
- Logs never go to stdout.

Commands that run for a while print several envelopes (NDJSON):

- **Console** (`chester app console`, `ble console`): `{"type":"line","seq","time","channel":"shell"|"log","dir":"out"|"in"|"marker","level","text"}`,
  `{"type":"connection","state":"connecting"|"probeOpen"|"connected"|"disconnected"|"stopped","error","transport"}`,
  `{"type":"dropped","count"}`, `{"type":"warning","error"}`, and at the end `{"type":"consoleFile","path"}` and a
  `stopped` connection event.
- **Flash, erase, recover**: `{"type":"progress","step","steps","phase","done","total"}`, then
  `{"type":"result",…}`.

```sh
hwio --json chester app command version | jq -r '.data.output[]'
hwio --json chester app console < cmds.txt | jq -c 'select(.data.type=="line" and .data.channel=="log")'
```

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Internal error (a bug: please report it with the diagnostic report) |
| 2 | Invalid arguments or options, an unknown command (also with `--help`); a confirmation is needed (`--yes`); `ble pair` on macOS |
| 3 | A file was not found (for example the firmware file of `chester app flash` or `chester lte flash`), could not be read or written; settings |
| 4 | J-Link Software or nRF Util missing or too old; Bluetooth adapter missing, off or not allowed |
| 5 | J-Link probe not found, busy or with a firmware problem; Bluetooth device used by another program |
| 6 | The device does not respond, has no power or no console, is protected; Bluetooth connection problems |
| 7 | Invalid firmware image, verification failed, unknown catalog ID |
| 8 | Download or catalog failure |
| 9 | Timeout |
| 130 | Cancelled or not completed (<kbd>Ctrl</kbd>+<kbd>C</kbd>, SIGTERM, guided procedure ended without restoring the application) |

The codes are part of the public interface and do not change between versions.

## Environment variables

| Variable | Meaning |
|---|---|
| `HWIO_JLINK_LIBRARY` | The J-Link library (below `--jlink-library`, above the setting) |
| `HWIO_NRFUTIL` | nRF Util (below `--nrfutil`, above the setting) |
| `HWIO_MCP_TOKEN` | The bearer token of `hwio mcp --http` ([MCP server](mcp.md#security)) |
| `HWIO_NO_CRASH_HANDLER=1` | No crash reports |
| `NO_COLOR` | No colours in the output |
