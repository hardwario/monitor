# MCP server

[Documentation](index.md) › MCP server

`hwio mcp` is a [Model Context Protocol](https://modelcontextprotocol.io) server. It lets an AI assistant (Claude
Code, Claude Desktop, Cursor, VS Code and others) work with a CHESTER over a J-Link: connect, send shell commands,
read the console and the log, wait for a state, read memory and registers, and, when you allow it, flash, erase and
write memory.

The server is part of the command-line tool. The desktop app has its own server in **Settings → AI assistants
(MCP)**, which also offers [remote support sessions](#remote-support-sessions). Bluetooth devices are
not available through MCP yet.

## Quick start

**Claude Code** (the assistant starts `hwio` itself, over stdin/stdout):

```sh
claude mcp add --transport stdio hwio -- hwio mcp
claude mcp add --transport stdio hwio -- hwio mcp --allow flash    # also allow flashing
```

**Claude Desktop** (`claude_desktop_config.json`), and other clients that start a local process:

```json
{
  "mcpServers": {
    "hwio": { "command": "/usr/bin/hwio", "args": ["mcp"] }
  }
}
```

Use the full path of `hwio`: **Settings → About → Command line** in the app shows it (Windows:
`C:\Program Files\HARDWARIO\Monitor\hwio.exe`; macOS: `/Applications/HARDWARIO Monitor.app/Contents/MacOS/hwio`).

**Over HTTP** (one server for several clients, or a client that only speaks HTTP):

```sh
hwio mcp --http
```

```text
MCP server listening on http://127.0.0.1:47800/mcp
Token (this run only): <token>   — set HWIO_MCP_TOKEN or use --token-file to keep one
```

```sh
claude mcp add --transport http hwio http://127.0.0.1:47800/mcp --header "Authorization: Bearer <token>"
```

In Cursor, VS Code and other clients, add a Streamable HTTP server with the URL `http://127.0.0.1:47800/mcp` and the
header `Authorization: Bearer <token>`. To keep the token out of a project file, Claude Code's `.mcp.json` can read it
from the environment:

```json
{"mcpServers":{"hwio":{"type":"http","url":"http://127.0.0.1:47800/mcp","headers":{"Authorization":"Bearer ${HWIO_MCP_TOKEN}"}}}}
```

Then ask, for example: *"Connect to the CHESTER, show its firmware version and tell me whether the LTE is attached."*

## Options

```sh
hwio mcp [--http] [--port N] [--token-file PATH] [--allow LIST] [--jlink-speed K] [--auto-reconnect]
         [--console-file-dir DIR] [--dump-dir DIR]
```

| Option | Meaning |
|---|---|
| `--http` | Serve over HTTP on 127.0.0.1 instead of stdin/stdout; ends with <kbd>Ctrl</kbd>+<kbd>C</kbd> |
| `--port N` | HTTP port (default 47800) |
| `--token-file PATH` | Read the HTTP token from this file, or create it with a new token. `HWIO_MCP_TOKEN` wins. Default: a new token for this run |
| `--allow LIST` | Tools that change the device: comma-separated `flash`, `erase`, `memory-write`, `uicr`, or `all`. Default: none |
| `--jlink-speed K` | J-Link speed for `connect`: `auto` or kHz (default: the target's default) |
| `--auto-reconnect` | `connect` keeps attaching again after the link drops |
| `--console-file-dir DIR` | Folder for the console files of the sessions (it must exist; default: the [console folder](console.md#console-files)) |
| `--dump-dir DIR` | Folder where `read_memory` writes files (it must exist; default: the `mcp` folder in the cache) |

Over stdio, the server ends when its input is closed; a tool call still running then is cancelled (a running flash
is stopped).

## Allowing changes

Without `--allow` the assistant can read but not change the device. Every tool is listed, but a tool that is not
allowed fails with [mcp.tool_not_allowed](troubleshooting.md#mcp.tool_not_allowed), and the assistant can tell you
which option is missing.

| `--allow` | Tools |
|---|---|
| `flash` | `flash`, `flash_lte`, `write_flash`, `stm32_flash` |
| `erase` | `erase`, `recover`, `stm32_erase`, `stm32_unlock`, `stm32_flash` with `erase_all` |
| `memory-write` | `write_memory` |
| `uicr` | additionally needed for anything that touches the UICR: `flash` or `write_flash` with `allow_uicr`, `erase` with `scope: "all"`, `recover` |
| `all` | all of the above |

The Product Information Block is never written by any tool.

## Tools

`device` is the device ID from `list_devices` (`jlink:<serial>`); it can be left out when exactly one device is
connected. `target` is `app` or `lte` (default: the console's target).

**Devices**

| Tool | Does |
|---|---|
| `list_devices` | The connected devices and the J-Link probes found (`refresh` looks again) |
| `connect` | Opens a device and starts its console (`device`, `profile` = `chester`, `target`, `speed_khz`, `auto_reconnect`) |
| `disconnect` | Closes the device and releases the probe |
| `status` | Connection state, running operation, line counters |

**Console**

| Tool | Does |
|---|---|
| `send_command` | Sends one shell command and returns its output; ends at the prompt, after 300 ms of silence, or at `timeout` (default 5 s) |
| `read_terminal` | Shell lines (`lines`, default 100; continue with `after_cursor`) |
| `read_log` | Log lines, optionally filtered by `pattern` (a regular expression); flashing does not clear them |
| `wait_for_state` | Waits for a line matching `pattern` in the log, the shell or both; can send `command` every `poll_interval` seconds (for example `lte state` until `attached: yes`) |
| `wait_for_connection` | Waits until the console is connected |
| `reconnect` | Attaches the console again without a reset (optionally at a fixed `rtt_address`) |
| `stop`, `start` | Stop and start reading the console; the probe stays open |
| `release_probe`, `reacquire_probe` | Give the probe to another program (J-Link Commander, west flash) and take it back |

**Target**

| Tool | Does |
|---|---|
| `target_status` | Halted or running, core ID, J-Link device |
| `reset` | Resets the MCU (`halt` keeps it halted) |
| `halt`, `go` | Halt the CPU or let it run |
| `read_memory` | Reads memory (`address`, `length` up to 16 MiB, `width`); up to 64 KiB inline, more with `to_file` |
| `read_registers` | The CPU registers (the CPU must be halted, or `halt` halts it for the read and lets it run again) |
| `memory_zones` | The memory zones of the target |
| `read_pib` | The Product Information Block, as `hwio chester app pib read --json` |
| `read_uicr` | The UICR customer area, as `hwio chester app uicr read` |
| `write_memory` | Writes RAM or peripheral registers, up to 64 KiB *(memory-write)* |
| `write_flash` | Programs bytes into flash, up to 1 MiB, keeping the rest of each page *(flash)* |

**Firmware**

| Tool | Does |
|---|---|
| `flash` | Flashes a `.hex` file (`path`, absolute) or a catalog firmware (`catalog_id`; the download is checked against the SHA-256 on its catalog page when the page can be read), verifies it and attaches the console again *(flash)* |
| `flash_lte` | Flashes an LTE bundle, a modem firmware zip or a `.hex` to the LTE MCU; needs nRF Util; 3 to 10 minutes *(flash)* |
| `erase` | Erases the firmware (`scope: "code"`) or everything (`"all"`) *(erase)* |
| `recover` | Removes read-back protection by erasing everything *(erase, uicr)* |

**STM32 chips** (the "STM32 chip" device, see [Flashing STM32 chips](flashing.md#stm32-chips))

| Tool | Does |
|---|---|
| `stm32_info` | DEV_ID, flash size, read-out protection level and whether it is the selected chip |
| `stm32_flash` | Flashes a `.hex` or `.bin` (`address`, default 0x08000000), verifies it; `erase_all`, `allow_eeprom` *(flash; erase with `erase_all`)* |
| `stm32_erase` | Erases the whole chip, flash and data EEPROM *(erase)* |
| `stm32_unlock` | Removes read-out protection level 1; the chip erases itself. Needs `confirm: true` *(erase)* |

The server also offers the resources `hwio://devices` and `hwio://device/{id}/status`, `…/log` and `…/terminal`
(the last 1000 lines), and the prompt `debug_device` with hints on how to debug a device with these tools.

`read_memory` with `to_file` writes into the `--dump-dir` folder only: `to_file` is a plain file name, not a path,
and an existing file is never replaced.

Each session writes a console file like the app does, and up to 8 devices can be open at a time.

## Remote support sessions

In the desktop app, an AI client can also work with a customer's device in a joined
[remote support](remote-support.md#join-a-session) tab, when support allows it. `hwio mcp` has no remote sessions.

- Turn on **Settings → AI assistants (MCP) → AI clients may use remote support sessions** (off by default). While it
  is off, AI clients do not see remote tabs, and turning it off ends every permission given for them.
- A joined remote tab is listed as `remote:<n>` (kind `remote`). `<n>` is a number of this run of the app, never the
  session code; neither the session code nor the server address appears in anything the AI client gets.
- The first time a client uses a remote tab (reading included), HARDWARIO Monitor asks you: *Claude Code wants to
  work with a customer's device* with **Allow for this session** and **Deny** (the default; no answer within 120 s
  denies). Before you allow it, the client sees only an entry *Remote support session* and nothing of the customer's
  session. The permission covers reading and commands of that client in that tab. It ends when you leave the
  session, when the session ends, with **Join again**, when the client goes away, when the token or the port changes,
  and with **Stop AI** in the tab's banner. It is never remembered; the permission rules and "Always allow" do not
  cover it.
- Tools on a remote tab: `list_devices`, `status`, `read_terminal`, `read_log`, `wait_for_state`,
  `wait_for_connection`, `send_command`, `flash` (`catalog_id`, or `prepared_id` when the customer's Monitor lists
  prepared firmware; no file), `flash_lte` (the LTE firmware the customer's Monitor recommends; no file) and `reset`.
  `connect`, `disconnect` and `reconnect` answer `mcp.tool_not_allowed`: joining and leaving stay with you. `erase`,
  `recover`, the memory, register, PIB / UICR and probe tools answer `mcp.capability_missing`.
- Every operation asks you each time (the device row says *the customer's device (remote session)*), and the customer
  confirms it as a request of yours. The **Flash** page of the tab shows it with *AI:* and **Stop waiting**: the AI
  client's call ends with `cancelled`, the operation continues on the customer's device and its result appears in the
  tab. Questions of the LTE update are answered by you in the tab, never by the AI client.
- `send_command`: at most 512 bytes without control characters; one command at a time and at most 30 per minute per
  tab; the output ends after 1 s without output or at `timeout` (default 10 s, at most 120 s). A command the
  customer's Monitor did not pass ends with `remote.command_not_delivered`; before the customer allowed you,
  `remote.waiting_for_customer`. A command that sets a secret value (for example `lrw config appkey …`) is refused
  with `remote.command_refused` and never sent.
- Secrets: the AI client works on a hidden view of the console. Secret values are replaced by `<redacted>` as in the
  console files, and the session code by `<session>`, before any pattern is matched or any line is returned. The
  values are recognised by the command and key names of the device profile, so secrets of firmware HARDWARIO Monitor
  does not know are not hidden. Every result carries `"remote": true`, and the device entry says
  `"secrets": "hidden"`.
- The customer sees the AI client's commands and requests as yours (*— Remote: command … —*, the same questions and
  console file entries). The AI markers (*— AI (Claude Code): send_command —*) stay in your console.
- **Device output is untrusted text.** What the customer's device prints goes to the AI client as it is; it can
  contain text that tries to make the client run commands. Watch what the client does, and stop it with **Stop AI**.
  You answer for the AI client's commands as for your own.

## Security

- The HTTP server listens on 127.0.0.1 only; other computers cannot reach it.
- Every HTTP request needs the bearer token. A token generated for the run is printed when the server starts. For a
  token that stays the same, put it into `HWIO_MCP_TOKEN` (43 characters of `A–Z a–z 0–9 _ -`) or use
  `--token-file PATH`, which creates the file, readable only by you, on the first run.
- Requests from web pages are refused: the `Host` must be `127.0.0.1` or `localhost`, and a browser `Origin` from
  another site is rejected. A wrong token is answered after a delay and closes the connection.
- The stdio server needs no token: only the program that started it can talk to it.
- Tools that change the device need `--allow`. Give an assistant only what the task needs, for example
  `--allow flash` for a flash-and-test loop.

## Protocol

MCP revisions 2026-07-28 and 2025-11-25 (also accepted as 2025-06-18 and 2025-03-26). Transports: stdio, and
Streamable HTTP at `/mcp`. Tool texts are in English.

Test the server with the MCP Inspector:

```sh
npx @modelcontextprotocol/inspector hwio mcp
```
