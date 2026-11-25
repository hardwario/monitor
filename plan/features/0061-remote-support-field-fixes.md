# 0061 — Remote support: fixes from the first real use (2026-11-24)

- **Status:** In progress (approved 2026-11-24, Matej Strnad: "Approve all" — D1 to D13 as recommended, including Amendment 1)
- **Milestone:** 2.0.0-rc.5 (items 1 and 2 and the secrets of amendment 1; D13 changed by the user 2026-11-24)
- **Author:** Claude, for Matej Strnad
- **Related ADRs:** proposed ADR-0429 (the connection button of a remote tab), ADR-0430 (remote session line
  diagnostics without text), ADR-0431 (the joiner reads stream pieces, amendment 1), ADR-0432 (secrets in both
  directions, amendment 1). Existing: plan 0044 and ADR-0336 (host hello), ADR-0354 (host ended), ADR-0364
  (command tree of the host), ADR-0183 (device text is sanitised), ADR-0186 (secrets in files and logs), ADR-0356
  (redaction toward the relay).
- **Depends on:** nothing. Plan 0059 (branch `strnad/plan-0059-remote-new-devices`) changes the same library
  (`libs/remote`); the item 1 change is in `app/gui` only and does not collide with it.

## Context

Matej Strnad used the remote support session (plan 0044) in a real environment on 2026-11-24 and reported:

1. **The Disconnect button does not work.**
2. **The console sometimes shows blank space instead of a command's result**: for example `system show` gives
   many blank lines and no text.

Code evidence is origin/main `0c8638e`.

### Item 1 — cause found

The device bar's connection button (`app/gui/qml/shell/DeviceBar.qml:162`, `objectName: "connectionAction"`) is
enabled by `ConnectionViewModel::actionEnabled()`:

```cpp
return !m_tab->isBusy() && !m_pairing && (m_serial || isBle());   // connection_view_model.cpp:330
```

A remote tab has no J-Link serial number and is not a Bluetooth tab, so the button is **always disabled** there,
whatever it shows ("Disconnect" while joined, "Connect" after the session ended). The same holds for the
`device.toggle` shortcut. Support can leave a running session only by closing the tab; the banner's "Leave" action
(`RemoteBanner.qml:114`) appears only in the states `idle` and `ended`, and it closes the tab too.

`DeviceTabViewModel::disconnectDevice()` itself would work for a remote tab (`releaseSession()` handles
`m_remoteSession`), and `connectDevice()` → `openSession()` → `openRemoteSession()` would join again with the
tab's code. Only the enable rule and the wording are missing. No test covers the button in a remote tab.

This assumes the report is about support's side (the joined tab). The customer's side ends with "End sharing"
(`RemoteShareViewModel::endSharing()`), where no fault was found; to be confirmed with the reporter (open point 1).

### Item 2 — cause not found yet

Reading the path of a line showed nothing that makes a row empty:

- Host: `RemoteHost::onLines()` (`remote_host.cpp:802`) forwards every non-marker line as `at` event `read` /
  `write` with the text; `redactedOutput()` replaces a secret's value with `<redacted>`, never with nothing.
  `read` events are sent as droppable: a dropped line would be missing, not blank.
- Joiner: `RemoteConsoleSession::onAt()` (`remote_console_session.cpp:310`) ignores a `data` that is not a string
  and otherwise appends one line per event (`makeLine()`: `sanitizeDeviceText()`, level and channel from the text).
- `onSync()` clears the console log and appends the host's transcript, then emits `linesReceived` and
  `transcriptReplaced`.

Hypotheses, to be tested in this order (none proven):

- **H1 — drawing.** The lines have text, the console view draws empty rows. A remote tab appends one row per
  event (a local tab appends batches), so a long answer is many single-row inserts in a row while the view
  follows the end. The local console of the customer shows the same lines from batches and would be correct.
- **H2 — replaced log.** A `sync` (sent when the customer allows support, and again after a reconnect of an
  unstable link) clears the log while rows of the old log are still in the view, or live lines arrive between the
  clear and the reload ("The live output may already appear" before the sync).
- **H3 — the text really is empty.** The peer is not Monitor (web Terminal, another host) and sends the output in
  a form `makeLine()` reduces to nothing, or `sanitizeDeviceText()` removes the whole line of this device
  (`system show` is not a CHESTER command; device and firmware unknown, open point 2).
- **H4 — not remote at all.** The same output is blank in the customer's local console; then the cause is in the
  line assembler or the console view and the fix belongs to the console, not to `libs/remote`.

A saved console output of the affected side separates H1/H2 (text present in the file) from H3 (empty lines in
the file).

### Item 2 — evidence of 2026-11-24 (saved console output of support's side)

The reporter sent the console output saved from the joined tab (92 lines; not stored in the repository: it
contains a LoRaWAN application key). What it shows:

- The prompt is `bt_nus:~$`: the customer's device is reached through the **Zephyr shell over Bluetooth (NUS)**,
  not through J-Link RTT. What hosts the session (a Monitor Bluetooth tab, the mobile app, the web Terminal) is
  not known yet (open point 2).
- **29 lines are really empty in the file.** So the text is lost before the console log of the joiner, not on the
  screen: **H1 is ruled out**, H3 or H4 remain.
- Three patterns: (1) in the first part, taken from the session history, a command is followed only by the prompt
  line, with no answer lines at all; (2) later an `info show` is followed by 1 to 8 empty lines; (3) the lines of
  a `config show` answer that do have text appear **twice** each, mixed with empty lines, and `bt_nus:~$ ` is
  sometimes repeated up to four times on one line.
