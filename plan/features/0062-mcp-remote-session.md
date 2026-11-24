# 0062 — AI clients (MCP) on a joined remote support session

- **Status:** Approved (2026-11-24, Matej Strnad: D3 as option (b) in his own words, then "Approve the rest" — every other decision as recommended)
- **Milestone:** 2.0.0-rc.5 (phase A and phase B; fallback in D15)
- **Author:** Claude, for Matej Strnad
- **Related ADRs:** ADR-0433 (AI clients may use a joined remote tab; supersedes ADR-0332, amends plan
  0059 D-22), ADR-0434 (the remote tab in MCP: id and tools), ADR-0435 (no protocol change: AI actions are
  support's actions for the customer), ADR-0436 (support's permission), ADR-0437 (secrets at the MCP boundary of a remote tab), ADR-0438
  (`send_command`, limits, cancel and questions over a remote link). Existing: plan 0044 and ADR-0335, ADR-0336,
  ADR-0354, ADR-0355, ADR-0363, ADR-0368; plan 0059 and ADR-0414, ADR-0416, ADR-0417; plan 0061 and ADR-0429,
  ADR-0431, ADR-0432; MCP: ADR-0098, ADR-0104, ADR-0105, ADR-0241, ADR-0260, ADR-0374, ADR-0377, ADR-0390.
