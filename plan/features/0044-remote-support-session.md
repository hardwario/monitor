# 0044 — Remote support session (HARDWARIO Terminal compatible)

- **Status:** In progress (approved 2026-11-09, Matej Strnad; implementation started 2026-11-10)
- **Milestone:** 2.0.0-rc.4 (ADR-0337, supersedes ADR-0326)
- **Author:** Matej Strnad, Claude
- **Related ADRs:** ADR-0334 (strict compatibility with the existing relay, 6-digit code), ADR-0335 (join approval),
  ADR-0336 (operations tunneled in `at` / `write`), ADR-0321 (QtWebSockets + own Socket.IO client),
  ADR-0322 (scope: console + flash/erase/recover/reset), ADR-0323 (firmware by catalog ID only),
  ADR-0324 (operation confirmation), ADR-0325 (J-Link RTT and BLE, host and join roles), ADR-0337 (2.0.0-rc.4; supersedes ADR-0326), ADR-0363 (remote LTE update, amends ADR-0323/0336), ADR-0364 (remote autocomplete),
  ADR-0328 (limits), ADR-0329 (TLS only), ADR-0330 (relay URL), ADR-0331 (CLI), ADR-0332 (no MCP),
  ADR-0333 (audit in the console file), ADR-0354 (tunnel `end`, amends ADR-0336), ADR-0355 (approval again after
  the host's reconnect, amends ADR-0335), ADR-0356 (secrets redacted toward the relay, amends ADR-0329),
  ADR-0357 (relay events through a BoundedQueue, amends ADR-0321), ADR-0358 (delivery confirmed before the host
  disconnects, amends ADR-0354); superseded: ADR-0320, ADR-0327; builds on ADR-0004, ADR-0007, ADR-0037
- **Depends on:** 0013 (operations), 0037 firmware catalog. Nothing on the relay or the web Terminal (ADR-0334).

## Context

HARDWARIO Support often needs to see a customer's CHESTER console and run a few commands. Today the customer can do
that only through the web [HARDWARIO Terminal](https://terminal.hardwario.com/) (Chrome, Bluetooth only).
**Create Session** gives a number that the customer reads to support over the phone. Support clicks **Join Session**
and both see the same synchronized console
([docs](https://docs.hardwario.com/chester/platform-connectivity/hardwario-terminal/)).

Monitor users (J-Link RTT or BLE) have no such option. Support can't run commands or flash firmware for them.

### The existing relay and web Terminal (source: `~/terminal`, `server/index.js`, `src/components/console/console.tsx`)

- Fastify + Socket.IO v4 (Engine.IO v4) on terminal.hardwario.com (port 5000 behind the proxy). Clients use
  `transports: ["websocket"]` and the namespace `/<code>`, where the code matches `^\d+$`. There is no
  authentication: the `use` middleware lets everyone in.
- When a socket connects to a namespace, the relay emits `newClient` (payload `null`) to **all** sockets in that
  namespace, the new one included.
- The relay forwards only `at`, `sync` and `write`, with `socket.broadcast.emit`, i.e. to every **other** socket
  in the namespace. Every other event is dropped. It logs payloads only at `debug` (the default level is `info`).
- Web host: generates the code with `Math.floor(Math.random() * 1e6)`. It emits
  `at {event: "state"|"write"|"read", data}`, where `state` is `"disconnected"|"connection"|"connected"`,
  `write` is a command line and `read` is a device output line. On `write` it calls `at.write(payload)`, which sends
  `String(payload) + "\r\n"` to the device. On `newClient` it emits `sync {state, commands}`, where `commands` is
  `[{id, timestamp, command, index, response: {outputs: [string], result: 0}}]`.
- Web joiner: emits `write <line>`. It handles `sync` (replaces its transcript) and `at` with `state` / `write` /
  `read`, and ignores any other `at.event`.

**Security consequence:** anyone who guesses the number while a session is open can read the live console and send
lines to the device. The relay must stay as it is and the code must stay short enough to read over the phone, so
Monitor adds the only protection it can: **confirmation in the app** (ADR-0334, ADR-0335). The host approves every
join before that participant's commands reach the device. Each flash, erase, recover and reset is also confirmed.

## Goals / Non-goals

**Goals**
- Customer (host) shares any open Monitor console (J-Link RTT or BLE) with one click and reads a 6-digit code to support.
- Support joins from the **web Terminal** (console only) **or from Monitor** (console + operations, full console UI:
  search, filter, log levels, save view).
- Full interoperability with the unchanged relay and web Terminal in all four combinations (Monitor/Terminal host ×
  Monitor/Terminal joiner).
- The host approves each join (ADR-0335). Monitor-to-Monitor, support can ask for **flash (catalog firmware ID
  only), erase, recover, reset**, and each one is confirmed by the host (ADR-0322..0324, ADR-0336).
- The host always sees what is remote (marked lines, a session banner) and can end the session at once.
- CLI parity: `hwio remote host` / `hwio remote join` (ADR-0331).

**Non-goals**
- Any change to the relay or the web Terminal; longer codes, secrets or tokens (ADR-0334).
- Removing one participant from a session (the relay can't); refusing a join ends the whole session.
- Arbitrary file transfer, uploading `.hex` over the relay (ADR-0323), UICR/PIB writes, nRF Util modem update remotely.
- MCP over the relay; any MCP tool for remote sessions (ADR-0332).
- End-to-end encryption (impossible while compatible; ADR-0329, ADR-0334).
- STICKER / NFC transport (0041–0043): possible later.

## User experience

### Host (customer)

1. Console toolbar → **Share with support…** (enabled when the tab is connected, J-Link or BLE).
2. Dialog *Share this console*. It explains:
   - What support will be able to do: see the console, send commands after you allow them, and ask to flash /
     erase / recover / reset (each needs your confirmation).
   - That the session passes through the HARDWARIO server (relay host name).
   - **That anyone who knows the code sees the console output**, so read the code only to HARDWARIO Support.
   - The limits (30 min idle, 8 h).

   [Start sharing].
3. Monitor connects with a fresh code (collision check, ADR-0334). Banner on the device tab:
   **"Shared with support — code 482 913"** (large, grouped for reading aloud) [Copy link] [End sharing]. The tab
   label shows a share badge.
4. Someone joins → modal (ADR-0335): *"Someone joined session 482 913. Allow them to send commands to the device?
   Allow only if HARDWARIO Support has just told you they are joining."* [Allow] [End session]. End session is the
   default button. It auto-ends after 2 minutes without an answer. Until the customer allows, remote commands are
   dropped and no history is sent.
5. Allowed commands appear in the console with a **remote** marker (same style as MCP markers, 0014). Join, allow
   and end appear as info lines ("Participant allowed (HARDWARIO Monitor 2.1.0)" when the joiner sent its hello,
   otherwise "(web Terminal or unknown)").
6. Operation request from a Monitor joiner → modal: *"Support asks to flash firmware CHESTER-Clime 3.2.1
   (ID abcd…) to the application core. The device restarts."* [Allow] [Deny]. Deny is the default button. It is
   denied automatically after 60 s.
7. **End sharing**, refusing a join, closing the tab, disconnecting the device or quitting Monitor all end the
   session. So does 30 min idle (with a warning 2 min before and [Keep sharing]) or 8 h in total (ADR-0328).
8. Everything remote is also written to the console file (ADR-0333).

### Joiner (support)

1. Home / connect page → **Join support session…** → type the code (or paste a link; a link to a host other than
   the configured relay needs confirmation, ADR-0330) → a new device tab of the transport **Remote**.
   - With a Monitor host, the title comes from its hello, e.g. "CHESTER-M 1234567890 — remote".
   - With a web Terminal host it is "Session 482 913 — web Terminal".
2. Until the host allows: banner "Waiting for the customer to allow you…". The live output may already appear.
3. The console works as usual: history, search, save view. Completion works where the host's command tree is known.
4. With a Monitor host (hello `ops: true`), the Flash page in the remote tab offers **catalog firmware only** and
   Erase / Recover / Reset. Each one shows "Waiting for the customer's confirmation…", then progress and the
   result as reported by the host. With a web Terminal host the Flash page explains that only the console is
   available.
5. Host ends (a Monitor host sends the tunnel `end`, ADR-0354, or the relay connection drops) → banner "The session
   ended." The tab stays read-only so the log can be saved. With a web Terminal host, Monitor can't tell "ended" from "quiet",
   so after the idle limit it shows "No activity from the customer for 30 min" and offers to leave.

### Error messages (all via `hwio::Error`, translated)

| Code | What / why / what to do |
|---|---|
| `remote.relay_unreachable` | Cannot reach the support server. Network, proxy or firewall blocks `wss://terminal.hardwario.com`. Check the connection or try another network. |
| `remote.code_invalid` | The session code must be a number (6 digits from HARDWARIO Monitor, up to 6 from the web Terminal). Check the code. |
| `remote.code_collision` | Another device is using this session code. The session was ended for safety. Start sharing again to get a new code. |
| `remote.join_not_allowed` | The customer ended the session instead of allowing you (or did not answer in 2 minutes). Ask them to share again. |
| `remote.host_ended` | The customer ended sharing, or their device or Monitor disconnected. |
| `remote.operation_denied` | The customer denied the request (or did not answer in 60 s). |
| `remote.operation_unsupported_peer` | The customer uses the web Terminal, which supports only the console. Ask them to use HARDWARIO Monitor for flashing. |
| `remote.firmware_not_in_catalog` | This firmware ID is not in the HARDWARIO catalog. Remote flashing accepts only catalog firmware. |
| `remote.protocol_error` | Unexpected data in the session. It was ignored, or the session was ended for safety. Report the issue with a diagnostic report. |

## CLI

ADR-0331:

```
hwio remote host  [device selection as in `chester app console` / `ble console`] [--relay URL] [--json]
    # prints the code and link, streams the console; asks on stdin to allow joins and operations;
    # any input extends idle. --deny-operations: refuse operation requests without asking.
    # Without a tty, joins are never allowed (the session ends at the first join, ADR-0335).
hwio remote join  <code|link> [--relay URL] [--json]                 # interactive console
hwio remote join  <code|link> --command "info show"                  # one command (after the host allows)
hwio remote join  <code|link> --flash <firmware-id> | --erase | --recover | --reset [--target app|lte]
```

Exit codes follow the CLI table in 0006 (denied / not allowed = the existing "aborted" code; a new code only if 0006
has none).

## Design

### Modules

- **New `libs/remote`** (QtCore, QtNetwork, **QtWebSockets**, ADR-0321; no QtGui). Headless-testable.
  - `engineio::Client`: Engine.IO v4 over `QWebSocket` (open packet, ping/pong timeouts, close). Text packets
    only. Binary packets are a protocol error.
  - `socketio::Client`: Socket.IO v5 on top of it: namespace connect (no auth payload), `EVENT` with a JSON array,
    no ACK, no binary. Parsing with `QJsonDocument`, size check before parsing.
  - `RelayClient`: connects to `wss://<relay>/socket.io/?EIO=4&transport=websocket` and the namespace `/<code>`.
    The relay URL comes from the setting `remote/relayUrl` or `--relay` (default `https://terminal.hardwario.com`;
    https only; ADR-0330). Emits typed signals for `newClient`, `at`, `sync` and `write` (payload kept as JSON:
    string or object).
  - `SessionCode`: CSPRNG 6-digit generation (100000–999999), parsing of a code or a `?session=` link, grouped display.
  - `RemoteHost`: binds one `device::ConsoleSession` + the operation runner to a `RelayClient`.
    - Device side: `linesReceived` → `at/read`, every line sent (local or remote) → `at/write`, connection events →
      `at/state` (mapped to the three web states).
    - Join approval state machine (ADR-0335) through a `ConsentProvider` interface (GUI dialog / CLI prompt /
      auto-deny).
    - An allowed `write` *string* goes through `ConsoleSession::sendLine` with the origin **remote**. A `write`
      *object* is a tunnel message (ADR-0336).
    - On allow: `sync` from a bounded snapshot in the web Terminal's transcript format, plus the hello.
    - Collision detection: any received `at` or `sync` (ADR-0334).
  - `RemoteConsoleSession` : `device::ConsoleSession` (new `Transport::Remote`), the joiner side. The GUI console
    page and the CLI console reuse it unchanged. `sendLine` → `write` string. `sync` / `at` → `ConsoleLog`
    (`sync` replaces the log contents shown for the session; `write` lines from other participants are shown too).
  - `TunnelCodec`: encode/decode and validate the `hwio` messages of ADR-0336.
- **`libs/device`**: `Transport::Remote`, a line origin `remote` (next to the MCP marker origin), `SessionKey` for
  remote tabs. The operation runner gets a `RemoteOperationClient` for remote tabs (request → accepted/denied →
  progress → result).
- **`app/gui`**: `RemoteShareViewModel`, `RemoteJoinViewModel`, share banner with the code, join-approval and
  operation dialogs, Join dialog (code field with digit validation), Flash page in remote mode (catalog only).
  QML stays thin.
- **`app/cli`**: `remote host` / `remote join` on the same classes.

### Wire protocol (ADR-0334, ADR-0336)

| Event | Sender → receivers | Payload | Notes |
|---|---|---|---|
| `newClient` | relay → all in namespace | `null` | Host: the first one after its own connect is its own and is ignored. Every later one starts join approval. |
| `at` | host → others | `{event: "state", data: "disconnected"\|"connection"\|"connected"}` | web-compatible |
| `at` | host → others | `{event: "write", data: <line>}` / `{event: "read", data: <line>}` | web-compatible; also sent to participants not yet allowed (relay broadcast) |
| `at` | host → others | `{event: "hwio", data: {v: 1, type: "hello"\|"op-accepted"\|"op-denied"\|"op-progress"\|"op-result"\|"end", …}}` | Monitor only; the web Terminal ignores it |
| `sync` | host → others | `{state, commands: [{id, timestamp, command, index, response: {outputs, result: 0}}]}` | sent only after a join is allowed |
| `write` | joiner → others | `<line>` (string) | shell line; the Monitor host passes it to the device only in the allowed state |
| `write` | Monitor joiner → others | `{hwio: {v: 1, type: "hello"\|"op-request", …}}` (object) | only after the host's `hello`; never a string |

`hello` (host): `{app: "hwio-monitor", version, device: {name, serial, transport, firmware}, ops: bool}`.
`hello` (joiner): `{app: "hwio-monitor", version}`.
`op-request`: `{id, kind: "flash"|"erase"|"recover"|"reset", target: "app"|"lte", firmwareId?}`.
`op-result`: `{id, ok, error?: {code}}` (the joiner translates from `code`).
`end` (host, ADR-0354): `{code}`, the hwio::Error id of the reason (`cancelled` for End sharing); sent before the host
disconnects; it confirms delivery by a refused CONNECT to `/hwio-end` before the DISCONNECT (ADR-0358, at most 2 s).
A Monitor joiner ends its session ("The session ended."), accepted only after the host's `hello`.

Everything the host sends to the relay (`at` write/read lines, the `sync` transcript, audit marker texts) has secret
command arguments and secret output values redacted by the ADR-0186 rules (ADR-0356).

Only one operation runs at a time. Requests during an operation, a consent dialog or a join approval are denied with
`busy`. The host validates every field (enum values, firmware ID format, target allowed by the device profile) before
it shows the dialog. The firmware is downloaded **by the host** from the catalog (ADR-0037, checksums unchanged).

### Threading

Each `RemoteHost` / `RemoteConsoleSession` owns one relay worker thread, an owned `std::jthread` running a
`QEventLoop` (explicit stop + join; ADR-0357), for the socket and the JSON parsing. The worker and its `RelayClient`
exchange data only through two `BoundedQueue`s and payload-free wake-ups (ADR-0357, the ADR-0031 pattern). The
`RelayClient` talks to the `ConsoleSession` / operation runner on the owner's thread. All vendor calls stay on the
device session's worker (unchanged). No other shared mutable state.

### Limits (all bounded)

| What | Limit |
|---|---|
| Incoming event size | 64 KiB (larger → dropped, `remote.protocol_error` warning; repeated → session ends) |
| Remote `write` line | 512 bytes, max 20 lines/s (excess dropped, info line on the host) |
| Outgoing queue to relay | 1 MiB. On overflow the oldest `read` lines are dropped and a "lines dropped" line is sent |
| Relay thread hand-off (ADR-0357) | 4096 incoming events (full: warnings dropped, otherwise `remote.protocol_error`), 4096 outgoing sends (full: the oldest droppable send is dropped) |
| Delivery before disconnect (ADR-0358) | the refused `/hwio-end` CONNECT is awaited at most 2 s (`RelayLimits::closeTimeout`), then the socket closes anyway |
| `sync` snapshot | last 500 transcript entries / 256 KiB |
| Joiner transcript from `sync` | 500 entries / 256 KiB (larger → truncated, oldest first) |
| Join approval | 2 min, then the session ends |
| Operation confirmation | 60 s, then deny |
| Idle / lifetime | 30 min idle (warning at 28 min), 8 h max, enforced by Monitor (ADR-0328, ADR-0334) |
| Collision check | 2 s after connect, max 3 new codes |
| Reconnect | Engine.IO reconnect with backoff 1 s → 30 s, max 5 min, then end. The host's own reconnect causes a `newClient` that it ignores; the others see a join, which a Monitor host on their side doesn't have. After its own reconnect the host asks the join question again (ADR-0355). |

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D-0a | Protocol | Terminal-compatible + hardened relay / as is / own relay | ~~compatible, relay hardened first~~ → **strictly compatible, unchanged relay, 6-digit code** (2026-11-09) | ADR-0320 → ADR-0334 |
| D-0b | Socket.IO client | own over QtWebSockets / socket.io-client-cpp | own over QtWebSockets | ADR-0321 |
| D-0c | Remote scope | console / read-only / console + flash/reset | console + flash/erase/recover/reset | ADR-0322 |
| D-0d | Firmware source | catalog ID / + file upload | catalog ID only | ADR-0323 |
| D-0e | Consent | commands free + ops confirmed / once at start / everything | ops confirmed each; commands → see D-8 | ADR-0324 |
| D-0f | Transports, roles | J-Link + BLE, host + join | J-Link + BLE, host + join | ADR-0325 |
| D-0g | Release | 2.1 / 2.0.0 | 2.0.0-rc.4 (2026-11-11) | ADR-0326 → ADR-0337 |
| D-1 | Relay hardening spec | as proposed / adjust | dropped: relay stays unchanged | ADR-0327 → ADR-0334 |
| D-2 | Idle timeout / max lifetime | 30 min / 8 h; 15 min / 2 h; none / 24 h | 30 min idle, 8 h max | ADR-0328 |
| D-3 | Relay sees plaintext console | TLS only / E2E | TLS only (E2E impossible while compatible) | ADR-0329 |
| D-4 | Relay URL configurable | setting + `--relay` / env only / fixed | setting + `--relay`, https only | ADR-0330 |
| D-5 | CLI names | `hwio remote host/join` / `hwio support share/join` | `hwio remote host/join` | ADR-0331 |
| D-6 | MCP | nothing / read-only status | nothing | ADR-0332 |
| D-7 | Audit | console file / separate log / none | console file | ADR-0333 |
| D-8 | What the host confirms | joins + operations / operations only / every command | joins + operations | ADR-0335 |
| D-9 | Operation transport | tunnel in `at`/`write` / own events / no remote ops | tunnel (the relay drops other events; verified in its source) | ADR-0336 |

## Implementation steps

- [x] A1 `libs/remote`: Engine.IO / Socket.IO client + fake relay for tests (`QWebSocketServer` copying
      `server/index.js`: namespace regex, `newClient` to all, broadcast to others, other events dropped)
- [x] A2 `RelayClient`, `SessionCode`, error mapping
- [x] A3 `RemoteHost`: console mapping, join approval, sync snapshot, collision detection, origin marker, limits,
      idle/lifetime timers, audit lines
- [x] A4 `RemoteConsoleSession` + `Transport::Remote`
- [x] A5 `TunnelCodec`, operation protocol, `ConsentProvider`, `RemoteOperationClient`
- [x] B1 CLI `remote host` / `remote join` (+ `--json`), CLI e2e tests against the fake relay
- [x] C1 GUI: Share dialog + banner with the code + remote markers; join-approval dialog; Join dialog + remote tab
- [x] C2 GUI: operation dialog, remote Flash page (catalog only)
- [x] C3 Translations (all languages, 0 unfinished), user docs `docs/<lang>/remote-support.md`
- [ ] D1 Interop with the real relay and web Terminal: the `~/terminal` server run locally (docker compose) for
      repeatable checks, then terminal.hardwario.com. All four host/joiner combinations.

Automated verification passed on 2026-11-11 (9b34806): linux-debug build and ctest (1798 tests), linux-tidy zero
findings, all_qmllint, clang-format-21, check-lfs, check-includes, check_translations (0 unfinished), ASan remote and
tst_gui tests (92), full linux-tsan (1739 tests, no suppression for Qt queued calls or our code). D1 local part done on
a real CHESTER (CLI and GUI, both directions); the terminal.hardwario.com, web Terminal, second-machine and
macOS/Windows checks remain (checklist: [0044-d1-interop-checklist.md](0044-d1-interop-checklist.md)); the status
becomes Done after them.

## Implementation notes (2026-11-10)

Details fixed while implementing A1–C3, all confirmed by the user on 2026-11-10. Those that change security
behaviour became ADRs (ADR-0355, ADR-0356); ADR-0354 adds the tunnel `end`.

- **Host reconnect needs approval again (ADR-0355).** After the host's own relay reconnect, the host asks the join question again
  (when someone was allowed before). A participant who connected while the host was away was never announced to it
  (the relay sends `newClient` only to sockets that are connected at that moment). This follows ADR-0335's rule for
  reconnecting participants.
- **Test-only http relays.** `parseRelayUrl()` accepts `http://` for loopback hosts only in builds with
  `HWIO_TEST_OVERRIDES` (Debug and test presets, never shipped), for the in-process `FakeRelay` of the tests.
  Shipped builds accept https only (ADR-0330).
- **`SessionKey` unchanged.** `SessionKey` stays `ProbeKey | BleKey`: it names lockable hardware (probe locks, console
  file retention, the session registry). A remote tab is identified by its session code
  (`SessionIdentity{Transport::Remote, "<code>"}`) and is never restored.
- **Secrets toward the relay (ADR-0356).** The host sends commands with secret arguments and secret output values redacted, by the
  same ADR-0186 rules as the console file (`claim token: <redacted>`).
- **Remote marker = audit marker.** A remote command is shown in the host's console as the audit marker line
  "— Remote: … —" before its echo (the MCP marker style). The echo itself carries `LineSource::Remote` (JSON, MCP).
  The audit markers reach the console file as `@` lines through `ConsoleSession::recordRemoteEvent()` (J-Link worker,
  BLE thread `RecordNote`).
- **Web Terminal codes.** A joiner normalises the code as the web Terminal does (`Number(code)`): `012345` joins
  namespace `/12345`; `0` is not a session.
- **The joiner learns the end from the tunnel `end` (ADR-0354).** The relay does not announce disconnects, so a
  Monitor host sends `end` before it disconnects. With a web Terminal host the joiner's tab ends when its relay
  connection fails, and shows the idle notice after 30 min otherwise.
- **Limits instead of a fake clock.** Tests shrink `HostLimits` / `RelayLimits` (as libs/mcp does) instead of injecting
  a clock.
- **Operation mapping.** Remote erase = `EraseScope::Code` on `app` (UICR and PIB kept) and `EraseScope::All` on `lte`
  (as `chester lte erase`); reset = `ResetMode::Go`; flash = catalog firmware on `app` only.
- **No silent queries from remote tabs.** A remote tab never sends `info show` or any other silent shell query (no
  `DeviceInfoWatcher`, so the command tree is not learned either).

## Amendment 2026-11-12: Remote LTE update (ADR-0363)

Support can ask for an LTE firmware update without a catalog ID.

- **Protocol:** tunnel operation `lteUpdate` (no target, no firmware ID), advertised in the host's hello
  (`opKinds`, alongside `ops`); hosts without it answer `remote.operation_unsupported_peer`.
- **Host (GUI):** confirmation *"Support asks to update the LTE firmware to HARDWARIO LTE firmware 1.7.0. You will
  have to move the J-Link cable between the APP and LTE connectors."* (**Deny** default, 60 s). On Allow the tab opens
  the guided LTE update (Flash › Update LTE firmware…) with the recommended firmware preselected and starts it; the
  customer follows its steps. Phases / progress / result are sent to support; sharing stays active across the
  disconnect / reconnect; audit lines as for other operations.
- **Host (CLI):** `hwio remote host` runs the guided terminal flow of `hwio chester lte flash --guided` (prompts on the
  host's terminal); the exit outcome "LTE updated, application not restored" is reported as such.
- **Joiner (GUI):** Flash page › Remote operations: with **LTE modem core** selected, **Ask to update LTE firmware**
  (no ID field); erase / recover / reset unchanged. **Joiner (CLI):** `hwio remote join CODE --lte-update`.
- **Errors:** denied / timeout / busy as for other operations; the LTE update's own errors (e.g.
  `target.reset_during_update`, download / verification errors) are passed through with their codes.

Steps:

- [x] A6 `lteUpdate` in `TunnelCodec`, hello `opKinds`, `RemoteHost` consent text + operation runner hook,
      `RemoteOperationClient`; unit tests (codec, state machine, unsupported peer)
- [x] B2 CLI: `remote join --lte-update`; `remote host` runs the guided terminal flow; e2e tests with the fake J-Link
      (LTE scenario) and the fake relay; `docs/en/cli.md`
- [x] C4 GUI: Remote operations button, host confirmation, guided wizard started from the request with progress
      forwarded; Qt Quick tests; translations ×4; docs `remote-support.md` ×5; release notes rc.4

## Amendment 2026-11-12: Remote autocomplete (ADR-0364)

A remote tab had no autocomplete (learning starts only after a local attach's `info show`).

- **Host:** `RemoteHost::setCommandTree(tree)` — the GUI tab (ConsoleViewModel's learned tree) and the CLI host
  (its learner) call it whenever their tree changes; after the join is allowed the host sends tunnel `commands`
  (serialized `CommandTree`, bounded: 256 KiB, 4000 nodes, depth 8). A tunnel `learn {path}` from an allowed joiner
  (known path only, at most 2 per second, at most 200 per session) makes the host run its silent `<path> -h` query and
  send the updated tree. Not audited as remote commands (read-only help queries), but counted in the audit summary at
  the end.
- **Joiner:** `RemoteConsoleSession::commandTree()` + `commandTreeChanged`; validation as untrusted input; the remote
  tab's ConsoleViewModel uses it instead of the local learner (no `info show` / learner on a remote tab) and sends
  `learn` where the local learner would query children. The CLI interactive joiner uses it too.
- **web Terminal host:** no tree; suggestions from history only.

Steps:

- [x] A7 Tunnel `commands` / `learn`, limits and validation, `RemoteHost::setCommandTree`, joiner tree; unit tests
      (codec, malformed / oversize trees, rate limits, learn before allow refused)
- [ ] B3 CLI host feeds its tree; CLI interactive joiner completion; e2e test
- [ ] C5 GUI: host tab feeds the tree; remote tab completion and suggestions; Qt Quick test (completion in a remote
      tab after the host's tree arrives); docs `remote-support.md` ×5

## Tests and verification

- Unit: `SessionCode` (range, CSPRNG source, link parsing, invalid input); Engine.IO/Socket.IO framing (fuzz-style
  malformed packets, oversize, binary, unknown events); join approval state machine (own `newClient` ignored, writes
  dropped while pending, no sync before allow, End/timeout ends, reconnect needs approval again); collision detection;
  `TunnelCodec` (string vs object, unknown type/version, invalid fields); operation state machine (busy, timeout,
  deny, wrong id, firmware not in catalog); idle warning / extend / timeout and 8 h end (fake clock); relay URL
  validation and foreign-link confirmation; audit lines in the console file; transcript snapshot format
  byte-compatible with the web Terminal.
- Integration: the fake relay; host ↔ joiner end-to-end with the fake J-Link library and the fake BLE scenario; a
  recorded web Terminal host and joiner (event scripts taken from `console.tsx`); flash over remote with the fake
  J-Link + a test catalog.
- Qt Quick tests: share banner with the code, join-approval dialog (End session is the default), operation dialog
  (Deny is the default), remote tab read-only after end.
- ASan/UBSan + TSan runs of all `libs/remote` tests; clang-tidy clean; qmllint.
- Manual: D1 on Linux / macOS / Windows; Monitor ↔ Monitor flash on a real CHESTER (HIL); corporate proxy check
  (system proxy via `QNetworkProxyFactory`).
- Security review of the host-side join approval, tunnel handling and operation validation before release.

## Risks and open points

- **Guessable code (accepted by ADR-0334):** a stranger who guesses the code sees the live console output from the
  moment they connect, even before or without approval. Commands and operations need the customer's click. Mitigations:
  the share dialog warning, the approval text, short lifetime, one-click end, the audit lines.
- **Social engineering:** a stranger who joins just before support may get approved by mistake. The approval text says
  to allow only after support says they are joining. Each later join asks again.
- The protocol and transcript format belong to the web Terminal. A change in `~/terminal` breaks interop. A short
  interop note in the Terminal repository (with its owners' agreement) and the D1 check before each Monitor release
  reduce the risk.
- With a web Terminal host, Monitor can't detect the session end except by the connection dropping (a Monitor host
  sends `end`, ADR-0354). The idle notice
  covers that case.
- QtWebSockets is a new Qt module in the packages (LGPL, dynamic). It must be added to deployment (0015) on all OSes.