- A `lrw config appkey …` line arrived **with its value**: the host did not hide it. If the host is Monitor, its
  list of secret keys does not cover this device's commands; if the host is another application, support's
  Monitor shows and saves whatever it receives. Either way this is a third item for this plan (D7).

Working explanation (not proven): the shell over NUS repaints its line with escape sequences and carriage returns
and delivers the answer in notification-sized pieces, and the side that turns those pieces into lines produces
empty lines and repeats. Where that happens depends on the host application, which decides between H3 and H4.

## Goals / Non-goals

Goals:

- Support can leave a remote session with the connection button and its shortcut, and join again.
- The cause of the blank lines is found with a reproduction, fixed, and covered by a test that fails before the
  fix.
- A diagnostics report of either side shows where a line lost its text, without containing any console text.

Non-goals:

- No change of the session protocol (version 1) and of the customer's "End sharing".
- No fix for item 2 is written before its cause is proven; the fix is added to this plan as an amendment and
  approved by the user first.

## User experience

**Connection button in a remote tab** (D1, D2):

| Join state | Button | Action |
|---|---|---|
| connecting, waiting, joined, idle | **Leave session** (normal kind, enabled) | Leaves the session; the tab stays open with the console history. Marker line: "— you left the session —". |
| ended (by the customer, by an error, or after Leave) | **Join again** (primary kind, enabled) | Joins the same code again; the customer has to allow support again. If the code is no longer valid, the existing "session not found" error is shown. |

- The shortcut `device.toggle` does the same; its tooltip shows the button's text.
- No confirmation before leaving (D3): nothing is lost, the history stays and the session can be joined again.
- While a remote operation that support started is running (flash, LTE update), the button is disabled with the
  tooltip "Wait until the running operation ends." (D3). The customer's Monitor finishes the operation in any
  case; support only must not lose sight of its result by accident.
- The banner text after Leave: "You left the session. The console stays here so that you can save it." with the
  existing "Leave" action renamed to **Close tab** in the states `idle` and `ended` (D4), because "Leave" now
  means leaving the session, not closing the tab.
- New strings are translated to cs, de, pl, sk.

**Blank lines:** no UX change is planned; the console must show the text.

## CLI

None for item 1 (`hwio remote join` ends with Ctrl+C). Item 2: none planned; if the cause is in shared code
(H3, H4) the CLI is fixed by the same change and gets an end-to-end test.

## Design

### Item 1

- `ConnectionViewModel::actionEnabled()`: a remote tab enables the button unless the tab is busy or a remote
  operation of this joiner is running (`RemoteJoinViewModel::operationRunning`, or the tab's busy state if it
  already covers it: the implementing agent checks and uses one source).
- `ConnectionViewModel::action()`: new values `"leave"` and `"join"` for a remote tab (from the join state, not
  from `ConnectionState`: a remote tab whose customer device is disconnected is still a joined session).
  `DeviceBar.qml` maps them to the texts above; no logic in QML.
- `ConnectionViewModel::triggerAction()` / `toggleConnection()`: `"leave"` → `DeviceTabViewModel::disconnectDevice()`
  (marker text "— you left the session —" for a remote tab), `"join"` → `connectDevice()`.
- `RemoteJoinViewModel`: a state for "left by the user" so that the banner shows the text above instead of
  "The session ended."; `RemoteBanner.qml` action text "Close tab".
- `RemoteConsoleSession::close()` already tells the relay that the participant left; verify with a test that the
  host's banner and audit line ("Support left the session.") appear, and add them if they do not.
- Threading, limits, error codes: unchanged.

### Item 2 — phase I (investigation, no behaviour change)

- **I1 Reproduction.** With the reporter's answers (open points 1–3): the loopback relay of
  `libs/remote/tests`, a host with the fake J-Link library scripted with the real `system show` answer (taken
  from a saved console output), a joiner GUI. Then the same with an artificially broken link (reconnect in the
  middle of the answer) for H2, and with the web Terminal's message form for H3.
- **I2 Line diagnostics (ADR-0430).** Category `hwio.remote`, debug level, on both sides: per forwarded or
  received batch the number of lines and the sum of their lengths before and after sanitising, and one line for
  every `sync` (entries, truncated or not). **Never the text.** With these, a diagnostics report from the field
  shows whether text was lost on the host, on the way, on the joiner, or only on the screen.
- **I3 Result.** The cause, the test that reproduces it, and the proposed fix are written into this plan as an
  amendment; the user approves it before phase II.

### Item 2 — phase II (fix)