- **Depends on:** plan 0059 steps A0–A4 (committed on `strnad/plan-0059-remote-new-devices`), plan 0061 (in
  progress, not committed), plan 0060 step A4 (`device::ConsoleRedaction`), plans 0050 / 0054 / 0055 (pull requests
  #14, #15: consent API and English marker texts).

## Context

Support joins a customer's session (plan 0044) in a **remote tab**. An AI client connected to support's Monitor
over MCP cannot see that tab: ADR-0332 gave MCP no remote surface, and plan 0059 D-22 confirmed it on 2026-11-24.
The user now wants an AI client on support's machine to work with the customer's device in 2.0.0-rc.5, in the
widest scope: see the remote tab, read its console, send shell commands, and run the remote operations the joiner
already has.

### Verified facts (code: `ec55069` = origin/main `0c8638e` + plan 0059 A0–A4)

| # | Fact | Where |
|---|---|---|
| F1 | MCP lists a tab only when it has a J-Link serial or a Bluetooth id; a remote tab has neither. `tabFor()` and `open()` accept only `jlink:` / `ble:` | `app/gui/src/mcp/gui_device_provider.cpp:166-177, 194, 263-272` |
| F2 | MCP operations (`flash`, `flash_lte`, `erase`, `recover`, `reset`, memory tools) run on `DeviceRef.jlink` (`DeviceSession`). A remote tab has none; `ensureSession()` refuses | `libs/mcp_tools/src/tool_support.cpp:830`, `device_tab_view_model.cpp:439-441` |
| F3 | The joiner's operations go through one `RemoteOperationClient` per tab (owned by `RemoteJoinViewModel`): `request(kind, target, firmwareId, prepared)`, one at a time, 90 s until accepted or denied, 5 min between progress messages | `libs/remote/include/hwio/remote/remote_operation_client.h` |
| F4 | The joiner has on CHESTER: flash (catalog ID, `app`), erase, recover, reset (`app` / `lte`), `lteUpdate` | plan 0044, ADR-0322, ADR-0363 |
| F5 | There is no cancel message on the wire. `RemoteOperationClient::abandon()` only stops listening; the customer's Monitor finishes the operation | `tunnel.h` (`JoinerMessage`), `remote_operation_client.h:74` |
| F6 | The relay broadcasts; the host cannot tell which participant sent a `write`. Anything a joiner says about itself is a statement, not a proof | ADR-0414 context, ADR-0368 |
| F7 | A host that cannot decode a joiner's tunnel object shows a warning and continues; unknown fields of known messages are ignored | `remote_host.cpp:480-484`, ADR-0414 |
| F8 | `send_command` needs no permission today, for any device. Permission categories are Flash, Erase, MemoryWrite, Uicr, Connect; a client's "always allow" covers every device | `libs/mcp/.../consent.h:29`, `gui_consent_broker.cpp:94` |
| F9 | `send_command` ends at the shell prompt only for Bluetooth or a profile with a prompt; a remote session never reports a prompt, so it ends after 300 ms without output, or at the timeout (default 5 s). It waits for the command's echo | `libs/device/src/command_capture.cpp:36-44`, ADR-0104 |
| F10 | The host drops a remote line without any answer when the participant is not allowed yet, when it is longer than 512 bytes or has control characters, and above 20 lines per second. A refused line (ADR-0416) is answered with `line-refused` | `remote_host.cpp:436-476` |
| F11 | **MCP hides no secrets today.** `read_terminal`, `read_log` and `send_command` return console lines as the screen shows them, also on a local tab. No ADR says otherwise (ADR-0186: files hide, the screen shows) | no redaction in `libs/mcp`, `libs/mcp_tools`, `app/gui/src/mcp` |
| F12 | Plan 0061's joiner-side hiding (ADR-0432) applies to hosts **without a hello** (web Terminal, mobile app). A 2.0.0-rc.4 Monitor host sends a hello and still passes secrets in command form (`lrw config appkey …`) | plan 0061 A1.8 fact 4 |
| F13 | The session code is the session's only secret. It is in the remote tab's identity (`SessionIdentity{Remote, "<code>"}`) and in the title of a tab whose host sent no hello ("Session 482 913 — …") | `remote_console_session.h:126-141` |
| F14 | `hwio mcp` builds a J-Link-only provider; the CLI's MCP server cannot hold a remote session | `app/cli/src/mcp_command.cpp:292-305` |
| F15 | The mobile app as host has no join approval and no operation tunnel; every participant who knows the code can send lines | plan 0061 A1.3 |

The task description says that secrets "are never returned over MCP". F11 shows this is not true for local tabs
today; it is a rule this plan introduces for remote tabs (D10). Local tabs are an open point (risk 6).

## Goals / Non-goals

Goals:

- An AI client on support's machine can list a joined remote tab, read its console, send shell commands to the
  customer's device and request the remote operations decided in D8.
- For the customer nothing changes (D3, decided 2026-11-24): commands and operations of support's AI client
  appear as support's own, with the same markers, questions and audit lines. No protocol change.
- Support decides on their own machine before any customer data goes to an AI client (D6).
- Secrets never reach an AI client through a remote tab (D10). The session code never reaches an AI client (D2).

Non-goals:

- An AI client creating, joining, leaving or ending a session, sharing a device, or reading a session code or
  link. This half of ADR-0332 stays (ADR-0433).
- Any change to the relay, the web Terminal or the mobile app.
- Remote memory, register, PIB / UICR or probe tools; firmware from a file.
- MCP over the relay (the AI client talks only to support's own Monitor).
- Changing what MCP returns for local tabs.

## User experience

### Support (joiner)

1. **Settings › AI › "AI clients may use remote support sessions"**, off by default (D1). Off: as today, a remote
   tab does not exist for AI clients. The text under the switch: "The customer's console output is then sent to
   your AI client. The customer is asked before the AI client can send commands."
2. With the switch on, a joined remote tab is listed by `list_devices` as `remote:<n>` (D2).
3. The first time a client touches a remote tab (reading included), support gets the prompt (D6):

   > **Claude Code wants to work with a customer's device**
   > Session: CHESTER-M 2159018247 — remote
   > It will read the customer's console and may send commands to the customer's device. Secret values are hidden
   > from it. The customer sees its commands as yours.
   > [Allow for this session] [Deny] — Deny is the default; 120 s; no "always allow".

   Allow covers reading and commands of this client in this tab until the tab leaves the session, the session
   ends or the client goes away. Before Allow the client gets only the `list_devices` / `devices` entry with the
   neutral name "Remote support session" (no device name, serial, profile or firmware); every other tool,
   resource (`device-status`, `device-log`, `device-terminal`), resource update and the `debug_device` prompt
   answers `mcp.consent_denied` for that device without content.
4. Every operation (D8) shows the existing permission prompt each time, with the row "Device: the customer's
   device (remote session)". It cannot be remembered and "always allow" does not cover it.
5. The remote tab's banner shows "AI client active: Claude Code" with **Stop AI** (ends the grant; running calls
   end with `cancelled`). The console shows the usual markers ("— AI (Claude Code): send_command —"); they stay
   in support's console and are not sent to the customer.
6. A remote operation started by the AI client uses the Flash page's existing remote-operation state (waiting for
   the customer, progress, result), with "AI:" in the busy text and a **Stop waiting** button (D12).