Defined by the amendment. Whatever the cause, the joiner appends the lines of one event-loop turn as one batch
(D5) only if the investigation shows single-row inserts matter (H1); it is not changed on suspicion.

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D1 | What does the connection button do in a remote tab? | **(a) "Leave session": leaves, the tab and its history stay (recommended)**; (b) "Disconnect" with the same effect (wording of a local tab, but nothing is disconnected on the customer's side); (c) hide the button in a remote tab and show "Leave" in the banner in every state | (a), as recommended (2026-11-24) | ADR-0429 |
| D2 | After leaving or after the session ended, can support join again from the same tab? | **(a) Yes, the button becomes "Join again" with the same code; the customer allows again (recommended)**; (b) No, the button is hidden; a new join goes through "Join a session" | (a), as recommended (2026-11-24) | ADR-0429 |
| D3 | Confirmation and running operations | **(a) No confirmation; disabled while an operation support started is running (recommended)**; (b) confirmation always; (c) no confirmation and never disabled | (a), as recommended (2026-11-24) | ADR-0429 |
| D4 | The banner's present "Leave" action (closes the tab) | **(a) Rename to "Close tab" (recommended)**; (b) keep "Leave" | (a), as recommended (2026-11-24) | ADR-0429 |
| D5 | Line diagnostics for item 2 | **(a) Counts and lengths at debug level on both sides, never text (recommended)**; (b) no permanent diagnostics, reproduction only | (a), as recommended (2026-11-24) | ADR-0430 |
| D6 | Release of item 2 | **(a) rc.5 if the cause is found and the fix is small (S), otherwise rc.6; item 1 does not wait for it (recommended)**; (b) rc.5 waits for item 2 | (a), as recommended (2026-11-24); superseded by D13 | — |
| D7 | A secret (`lrw config appkey`) arrived unhidden in support's console | **(a) Investigate with item 2 and propose the fix in the same amendment (recommended)**; (b) separate plan | (a), as recommended (2026-11-24) | — |

## Implementation steps

Phase A — item 1 (after approval of D1–D4):

- [x] A1 `ConnectionViewModel`: enable rule, actions `leave` / `join`, trigger; unit tests.
- [x] A2 `DeviceTabViewModel`: marker text for a remote tab; `RemoteJoinViewModel`: "left by the user" state and
      banner text.
- [x] A3 `DeviceBar.qml`, `RemoteBanner.qml`: texts, "Close tab"; Qt Quick tests: the button is enabled in a
      joined remote tab, Leave keeps the tab and ends the session, Join again opens a session with the same code,
      disabled during a remote operation.
- [ ] A4 Host side: test that the participant's leave is shown and audited. **Blocked (2026-11-24,
      implementation):** the host cannot learn that a participant left. The relay (`~/terminal/server/index.js`)
      announces only `newClient` and broadcasts `at`, `sync`, `write`; it sends nothing when a socket disconnects,
      and `RemoteConsoleSession::close()` only closes its socket (no tunnel message: `JoinerMessage` is
      `JoinerHello | OpRequest | Learn | OpAnswer`). Showing the leave needs a new tunnel object (a protocol change,
      excluded by the non-goals) or a relay change: a decision for the user.
- [x] A5 Translations (cs, de, pl, sk), docs `docs/*/remote-support.md`, release notes.

Phase I — item 2 (after approval of D5):

- [ ] I1 Reproduction (needs open points 1–3).
- [x] I2 Line diagnostics with tests that the log lines contain no console text.
- [ ] I3 Amendment of this plan with cause, failing test and fix; user approval.

Phase II — item 2 fix: steps from the amendment.

## Tests and verification

- GoogleTest and Qt Quick tests listed in the steps; full `linux-debug` ctest, `linux-asan`, `linux-tsan`,
  `linux-tidy` with zero findings, `all_qmllint`, `check_translations` with 0 unfinished, clang-format.
- Manual check (two Monitors through the production relay): Leave session during a joined session, the
  customer's banner and audit line; Join again and the customer's consent prompt; the button during a remote
  flash; the shortcut.
- Item 2: the reproduction of I1 as an automated test; manual retest by the reporter in the same environment.

## Risks and open points

1. **Which side and which button** does report 1 mean: support's device bar (assumed) or the customer's "End
   sharing"? If the latter, item 1 gets a second investigation.
2. **Item 2 facts needed from the reporter:** which side showed the blank lines (customer, support, both); what
   was on the other end (Monitor, web Terminal); device type and firmware (`system show`); Monitor builds on both
   sides; whether the link reconnected during the session; a saved console output of the affected side.
3. Without those facts the reproduction is a search through H1–H4 and may take long; I2 then is the way to get
   the answer from the next field session.
4. A joiner that leaves and joins again repeatedly asks the customer for consent each time; the host's existing
   rate limits for join requests apply (plan 0044), no new limit is planned.

## Amendment 1 — cause of item 2 (investigation result)

Date: 2026-11-24. Investigation only: no production code was changed. The plan stays in status Draft.

### A1.1 Result in short

- The host of the reported session was the **HARDWARIO mobile app** (confirmed by the reporter). It speaks a
  different dialect of the relay protocol than the web Terminal and Monitor: an `at` `read` event carries **one
  Bluetooth notification as text** (several lines, cut anywhere), and a `sync` carries **the whole output of a
  command as one string**. The web Terminal and Monitor send one line per event and one line per `outputs` element.
- Monitor's joiner takes every `read` and every `outputs` element as **one line** and passes it to
  `sanitizeDeviceText()`, which keeps only the text after the last carriage return. Of a piece with ten lines only
  the last one stays; if the piece ends with a line end, an empty line stays; if it ends with the prompt, the
  prompt stays. This is H3. H1, H2 and H4 are ruled out as the cause of the blank lines.
- This explains the commands with only a prompt, the empty lines and the few lines that did show text. It does
  **not** explain why every live line appeared twice; that part is not proven (A1.5).
- The secret arrived unhidden because the mobile app hides nothing, and support's Monitor hides nothing it receives.
  A Monitor host has a gap for the same line too (A1.8).

### A1.2 Sources read

| Source | Where | Version |
|---|---|---|
| Mobile app (Flutter) | `/home/hardwario/manager` (it was at `/home/hardwario/src/manager` when the investigation started and was moved during it) | commit `13950d4f`; all `manager:` references below are to this commit |
| Web Terminal and relay | `~/terminal` | commit `30a2a8e` |
| Monitor | this worktree | origin/main `0c8638e` |

Whether the customer's installed app is exactly commit `13950d4f` is not known (inferred: the session code has the
same structure in another checkout, commit `9167b3a0`, seen at the start of the investigation).

### A1.3 Proven facts: what each host sends

Relay (`~/terminal/server/index.js:74-86`): `newClient` goes to every socket of the namespace; `at`, `sync` and
`write` are broadcast unchanged to every other socket.

| | Web Terminal host | Monitor host | Mobile app host |
|---|---|---|---|
| `at` `read`, `data` | one line: the stream is split at `\n`, one trailing `\r` removed (`src/lib/ble-serial.ts:125-132`, `console.tsx:198-203`) | one sanitised line without CR or LF (`remote_host.cpp:804-819`; `LineAssembler::makeLine` → `sanitizeDeviceText` → `dropControls`, `text_sanitizer.cpp:102-112,128`) | **one NUS notification**, UTF-8 decoded, otherwise unchanged: `\r\n` inside, escape sequences, the prompt, cut at any byte (`manager: lib/features/chester/data/ble/chester_ble_gateway.dart:449-453`, `.../application/session/chester_session_controller.dart:113-116`) |
| `at` `write`, `data` | the command line (`console.tsx:191-196`) | the command line, secrets redacted (`remote_host.cpp:811-814`) | the command line, trimmed, one event per line of a **viewer's** `write` (`chester_session_controller.dart:153-164`). A command typed on the phone emits **no** `write` (`lib/hardwario/chester/screens/chester_terminal_screen.dart:365` calls the device controller only): viewers see its output live without a command line, and the command itself only in the next `sync` |
| `at` `state` | `disconnected` / `connection` / `connected` | the same | only `connected`, once, when hosting starts (`chester_session_controller.dart:136`) |
| other `at` events | none | `hwio` tunnel objects | `{event: "end", data: null}` when sharing stops (`chester_session_controller.dart:267`) |
| `sync` sent | on every `newClient` (`console.tsx:254-259`) | after the customer allowed the participant | on every `newClient`, its own included (`chester_session_controller.dart:125-133`) |
| `sync` `commands` entry | `{id, timestamp, command, index, response: {outputs: [line, …], result}}` | the same (`transcript.cpp:99-114`) | `{command, response: {outputs: [text], result: 0}}`: no `id`, `timestamp`, `index` (`manager: lib/features/chester/data/session/chester_session.dart:47-52`) |
| `outputs` | one element per line | one element per line | **one element per command: all its output merged into one string** (`chester_controller.dart:691-712` merges consecutive output, `chester_session_controller.dart:192-211` copies each terminal entry into `outputs`) |
| Secrets | not hidden | hidden by ADR-0356 rules (gap: A1.8) | **not hidden** (no redaction in the session code) |

How each joiner shows it:

- **Web Terminal** (`console.tsx:150-179`, `:553`): appends every `read` to the outputs of the last command and
  draws `outputs.join("\n")` in a `<pre>`. Text is never dropped, so a mobile app host's pieces are readable there
  (with an extra line break at every piece boundary). This is why the web Terminal "works".
- **Mobile app as viewer** (`chester_session_controller.dart:213-250`): joins `outputs` and consecutive `read`
  events **without** a separator. It treats `read` as a piece of a stream, consistent with its own host.
- **Monitor** (`remote_console_session.cpp:339-346`, `:378-385`): one console line per `read` event and per
  `outputs` element, through `makeLine()` (`:240-259`). `Transcript::fromJson` sanitises each `outputs` element as
  one line even earlier (`transcript.cpp:146`).

### A1.4 Proven facts: where the text is lost

`sanitizeDeviceText()` (`libs/core/src/text_sanitizer.cpp:127-128`) is made for **one line**:
`dropControls(applyCarriageReturns(stripEscapeSequences(bytes)))`.

- `applyCarriageReturns()` (`:116-125`) removes trailing `\r` and then keeps only what follows the last remaining
  `\r` (a terminal that redraws a line).
- `dropControls()` (`:102-112`) removes `\n`.

Applied to a multi-line piece `"A\r\nB\r\nC"` the result is `"C"`; applied to `"A\r\nB\r\n"` it is `""`; applied to
`"A\r\nB\r\nbt_nus:~$ "` it is `"bt_nus:~$ "`. Monitor's own consoles never do this: `LineAssembler::feed()`
(`libs/device/src/line_assembler.cpp:179-198`) splits the byte stream at `\n` first and sanitises each line
(`BleConsoleHandler::onData`, `ble_session.cpp:238-271`). The joiner has no such step because the hosts known when
plan 0044 was written send lines.

The fault is therefore in the callers (`RemoteConsoleSession::onAt`, `onSync`, `Transcript::fromJson`), not in the
sanitiser.

Other hypotheses:

- H1 (drawing): ruled out before (empty lines are in the saved file).
- H2 (replaced log): not the cause. A mobile app host does send a `sync` to everyone at every `newClient`, so
  support's console is replaced by the host's history whenever anybody joins or reconnects
  (`remote_console_session.cpp:362`); the history then shows pattern (a) below.
- H4 (not remote): ruled out. Monitor's own Bluetooth console assembles lines before sanitising.
- Log channel: not the cause. "Save view…" writes the rows of the view it is called for, but the lost lines are
  lost before a channel is chosen (`makeLine()` sanitises first).

### A1.5 The evidence file, pattern by pattern

The file has three parts. Line numbers are those of the file (92 lines).