### Customer (host)

No change (D3). A command of support's AI client appears as "— Remote: command `config show` —" before its echo;
an operation request shows the existing question "Support asks to flash firmware …" (Deny default, 60 s); the
audit lines in the console file are the existing ones. The customer's join approval (ADR-0335) and the question
before every operation (ADR-0324) are the customer's control, as for a person. The customer's software does not
show that an AI client typed a command; support is responsible for what their AI client does, as for what they
type themselves. `docs/*/remote-support.md` says that support may use an AI assistant.

### Error messages (new; all via `hwio::Error`, translated; none is sent over the wire)

| Id | What / why / what to do |
|---|---|
| `remote.command_not_delivered` | The customer's app did not pass the command to the device: you are not allowed yet, too many commands per second, or the session is busy. Wait and try again. |
| `remote.waiting_for_customer` | The customer has not allowed you in the session yet. |

Existing ids reused: `remote.command_refused` (ADR-0416), `remote.host_ended`, `remote.operation_denied`,
`remote.operation_unsupported_peer`, `remote.firmware_not_in_catalog`, `remote.target_unsupported`,
`mcp.tool_not_allowed`, `mcp.capability_missing`, `mcp.consent_denied`, `mcp.consent_timeout`,
`mcp.too_many_requests`, `usage.invalid_argument`, `internal.busy`.

## CLI

- `hwio remote host`: unchanged.
- `hwio remote join`: unchanged (a person types).
- `hwio mcp`: no remote session in rc.5 (D14). The backend parts (tools, protocol) are in `libs` and shared.

## Design

### Modules

- **`libs/remote`**: no protocol change. `RemoteConsoleSession` gets what the remote `send_command` needs:
  `lineRefused(code)` signal for the host's `line-refused` (today only a marker line), and `aiCommandActive`
  bookkeeping is in the GUI. `RemoteOperationClient` unchanged.
- **`libs/mcp_tools`**
  - `DeviceEntry`: `kind` `remote`, `remote` block (below). `DeviceRef` gets
    `QPointer<IRemoteDevice> remote` (new interface in `device_provider.h`):

    ```cpp
    class IRemoteDevice : public QObject {
    public:
        [[nodiscard]] virtual RemoteInfo info() const = 0;             // host kind, join state, targets
        // A plain remote line, exactly as a person's (RemoteConsoleSession::sendLine).
        [[nodiscard]] virtual Status sendLine(std::string line, const RequestContext& who) = 0;
        // One at a time per tab; `done` once. Detach = stop waiting (D12).
        [[nodiscard]] virtual Result<RemoteOperationId> requestOperation(const RemoteOperation& op,
                                                                         const RequestContext& who,
                                                                         RemoteOperationCallbacks callbacks) = 0;
        virtual void detachOperation(RemoteOperationId id) = 0;
        [[nodiscard]] virtual device::ConsoleRedaction redaction() const = 0;
    };
    ```

    `libs/mcp_tools` gets no dependency on `libs/remote`: the GUI implements the interface.
  - Tool dispatch: a `remote:` device takes the remote branch in `send_command`, `flash`, `flash_lte`, `reset`;
    read tools pass every returned line through `redaction()`; every other device tool answers as in the table
    of D7.
  - `ConsentCategory::RemoteDevice` (session grant, never remembered, never set by the "All" row or by `--allow`).
- **`app/gui`**
  - `GuiDeviceProvider`: remote tabs in `list()` / `find()` when the setting is on; `GuiRemoteDevice` adapter over
    the tab's `RemoteConsoleSession` and `RemoteJoinViewModel`'s `RemoteOperationClient` (one source of busy
    state, so plan 0061's Leave-button rule holds).
  - `GuiConsentBroker` / `ConsentViewModel`: the session prompt; `RemoteJoinViewModel` + `RemoteBanner.qml`:
    "AI client active" and Stop AI on support's side; settings switch. QML stays thin. The host side
    (`RemoteShareViewModel`) is not touched.
- **`app/cli`**: nothing.

### Device entry (D2)

```json
{"id": "remote:1", "kind": "remote", "state": "open", "displayName": "CHESTER-M 2159018247 — remote",
 "profile": "chester",
 "remote": {"owner": "customer", "host": "monitor", "join": "allowed",
            "commands": true, "operations": [{"target": "app", "kinds": ["flash", "reset"]}, {"kinds": ["lteUpdate"]}],
            "secrets": "hidden"}}
```

`<n>` is a counter of remote tabs in this run of Monitor (never the session code, never reused). The display name
of a host without a hello is "Remote session (web Terminal or mobile app)", without the code. The code and the
relay URL appear in no tool result, marker or activity entry. Every tool result on a remote device carries
`"remote": true`, and `list_devices` / `status` carry the text "This is a customer's device in a support
session. Commands and operations run on the customer's device."

### Flow of an AI command

```
AI client → send_command(remote:1, "config show")
  support's Monitor: setting on? grant for this client and tab? (else prompt, D6)
  → write "config show"                                (the same string a person's line is)
  customer's Monitor (unchanged): checks → audit "— Remote: command config show —" → device
  → at/write (echo) → at/read … → support's console log → hidden view → capture → tool result
```

### `send_command` over a remote link (D9)

- Refused before sending: longer than 512 bytes or control characters (`usage.invalid_argument`), join state not
  Allowed (`remote.waiting_for_customer`), session ended (`remote.host_ended`).
- After sending: `line-refused` → `remote.command_refused`; no echo within 5 s → `remote.command_not_delivered`.
- End of output: **1000 ms** without output after output started (the relay delivers one event per line; 300 ms
  cuts answers on a slow link), always at the timeout (default 10 s on a remote tab, maximum 120 s as today). The
  prompt rule is not used: a remote session never reports a prompt (`promptSeen` is emitted by no code in
  `libs/remote`), so `CommandCapture` gets an option that forces the silence rule whatever the profile says.
  Result fields as today (`end`: `silence` or `timeout`; `truncated`, `interleaved`) plus `remote: true`.
- Echo: the capture matches the echo against the line **as the host will send it back**: trimmed, and with the
  secret argument replaced as the host's redaction does (a Monitor host echoes `… <redacted>`, the mobile app
  trims). Since secret-setting commands are refused (below), the match is the trimmed line.
- One AI command at a time per remote tab (`internal.busy` for a second one), at most 30 per minute per tab
  (`mcp.too_many_requests`). The host's own limits stay.

### Operations (D8, D12, D13)

| Tool on `remote:<n>` | Maps to | Notes |
|---|---|---|
| `flash` with `catalog_id`, target `app` | `op-request flash`, `firmwareId` | a file path, URL or other target: `usage.invalid_argument` / `remote.target_unsupported`; `prepared` (ADR-0418) accepted as `prepared_id` when the host lists it |
| `flash_lte` | `op-request lteUpdate` | no file or version argument on a remote device (`usage.invalid_argument`); the host uses its recommended firmware (ADR-0363) |
| `reset` | `op-request reset` | targets the host lists |

Requests are the same messages a person's are. Before sending: support's prompt (each time), `RemoteConsoleSession::hostOffers()`.
Progress is forwarded as MCP progress; the result is the host's `op-result` (error id from `detail.code`).