| Lines | What is there | Explanation | Status |
|---|---|---|---|
| 1 | "— Session history from the customer —" | written by `onSync` after it cleared the log (`remote_console_session.cpp:362,372`); it marks the **last** `sync` only | proven |
| 2–29, 52–53 | a command, then one line `bt_nus:~$ ` | history: `outputs` is one string that ends with the prompt; only the text after its last `\r` survives. Three commands in a row (10–12) are entries with empty `outputs`. Lines 52–53 are the last history entry: the last `sync` lies between lines 53 and 54 | mechanism proven by test; the bytes are inferred |
| 29 | `bt_nus:~$ ` four times on one line | one `outputs` string in which the prompt follows the last `\r` four times with only removed bytes between (LF, escape sequences of a line repaint) | inferred |
| 30–51 | a command, then one empty line (11 commands) | history: the `outputs` string ends with a line end, not with the prompt. Example: the app's own text for a command it could not send, `"\r\n[error] …\r\n"` (`chester_controller.dart:684-687`), after `config save` (line 30) restarted the device | mechanism proven by test; the reason for the missing prompt is inferred |
| 54–92 | a command, then several lines: empty ones, a few with text, the prompt | live `read` events, one console line per notification: empty when the notification ends with a line end, the last line of the notification when it ends inside or right after a line, the prompt when it ends with the prompt. Every other line of each notification is lost. A history entry of this host gives exactly one output line, so these lines cannot come from a `sync`. The `>` lines here are commands a viewer sent (support); the host echoes only those | mechanism proven by test; where each notification ended is inferred |
| 54–92 | every device line appears twice, in strict pairs (also the empty ones and the prompt); the `>` command lines appear once | **not explained** (see below) | not proven |

The pairs. Facts that limit the explanation:

- `RelayClient::onEvent` emits `atReceived` once per `at` event, whatever its payload (`relay_client.cpp:585-600`),
  and `onAt` appends one log line per event for both directions (`remote_console_session.cpp:339-346`). A
  duplicated relay connection or session on Monitor's side would double the `>` lines too. They are single.
- The view (`ConsoleLineModel::refresh`, `console_line_model.cpp:220-251`) reads the log by a cursor, and "Save
  view…" writes the view's rows (`:742-755`). No path was found that adds a row twice.
- The mobile app has one place that emits `read` (`chester_session_controller.dart:113-116`) and adds one stream
  event per notification (`chester_ble_gateway.dart:449-453`); it replaces its subscription before it subscribes
  again (`:422-425`).

Candidates, none proven: (1) the phone delivered every notification twice after it reconnected to the restarted
device (two GATT client connections or two subscriptions below the app's code); then the phone's own terminal and
a web Terminal joined at that moment show the same pairs; (2) a cause in Monitor that this investigation did not
find. What decides it: the relay's debug log or the WebSocket frames of one `info show` in such a session (two
`read` events with the same `data`, or one), or the counts of the line diagnostics of ADR-0430 (events received
against lines appended).

Note for the fix: if the host sends a notification twice, a joiner that assembles lines shows doubled or mixed
text. Monitor cannot remove such duplicates safely (two equal pieces in a row are legal, for example two line
ends).

### A1.6 Reproduction tests (uncommitted)