Cancel (`notifications/cancelled`, the busy page's button, `McpController::cancelOperation`): the tool call ends
with `cancelled` and the text "The operation continues on the customer's device; its result appears in the
remote tab." The tab keeps following the operation (no `abandon()`), stays busy until the result, and the next
request gets `internal.busy`.

Questions of a remote LTE update (ADR-0368): the request carries `questions: true` as a person's request does;
the question is shown to the **person** in the remote tab as today. The AI client gets a progress message
"waiting for support's answer" and has no tool to answer. When nobody answers, the host's existing 60 s rule
applies (no answer is no) and the tool call ends with the host's result.

### Secrets (D10)

- An AI client works on a **hidden view** of a remote tab's console: `device::ConsoleRedaction` (plan 0060 A4)
  of the profile named in the host's hello (CHESTER without one) is applied to each line **before** anything of
  MCP sees it: before pattern and regex matching in `read_log` / `read_terminal`, before `wait_for_state`
  conditions, before the capture of `send_command`, and in the `device-log` / `device-terminal` resources. A
  pattern can therefore never be used to test for a hidden value. It runs whoever the host is, in addition to
  plan 0061's hiding (F12).
- The same view replaces the session code (grouped `482 913` and plain `482913`) with `<session>` wherever it
  appears in a line (markers of support's own Monitor, a host's audit line, a customer who types it).
- A `send_command` whose line sets a sensitive value (the profile's sensitive command with an argument) is
  refused with `remote.command_refused` on support's side and never sent: an AI client does not write a
  customer's keys.
- Activity entries and markers store the redacted command.

### Threading, limits

No new thread. Everything runs on the GUI thread (owner of the tab's sessions); the relay worker is unchanged.
Bounds: AI commands 1 at a time and 30 per minute per
tab; prompts through the existing queue (8 waiting); `<n>` is a 32-bit counter.

## Decisions requiring user confirmation

| # | Question | Options (recommended in bold) | Decision (by user) | ADR |
|---|---|---|---|---|
| D1 | Reverse ADR-0332 and plan 0059 D-22; master switch | **(a) Yes, behind a setting "AI clients may use remote support sessions", off by default**; (b) yes, always on; (c) on by default with the switch | as recommended (2026-11-24) | ADR-0433 |
| D2 | How MCP identifies a remote tab | **(a) `remote:<n>`, a counter per run; `kind: "remote"`, a `remote` block with `owner: "customer"`; the session code never appears**; (b) `remote:<code>` (leaks the code to the AI client and its provider); (c) the device's serial from the hello (missing for hosts without a hello, and can collide with a local probe's device) | as recommended (2026-11-24) | ADR-0434 |
| D3 | Does the customer's Monitor learn that an AI client acts? | (a) cap `ai`, marked AI commands and operations; **(b) no: AI commands and operations look like support's** | **(b)** — Matej Strnad, 2026-11-24: "no, to the client it shows as classic operations from the operator" | ADR-0435 |
| D4 | Separate permission of the customer for AI | — | dropped by D3 (b): the customer's join approval and the question before every operation stay as they are | ADR-0435 |
| D5 | Hosts without a join approval (web Terminal, mobile app: everyone who knows the code can send lines, F15) | **(a) Same as a Monitor host: support's session permission covers the AI client's commands**; (b) support confirms every single command there; (c) reading only there | as recommended (2026-11-24) | ADR-0436 |
| D6 | Support's own permission | **(a) New category "customer's device": one prompt per client and remote tab, covers reading and commands until the session ends, Stop AI in the banner; never "always allow", not in the "All" row; operations prompt every time. Before it, the client sees only a nameless "Remote support session" entry; tools, resources and prompts give nothing**; (b) a prompt for every command; (c) none beyond the setting | as recommended (2026-11-24) | ADR-0436 |
| D7 | Tools on a remote tab | **(a) `list_devices`, `status`, `read_terminal`, `read_log`, `wait_for_state`, `wait_for_connection`, `send_command`, and the operations of D8. `connect` / `disconnect` / `reconnect` → `mcp.tool_not_allowed` (join and leave stay human); probe, memory, register, PIB / UICR, halt / go, `write_*` → `mcp.capability_missing`** | as recommended (2026-11-24) | ADR-0434 |
| D8 | Which remote operations an AI client may request | **(a) Catalog flash, LTE update, reset. Not erase and not recover** (they are not in the request "flash, LTE update", and they leave the customer's device without firmware); (b) flash and LTE update only; (c) everything the joiner has, erase and recover included | as recommended (2026-11-24) | ADR-0434 |
| D9 | `send_command` end rule and limits on a remote tab | **(a) As designed: echo within 5 s or `remote.command_not_delivered`; 1000 ms silence (no prompt rule on a remote tab); default timeout 10 s; 512 bytes; one at a time; 30 per minute per tab**; (b) keep the local values (300 ms, 5 s) | as recommended (2026-11-24) | ADR-0438 |
| D10 | Secrets toward the AI client | **(a) The AI client works on a hidden view of every remote tab, whoever hosts: redaction (profile lists) before any pattern match or wait condition; the session code is replaced too; commands that set a secret are refused; result says `secrets: "hidden"`**; (b) rely on plan 0061 (misses an rc.4 Monitor host, F12); (c) refuse AI access unless the host is a Monitor with this plan | as recommended (2026-11-24) | ADR-0437 |
| D11 | Markers and audit | **(a) Support: the existing AI markers and activity list, in support's Monitor only. Customer: the existing remote markers and audit lines, unchanged (follows from D3)** | as recommended (2026-11-24) | ADR-0435 |
| D12 | Cancel of a remote operation | **(a) Stop waiting: the call ends `cancelled` with an honest text, the tab follows the operation to its result**; (b) a new cap-gated `op-cancel` that the host honours where the operation can be cancelled (larger: host-side cancel points, new tests; candidate for rc.6) | as recommended (2026-11-24) | ADR-0438 |
| D13 | Questions of a remote LTE update started by an AI client | **(a) The support person answers in the remote tab as today; never the AI client**; (b) the customer answers (ADR-0366 behaviour, `questions` not sent); (c) a tool for the AI client | as recommended (2026-11-24) | ADR-0438 |
| D14 | MCP in the CLI (`hwio mcp` holding a joined session) | **(a) Not in rc.5: a CLI `hwio mcp --join` needs its own consent design (no prompt on stdio) and goes to a later plan. No CLI change in this plan**; (b) in rc.5 | as recommended (2026-11-24) | ADR-0433 |
| D15 | Branch base, merge order, release | **(a) Base `strnad/impl-0062-mcp-remote` on a merge of plan 0059 A0–A4 + plan 0061 + plan 0060 + pull requests #14 and #15; merge to main after them. Phase A (see steps) and phase B both in rc.5; if phase B's verification or a dependency is not ready when rc.5 is cut, phase A ships alone and operations are refused with `mcp.capability_missing` until rc.6**; (b) rc.5 waits for everything; (c) all in rc.6 | as recommended (2026-11-24) | — |
| D17 | Reading by an AI client and the customer | — | dropped by D3 (b): reading is support's decision (D6) | — |
| D16 | Amendment of plan 0059 D-22 and of plan 0044's non-goal | **(a) A dated note in both plans pointing to ADR-0433; ADR-0332 gets "Superseded by ADR-0433"** | as recommended (2026-11-24) | ADR-0433 |

## Implementation steps

Phase A — tab, reading, commands:

- [ ] A1 `RemoteConsoleSession`: `lineRefused(code)` signal; `CommandCapture` option that forces the silence rule;
      unit tests. No change of `tunnel` and `RemoteHost`; a test proves that an AI command and a person's command
      are the same bytes on the relay.
- [ ] A4 New error ids with texts and troubleshooting anchors.
- [ ] A5 `libs/mcp_tools`: `IRemoteDevice`, `DeviceEntry` remote block, `ConsentCategory::RemoteDevice`, tool
      matrix of D7, remote `send_command` (D9), redaction of every read path (D10); tool tests with a fake
      `IRemoteDevice`.
- [ ] A6 GUI: setting; `GuiDeviceProvider` remote entries; `GuiRemoteDevice`; support's session prompt; banner
      "AI client active" + Stop AI; Qt Quick tests.
- [ ] A8 Translations (cs, de, pl, sk; 0 unfinished).

Phase B — operations:

- [ ] B2 `flash` (catalog, prepared), `flash_lte`, `reset` on a remote device; progress; stop waiting (D12);
      questions (D13); tool tests and an end-to-end test host ↔ joiner with the fake J-Link and a test catalog.
- [ ] B3 GUI: busy text, Stop waiting, permission prompt rows; Qt Quick tests.

Phase C — documents:

- [ ] C1 `docs/<lang>/mcp.md`, `remote-support.md`, `privacy.md` in en, cs, de, pl, sk; release notes
      `packaging/release-notes/v2.0.0-rc.5.md`; interop checklist rows (AI client with: rc.5 Monitor host,
      rc.4 Monitor host, web Terminal host, mobile app host).
- [ ] C2 ADR-0332 marked superseded; notes in plans 0044 and 0059 (D16); `plan/0004-mcp.md` tool table.

## Implementation notes (A4, A5; confirmed by the user 2026-11-24, "Accept both")

- **Where support's permission is kept (changes a consequence of ADR-0436).** The session grant is kept by the
  remote device object (`IRemoteDevice::aiAllowed` / `allowAi` / `aiRevoked`), not by the consent broker: resources,
  the resource list and resource updates are answered synchronously and cannot ask the broker. The broker only
  shows the prompt; the tools accept only a prompted answer for `RemoteDevice` and for remote operations.
- **Local tool schemas changed** so that the same tools serve a remote device: `flash_lte` no longer requires
  `path` in its schema (a local call without it is the tool error `usage.invalid_argument`, not -32602);
  `send_command` has no schema default for `timeout` (5 s local, 10 s remote, applied in code); `flash` has
  `prepared_id` (refused on a local device); `connect` accepts a `remote:` id to answer `mcp.tool_not_allowed`.
- A tool the matrix refuses on a remote device answers at once, without prompting support. `wait_for_state` with
  a polling command counts toward the 30 commands per minute.
- The hidden view replaces the session code written as `482913` and `482 913`; other spellings are not
  recognised.
- New error ids: `remote.command_not_delivered` (category timeout, baseline `timeout`),
  `remote.waiting_for_customer` (category cancelled, baseline `remote.join_not_allowed`); neither is sent.

## Tests and verification

- Unit (GoogleTest): `lineRefused`; the forced silence rule;
  MCP tools with a fake `IRemoteDevice` (every tool of D7 and its error, `send_command` end rules and each error
  of D9, redaction of each read path with made-up key values, a pattern and a wait condition that try to match a
  hidden value and must not, secret-setting command refused, a log fixture with the session code in a marker and
  in a device line and the code absent from every result, nothing but the nameless entry before support's Allow
  (tools, resources, resource updates, prompt), operations and stop waiting).
- Integration: `FakeRelay` host ↔ joiner with the fake J-Link library: AI command from tool call to device and
  back; denied by the customer; flash from a test catalog requested by a tool call.
- Qt Quick: setting off hides the tab from `list_devices`; support's prompt (Deny default, no "always allow");
  Stop AI ends the grant and running calls.
- No test uses hardware, Bluetooth, the production relay or the network. No test is added to a sanitizer
  exclusion list without the user's approval.
- Full verification, one preset at a time (`nice -n 19 ionice -c 3`, `--parallel 5`): `linux-debug` build and
  ctest; `linux-asan`; `linux-tsan`; `linux-tidy` with zero findings; `all_qmllint`; `check_translations` with 0
  unfinished in cs, de, pl, sk; clang-format-21; `tools/check-includes.sh`; `tools/check-lfs.sh`.
- Manual (not automated, reported as not verified until done): two Monitors through the production relay with a
  real AI client (commands, Stop AI, flash, LTE update with a question); the mobile app as host;
  an rc.4 Monitor as host.
- Security review of support's permission, the redaction paths and the absence of the session code in MCP
  output before release.

## Risks and open points

1. **The customer is not told by the software that an AI client acts (D3).** The customer allowed "support";
   support answers for their AI client. The customer's controls are unchanged: join approval, a question before
   every operation, End sharing. Whether support tells the customer is a matter of support's process; the docs
   mention it.
2. **Console text leaves support's company.** What an AI client reads goes to its provider. The setting's text,
   the prompt and `privacy.md` say so; redaction is name-based (profile lists) and does not know secrets of other
   firmware.
3. **Dependencies are moving.** Plan 0061 is not committed; plan 0060's `ConsoleRedaction` and the consent API of
   pull request #15 are on other branches. Phase A cannot be verified before they are merged into the base (D15).
4. **Hosts without a join approval (D5).** With the web Terminal or the mobile app as host, nothing on the
   customer's side stands between support's AI client and the device shell. Plan 0061's stream mode decides how
   `send_command` sees the echo and the output there; the end rule has to be tested against the mobile app
   script of plan 0061 A1.6.
5. **Prompt injection through the customer's console.** Device output is untrusted text an AI client reads; it
   could ask the client to run commands. Limits: support's prompts for operations, the customer's question before every operation, 30
   commands per minute, refused secret-setting commands. `docs/*/mcp.md` warns about it.
6. **Local tabs (F11):** MCP returns secrets of a local device as the screen shows them. Not changed here; a
   decision for a separate plan.
7. An AI client that runs many commands makes the customer's console busy; Stop AI on support's side and End sharing on the customer's are the answer.