In `libs/remote/tests/remote_session_test.cpp`, suite `RemoteSession`. A raw Socket.IO participant plays the mobile
app host through `FakeRelay`. The message forms are those of A1.3 (proven from the app's source); the device bytes
are synthetic (Zephyr shell over NUS: the answer starts with `\r\n`, lines end with `\r\n`, then the prompt). The
tests compare the device lines that carry text and leave out empty lines and the prompt, so they do not fix how the
prompt is shown.

| Test | Feeds | Expects | Result on `0c8638e` |
|---|---|---|---|
| `MobileAppHostHistoryKeepsEveryOutputLine` | a `sync` with two entries: `outputs` = one string of three lines ending with the prompt; one string ending with a line end | 4 lines with text | fails: 0 lines (the log holds `bt_nus:~$ ` and an empty line) |
| `MobileAppHostLiveOutputKeepsEveryLine` | `write`, then three `read` notifications that end at line ends or with the prompt | 3 lines with text | fails: 0 lines (two empty lines and the prompt) |
| `MobileAppHostNotificationCutInsideALine` | notifications cut inside a line | 2 whole lines | fails: a half line and one whole line. Holds only for fix option (a) of D8 |

A fourth test proves the host-side gap of A1.8: `RemoteHost.SecretInCommandFormIsRedactedTowardsTheRelay` in
`libs/remote/tests/remote_host_test.cpp` (a made-up key value).

The existing test `RemoteSession.JoinerWithAWebTerminalHost` (one line per `outputs` element and per `read`) passes
and must keep passing after the fix.

Run on 2026-11-24 (`linux-debug`, only the target `hwio_remote_tests` was built):

```sh
cmake --build --preset linux-debug --parallel 5 --target hwio_remote_tests
build/linux-debug/libs/remote/tests/hwio_remote_tests \
  --gtest_filter='RemoteSession.MobileApp*:RemoteSession.JoinerWithAWebTerminalHost:RemoteHost.Secret*'
```

```text
[       OK ] RemoteHost.SecretsAreRedactedTowardsTheRelay
[  FAILED  ] RemoteHost.SecretInCommandFormIsRedactedTowardsTheRelay
    Which is: "lrw config appkey 00112233445566778899aabbccddeeff"      (made-up value)
  "lrw config appkey <redacted>"
[       OK ] RemoteSession.JoinerWithAWebTerminalHost
[  FAILED  ] RemoteSession.MobileAppHostHistoryKeepsEveryOutputLine
  answerLines(joiner.log())  Which is: {}
  expected: { "Vendor name: HARDWARIO", "Product name: CHESTER-M", "Serial number: 2100000123",
              "[error] The command was not sent." }
  all device lines: { "bt_nus:~$ ", "" }
[  FAILED  ] RemoteSession.MobileAppHostLiveOutputKeepsEveryLine
  answerLines(joiner.log())  Which is: {}
  expected: { "app config interval-sample 60", "app config interval-aggreg 300",
              "app config interval-report 1800" }
  all device lines: { "", "", "bt_nus:~$ " }
[  FAILED  ] RemoteSession.MobileAppHostNotificationCutInsideALine
  answerLines(joiner.log())  Which is: { "app config interval-sa", "app config interval-aggreg 300" }
  expected: { "app config interval-sample 60", "app config interval-aggreg 300" }
  all device lines: { "app config interval-sa", "app config interval-aggreg 300", "bt_nus:~$ " }
```

"all device lines" is what support's console shows today: the prompt alone and an empty line for the history
(lines 2–53 of the evidence file), empty lines and the prompt for the live answer (lines 54–64).

The whole binary `hwio_remote_tests` (86 tests) was run once: 82 pass, exactly these four fail.

What would confirm the inferred bytes: one capture of a phone-hosted session with one `info show` and one
`config show`: either the debug log of a local relay (`tools/remote/local-terminal.sh`, payloads at level 20) with
the phone's relay URL pointed at it (the app has a debug-only relay override), or the WebSocket frames of a web
Terminal joined to the session (browser developer tools). Never from a device with real keys.

### A1.7 Proposed fix for the blank lines (design only)

**What changes.** The joiner reads the device output of a host as what it is: lines (web Terminal, Monitor) or
pieces of a byte stream (mobile app).

1. New private class in `libs/remote/src` (working name `HostOutputReader`), owned by `RemoteConsoleSession`:
   - **Line mode** (start, and always after a host hello): a `read` `data` without `\n` is one line. Byte for byte
     today's behaviour.
   - **Stream mode**: entered for the rest of the session when a `read` `data` or a `sync` `outputs` element
     contains `\n`. The text is fed to a `device::LineAssembler` (`libs/device`, already public): split at `\n`, one
     trailing `\r` removed, each line sanitised, an unfinished line kept for the next event (at most
     `LineAssembler::maxPartialBytes` = 4096 bytes, then emitted). Level and channel as in `makeLine()` today.
   - Prompt and empty line as in Monitor's own Bluetooth console (ADR-0082, `ble_session.cpp:238-271`): an
     unfinished line that is a Zephyr prompt (`<name>:~$ `) is not a line; the one empty line after a prompt or
     after a `write` is not output. The rule must not need a `write`: output of a command typed on the phone
     arrives without one (A1.3).
   - All lines of one event are appended as one batch (`appendLines`), which also covers D5's batch idea.
2. `Transcript::fromJson` (`transcript.cpp:116-156`) splits an `outputs` element that contains `\n` into lines
   before sanitising, so `TranscriptEntry::outputs` keeps its meaning (one line per element). The unfinished tail of
   the last entry seeds the reader's unfinished line. The 256 KiB bound counts the lines as today.
3. `onSync` resets the reader (the log is replaced).
4. CLI: `hwio remote join` uses the same session and is fixed by the same change.

**Public interfaces touched:** `hwio/remote/transcript.h` (documented behaviour of `fromJson`; possibly one new
field for the unfinished tail), the header comment of `remote_console_session.h`, one small helper in
`hwio/device/line_assembler.h` for the generic Zephyr prompt. No protocol change, no new dependency, no new thread.

**A Monitor host stays unaffected** because (1) its `read` data and `outputs` elements cannot contain `\r` or `\n`
(`dropControls`, `text_sanitizer.cpp:102-112`), so stream mode is never entered by content, and (2) after the
host's hello the reader stays in line mode whatever arrives. **A web Terminal host stays unaffected** because it
splits at `\n` itself (`ble-serial.ts:125-132`). Silent shell queries are not sent to a remote host
(`device_tab_view_model.cpp:597-600`), so `captureLine` is not involved.

**Risks.**

- The host kind is recognised by content (a `\n` in the data), because the mobile app sends no hello and an empty
  history has nothing to inspect. A piece without `\n` that arrives before the first piece with one is shown as a
  line (at most once per session: an answer starts with `\r\n`).
- A dropped or doubled `read` event joins or doubles parts of lines (A1.5). Bounded by the 4096-byte rule.
- After the fix, lines the bug hides today become visible, among them secrets of a host that does not hide them
  (A1.8). D9 has to be decided together with D8.
- Plan 0059 changes `libs/remote` too (`RemoteConsoleSession::profile()`); merge order to be agreed.

**Size:** option (a) M (one class of about 150 lines, `fromJson`, tests); option (b) S.

**Tests:** the three tests of A1.6 pass; unit tests of the reader (mode switch, hello keeps line mode, unfinished
line across events, 4096-byte bound, prompt and empty-line rule, output without a preceding `write`, reset on
`sync`); `Transcript::fromJson` with a merged output and with the byte bound; `JoinerWithAWebTerminalHost` and the
Monitor-to-Monitor tests unchanged; CLI end-to-end `remote join` against a relay scripted as the mobile app;
manual retest with the phone (interop checklist gets rows "mobile app host → Monitor" and "Monitor host → mobile
app").

**Other gaps with a mobile app host** found on the way (facts; decisions D11, D12):

- The app's `at` `end` event is ignored (`remote_console_session.cpp:336-338`: `data` is not a string), so support
  does not learn that the customer stopped sharing.
- The tab title says "Session NNN — web Terminal" for every host without a hello (`:199-201`).
- A Monitor or web Terminal **host** with a mobile app **viewer**: the app joins the lines without a line break
  (`chester_session_controller.dart:218,243-250`), because Monitor and the web Terminal send lines without a line
  end. This is the same mismatch in the other direction; it can only be fixed in the app.

A character of several bytes that a notification cuts in two is already lost on the host (it decodes every
notification on its own, `chester_ble_gateway.dart:451`); the joiner cannot restore it.

To report to the mobile app's team (no change in this repository): the two dialects; the missing line breaks as a
viewer; no `write` event for a command typed on the phone; nothing is hidden toward the relay; `sync` is sent to
everyone at every `newClient`; characters cut by a notification; the possible double notifications of A1.5.

### A1.8 The secret that arrived unhidden

Facts:

1. **The mobile app host sends everything as it is** (`chester_session_controller.dart:113-116,159,192-211`); the
   web Terminal host too (`console.tsx:191-203`). Every secret the device prints or the customer types goes to the
   relay and to every socket of the namespace in plain text (TLS only, ADR-0329), also to participants nobody
   allowed (ADR-0335). No change in Monitor can prevent that.
2. **Monitor's joiner hides nothing it receives.** `makeLine()` only sanitises
   (`remote_console_session.cpp:240-259`). A remote session has no console file (`remote_console_session.h:111`).
   The screen shows the line as received. "Save view…" on main writes the rows as shown
   (`console_view_model.cpp:730-760`, `console_line_model.cpp:742-755`); that is how the key got into the evidence
   file.
3. **Existing rules.** ADR-0186: files hide secrets (console file, history), the screen and Copy show them.
   ADR-0356: a Monitor **host** hides secrets in everything it sends to the relay, with the consequence "support
   cannot read a claim token or passkey through the session". ADR-0426 (accepted 2026-11-24, plan 0060 A4, branch
   `strnad/impl-0060-test-feedback`, not on main): "Save view…" hides secrets with `device::ConsoleRedaction` of the
   tab's profile and console target and creates the file owner-only; that redaction covers a shell output line in
   command form (`lrw config appkey …`, prefix list `libs/device/src/profiles/chester.cpp:150-153`) and
   `<key>: <value>` lines (`claim token`, `ble passkey`). Whether a **remote** tab passes `chester` / `app` there
   was not verified; it needs a test.
4. **A Monitor host has the same gap for this line.** `RemoteHost::redactedOutput()` (`remote_host.cpp:869-878`)
   matches only `<key>: <value>`. An output line in command form, as `config show` prints it, goes to the relay
   with its value. The console file does hide this form (`console_file_sink.cpp:418-440`, ADR-0186 amendment of
   2026-11-02). The existing test covers only `claim token` (`remote_host_test.cpp:286`). Proven by the failing
   test `RemoteHost.SecretInCommandFormIsRedactedTowardsTheRelay`. Plan 0059 (ADR-0416) adds output prefixes for
   STICKER on the same path; the two changes have to be made as one.
5. **The fix of A1.7 makes more secrets visible** on support's side than today, because the bug now swallows most
   lines of `info show` (claim token, BLE passkey) from a host that does not hide them.
6. The application key of the evidence file passed the relay in plain text and is stored in a file on support's
   computer. Whether the customer should replace it is for the user to decide (open point 5).

Options are in D9 and D10. Limits of any joiner-side hiding: it uses the lists of the CHESTER profile, it does not
know the secrets of other firmware, and it cannot take back what the relay and other participants already
received.

### A1.9 New decisions

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D8 | How does Monitor's joiner read the output of a host that sends pieces of the stream (mobile app)? | **(a) Assemble lines across events in stream mode, recognised by a line feed in the data; prompt and first empty line as in Monitor's Bluetooth console (recommended, M)**; (b) split every event on its own at line feeds, no state: nothing is lost, but a line cut by a notification shows in two pieces and the prompt shows as a line, as in the web Terminal (S); (c) no change in Monitor, the mobile app sends lines like the web Terminal (sessions with app versions already installed stay broken; the app's own viewer expects pieces) | (a), as recommended (2026-11-24) | ADR-0431 |
| D9 | Does support's Monitor hide secrets it receives from a host that does not hide them? | **(a) Yes, at reception, for a host without a hello: the profile's rules (ADR-0186 lists) are applied before a line enters the console log, so screen, Copy, "Save view…" and the CLI show `<redacted>`; same result for support as with a Monitor host under ADR-0356 (recommended)**; (b) only in "Save view…" as ADR-0426 already decides, plus a test that it works in a remote tab; the screen shows the value; (c) nothing | (a), as recommended (2026-11-24) | ADR-0432 |
| D10 | A Monitor host sends a secret in command form (`config show` output) to the relay with its value | **(a) The host uses the same redaction as the console file for `read` lines and the transcript (one implementation, `device::ConsoleRedaction` of plan 0060 A4; together with plan 0059 / ADR-0416) (recommended, S)**; (b) leave it and correct the consequence text of ADR-0356 | (a), as recommended (2026-11-24) | ADR-0432 |
| D11 | The mobile app's `at` `end` event | **(a) Monitor's joiner ends the session on it, like on the tunnel `end` of ADR-0354 (recommended, S)**; (b) keep ignoring it | (a), as recommended (2026-11-24) | ADR-0431 |
| D12 | Title of a joined tab whose host sent no hello | **(a) "Session NNN NNN — web Terminal or mobile app" (recommended; new string in 5 languages)**; (b) keep "— web Terminal" | (a), as recommended (2026-11-24) | — |
| D13 | Release (replaces the open part of D6) | **(a) D8, D9, D10 together in rc.6; item 1 in rc.5 does not wait (recommended: D8 (a) is size M and should not ship without D9)**; (b) D8 (b) alone in rc.5, the rest in rc.6; (c) everything in rc.5 | (b) changed by the user 2026-11-24: "all fixes of this bug must go into rc.5" — D8, D9, D10, D11, D12 ship in rc.5 together with item 1 | — |

D5 (line diagnostics, ADR-0430) stays useful: with it the report of this session would have shown notifications of
several hundred bytes becoming lines of 0 bytes, and it is the cheapest way to settle the pairs of A1.5.

### A1.10 Steps (after approval; replaces "Phase II")

- [x] II-1 `HostOutputReader` (line mode, stream mode, prompt rule) with unit tests; `Transcript::fromJson` (D8).
- [x] II-2 `RemoteConsoleSession::onAt` / `onSync` use it; the tests of A1.6 pass; CLI end-to-end test.
- [x] II-3 Secrets as decided in D9 and D10, with tests in both directions. `device::ConsoleRedaction` is taken
      unchanged from plan 0060 A4 (branch `strnad/impl-0060-test-feedback`, commit `e86a600`: the two files and the
      `ConsoleFileSink` change, nothing else), so both branches merge without a second shape. The host checks every
      output line as shell output (as `redactedOutput()` did for every `read` line). The output prefixes of plan 0059
      (ADR-0416, STICKER) are not on this branch: they join the same lists when 0059 is merged.
- [x] II-4 `at` `end` and the tab title as decided in D11 and D12; translations.
- [ ] II-5 Interop checklist rows for the mobile app (done: rows 2.5 and 2.6 of the 0044 D1 checklist); manual
      retest with the phone (open); report to the app's team (open).

### A1.11 Open points added

5. The exposed application key of the evidence file (A1.8, fact 6): inform the customer or not.
6. The pairs of A1.5 need one capture or one session with the ADR-0430 diagnostics.
7. The installed version of the mobile app in the reported session (A1.2).

## Amendment 2 — the customer's side learns that support left (2026-11-24)

**Decision D14 (Matej Strnad, 2026-11-24: "Add a leave message"; ADR-0439).** Step A4 was blocked because the host
cannot learn that a participant left: the relay sends nothing on a disconnect and the tunnel has no leave message
(see the note at A4). The non-goal "no change of the session protocol" is lifted for this one message.

### Design

- **Message.** A new tunnel message from the joiner to the host, type `leave`, no payload besides the common
  fields. Protocol version stays 1.
- **Capability-gated.** An rc.4 / rc.5-without-this host rejects unknown message types with a warning, so the
  joiner sends `leave` only when the host's hello lists the capability `leave` (the `caps` list of plan 0059,
  ADR-0414). A Monitor host of this build lists it. Hosts without a hello (web Terminal, mobile app) and older
  Monitor hosts never get the message: behaviour as today.
- **When it is sent.** On "Leave session" (ADR-0429), on closing a joined remote tab, and on quitting Monitor
  with a joined tab: best effort, before the socket is closed, without waiting for an answer. A lost connection
  sends nothing: the existing idle rules stay the only signal there.
- **Host on `leave`.**
  1. The participant's permission is withdrawn: commands and operation requests are dropped again until the
     customer allows a participant anew (the state after "Join again" before the consent).
  2. Audit line in the customer's console and log: "— Remote: Support left the session. —" (translated, with the
     English `textEn` for MCP as ADR-0374).
  3. The banner returns to "Shared with support — code NNN NNN" (no "Support can send commands.").
  4. A remote operation that this participant started and that is still running is **not** cancelled: the joiner's
     button is disabled during it (D3), and a `leave` that arrives anyway (tab closed, quit) leaves the operation
     to finish on the customer's side as a lost connection would.
- **Forged `leave`.** The relay does not authenticate senders, so anybody who knows the code can send `leave`.
  Its only effect is to withdraw permission and to end with the customer being asked again, never to grant
  anything; the host's existing rate limit for remote lines applies to it too.
- **Several participants.** The relay gives the host no participant identity (plan 0044), and the host's
  permission is one flag for "a participant". `leave` therefore withdraws that one permission; another
  participant still connected has to be allowed again. Documented as a limit, not solved here.
- **Base.** The capability list exists only with plan 0059 (branch `strnad/plan-0059-remote-new-devices`), so
  this amendment is implemented on a branch that contains plans 0059 and 0061 together (the plan 0062 branch
  `strnad/impl-0062-mcp-remote` is the first such branch).

### Steps

- [x] III-1 `tunnel`: message `leave` (encode, decode, bounds), capability `leave` in the host hello; compat
      tests: an rc.4-shaped host never receives it, an rc.4-shaped joiner ignores the new capability.
- [x] III-2 Joiner: send on Leave session, tab close and quit, only with the capability; unit tests.
- [x] III-3 (deviation: no English `textEn` of the audit line for MCP, awaiting the user; see the notes) Host: withdraw permission, audit line, banner, rate limit; a running operation is not cancelled;
      tests incl. a forged `leave` and a command after `leave` being dropped.
- [x] III-4 Qt Quick test of the customer's banner; translations (cs, de, pl, sk); docs `remote-support.md` in
      five languages; release notes; interop checklist row (0044 D1, row 2.7). This completes A4.

Implementation notes (2026-11-25):

- The joiner sends `leave` in `RemoteConsoleSession::closeSession()`, the one path behind Leave session, a closed
  tab and quit (`ConsoleSession::close()` and the destructor); `RelayClient::stop()` sends it before the socket
  closes, as the host's `end`. Consequence: `hwio remote join` sends it too when it exits.
- The host handles `leave` after the existing check for an allowed participant: a `leave` while no participant is
  allowed, or while a join question is open, is dropped like the participant's lines (the open question decides).
- Open: the audit line has no English `textEn` for MCP. `ConsoleSession::recordRemoteEvent()` takes one text, so no
  audit line of the host has one today; adding it changes that interface for every transport and is left to a
  decision. The line is written translated like every other audit line.
- Open: a `leave` while the customer's operation question for that participant is still open does not withdraw the
  question (the customer can still allow the operation); a `leave` during a remote LTE update makes the answer to a
  later question to support be dropped (it times out: "Support did not answer. Nothing was changed."). Not decided
  by this amendment.
- Not done: the manual check with two Monitors.

Verification as for the rest of the plan (debug, ASan, TSan, tidy, qmllint, translations, format, includes), plus
the manual check with two Monitors: Leave session → the customer's banner and audit line; Join again → consent.
