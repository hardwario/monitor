# 0014 — M6 MCP in the GUI: server lifecycle, tabs as devices, consent, activity, settings

- **Status:** Draft (revised 2026-11-03)
- **Milestone:** M6 (plan 2 of 2, ADR-0094)
- **Author:** Matej Strnad (with Claude), 2026-10-16; revised 2026-11-03 against the code after 0021–0029
- **Related ADRs:** ADR-0003, ADR-0008, ADR-0025, ADR-0027, ADR-0043, ADR-0058, ADR-0059, ADR-0063,
  ADR-0094–ADR-0105, ADR-0107, ADR-0108, ADR-0150, ADR-0161, ADR-0162, ADR-0164, ADR-0172, ADR-0181, ADR-0182,
  ADR-0188, ADR-0202, **ADR-0240–ADR-0244** (this revision)
- **Depends on:** [0013](0013-m6-mcp-core-cli.md) phases A–C (`hwio_mcp`, `hwio_mcp_tools`, `TokenStore`, CLI
  `hwio mcp`), [0025](0025-security-fixes-round-1.md) (MCP hardening), [0004](0004-m3-gui-shell.md) /
  [0005](0005-m3-console.md) / [0006](0006-m3-flash-settings.md) / [0021](0021-gui-feedback-round-1.md) /
  [0028](0028-gui-feedback-round-2.md) (shell, console, Settings tab). Bluetooth tabs as MCP devices: step D1 after
  [0012](0012-m5-ble-gui-pairing.md) is merged (ADR-0243).
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md)

> **Revision 2026-11-03.** Names follow ADR-0181 (`hwio::`, `hwio_mcp`, `hwio_mcp_tools`, CLI `hwio`,
> `HWIO_MCP_TOKEN`). The open ADR-0103 question is decided by ADR-0240 (backend errors in the GUI language), review
> R6 by ADR-0241, connect on a user's tab by ADR-0242, the structure by ADR-0243 (a registered extension, J-Link
> first), the minor defaults by ADR-0244. R17 is solved by design: the GUI provider's `open` completes
> synchronously after the tool's own consent request (see Design). Q9/R12 needs nothing here: MCP call-site texts
> are plain English, the markers and busy texts of `hwio_mcp_tools` are translated in the context `hwio::mcp`
> (already in `i18n/hwio_cs.ts`).

## Context

ADR-0008: the GUI hosts MCP over Streamable HTTP on 127.0.0.1, opt-in, and the AI works on **the same live
session the user sees**. 0013 delivered the server (`McpServer`, `HttpServer` with the ADR-0102 / ADR-0188 checks),
the tools and the seams `IDeviceProvider`, `IConsentBroker` and `IActivitySink`; the CLI uses them in `hwio mcp`.
This plan connects them to the GUI: the open tabs are the devices, consent comes from a prompt window, and the
activity is visible in the console and in the MCP section of Settings.

## Goals / Non-goals

**Goals**
1. `McpController`: starts and stops `McpServer` + `HttpServer` from the settings, owns the token, reports the
   state (off / listening / N clients / error).
2. `GuiDeviceProvider`: J-Link tabs ↔ device ids, `connect` opens (or connects) a tab after consent, `disconnect`
   only for AI-opened tabs, busy state, markers and the console hint (ADR-0098, ADR-0105, ADR-0242).
3. `GuiConsentBroker` + `ConsentWindow` (own always-on-top window, ADR-0108): categories, Ask / Always allow,
   per-client remembering (ADR-0241), queue, timeout, withdrawal (ADR-0101).
4. Settings → **AI assistants (MCP)**: enable, port, token, client snippets, consent rules, grants, activity.
5. Rail indicator "MCP" with state, opening the section.
6. EN + CS texts, accessibility, Qt Quick tests and screenshots.

**Non-goals**
- Protocol, tool or backend changes (0013). If the GUI needs one, stop and amend 0013.
- Remote access, TLS, OAuth (ADR-0102). An MCP client inside the app.
- Bluetooth tabs as MCP devices before 0012 is in `main` (step D1, ADR-0243).
- English rendering of backend errors (ADR-0240).

## User experience

### Settings → AI assistants (MCP)

```
┌ AI assistants (MCP) ─────────────────────────────────────────────────────────┐
│ [■ ] Allow AI assistants to use HARDWARIO Monitor          ● Listening        │
│      AI tools such as Claude Code can read the console, send commands and,   │
│      with your permission, flash devices.                                    │
│ Address   http://127.0.0.1:[47800]/mcp                            [Copy]      │
│ Token     ••••••••••••••••••••••••    [Show] [Copy] [Regenerate…]             │
│ Connect a client                                                             │
│   Claude Code (HTTP)    claude mcp add --transport http hwio …   [Copy]       │
│   Claude Code (.mcp.json, token from HWIO_MCP_TOKEN)             [Copy]       │
│   Claude Desktop (runs the hwio command separately)              [Copy]       │
│ Permissions                                Ask        Always allow           │
│   Flash firmware (flash, LTE, raw flash)    (•)        ( )                    │
│   Erase and recover                         (•)        ( )                    │
│   Write memory                              (•)        ( )                    │
│   Write UICR / factory data                 (•)        ( )                    │
│   Connect devices (opens a tab)             (•)        ( )                    │
│   Allowed until disconnect: Claude Code — Flash firmware   [Revoke]          │
│ Activity (last 1000)                                       [Copy] [Clear]    │
│   14:02:11  Claude Code  flash     jlink:6830…  app.hex      ok     12.4 s    │
│   14:01:58  Claude Code  send_command jlink:6830…  "lte state"  ok   0.3 s   │
└──────────────────────────────────────────────────────────────────────────────┘
```

- **Enable** is off by default. Turning it on creates the token file if needed and starts the server. The state
  text is "Off", "Starting…", "Listening", "Listening · 2 clients", or an error ("Port 47800 is in use by another
  program — choose another port", `mcp.port_in_use`) with **Retry**. A token file error (`mcp.token_file`) shows an
  ErrorPanel with **Regenerate token**.
- **Port:** a spin box (1024–65535). A change restarts the server; a note says that clients have to be
  reconfigured.
- **Token:** masked, with Show and Copy. **Regenerate…** asks "Regenerate the token? Connected AI clients stop
  working until you update their configuration." The new token applies to the next requests at once
  (`HttpServer::setToken`), and remembered grants are forgotten (ADR-0241). The token is never shown in the
  activity list or the logs.
- **Snippets** (ADR-0106, ADR-0244): Claude Code HTTP with the token embedded (shown masked, copied in full),
  `.mcp.json` using `${HWIO_MCP_TOKEN}`, and the Claude Desktop stdio entry with the installed `hwio` path
  (`cliCommand()`, ADR-0122) and the note "Runs separately from this window and cannot use a probe that is open
  here." Copy goes through `AppViewModel::copyToClipboard` (ADR-0202) with the toast "Copied. Run it in a
  terminal." (the `.mcp.json` / Desktop ones: "Copied.").
- **Permissions:** one row per consent category (ADR-0101) with Ask / Always allow. The memory-write row has the
  hint "Writing memory can make the device run arbitrary code." (ADR-0164). The grant list shows the remembered
  "Allow … for <client>" answers, each with Revoke.
- **Activity:** newest first, 1000 entries in memory (ADR-0105, ADR-0244). Columns: time, client, tool, device,
  arguments (short), result, duration. Errors show their id with a tooltip. Copy copies all rows as text (one line
  each), Clear empties the list.

### Rail indicator

Above Settings at the bottom of the rail (ADR-0105, ADR-0172) sits an "MCP" item with a state dot: hidden when off;
green when listening; green with a count badge when clients are active (a request within the last 60 s); red on
error. Its tooltip is e.g. "AI assistants: listening on port 47800, 1 client". Clicking it opens Settings → AI
assistants (`openSettings("mcp")`).

### Consent prompt window (ADR-0101, ADR-0108)

```
┌ HARDWARIO Monitor — Allow AI assistant? ──────────────────────┐
│  Claude Code wants to flash firmware                          │
│  Device    CHESTER · J-Link 683012345 (tab 1)                  │
│  File      /home/me/proj/build/merged.hex  (412 KiB)           │
│  Target    app                                                │
│                                                               │
│  Show device ↗                                                │
│  [Deny]  [Allow once]  [Allow flashing for Claude Code]        │
│  Denied automatically in 1:54 if not answered.                │
└───────────────────────────────────────────────────────────────┘
```

- A QML `Window` with `Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint`, `transientParent: null`
  (so it shows while the main window is minimised), centred on the screen under the mouse cursor
  (`QGuiApplication::screenAt(QCursor::pos())`, falling back to the main window's screen), raised with
  `requestActivate()` plus `QGuiApplication::alert(mainWindow)`.
- "Visible on all workspaces" (ADR-0108, best effort without native code): Qt 6.8 has no portable API for it, so it
  is not done; the manual check C2 records the behaviour per desktop.
- The main window does not switch tabs. **Show device ↗** raises the main window on the device's tab; the prompt
  stays open.
- One prompt at a time, at most 8 queued (more → `mcp.consent_timeout` at once). The 120 s countdown starts when a
  prompt is shown; queued prompts wait (bounded by 8 × 120 s).
- Detail rows per category: flash (file and size, or catalog id; target; "Contains UICR data" in the warning colour
  when `uicr`); erase (target, scope; for `scope=all` / `recover` "The PIB (serial number, keys) is lost" — these
  ask erase + uicr in one prompt, ADR-0161); memory-write (target, address range, size, file; the ADR-0164 hint);
  uicr ("Factory data of the device may be overwritten"); connect ("Open a new tab for J-Link 683012345" or
  "Connect the tab <title>", ADR-0242). Several categories in one request → one prompt listing all, and the
  remember button reads "Allow these for <client>".
- When the request is withdrawn (the client cancelled or disconnected, the server stopped), the prompt closes with
  the toast "The AI assistant cancelled the request".
- Keyboard: Esc = Deny. No default button; Tab moves between the buttons. The window announces its title and the
  action to screen readers when shown.
- Closing the main window quits (no tray, ADR-0108): pending prompts are denied and the server stops.

### Device tabs

- AI-opened tabs carry an "AI" badge next to the title (tooltip "Opened by Claude Code"). They are appended
  without switching the current tab and are not restored on the next start (ADR-0244).
- **Markers** (ADR-0105, written by `hwio_mcp_tools`): "— AI (Claude Code): flash merged.hex —",
  "— AI: flash done in 12.4 s —", "— AI: flash failed: <message> —".
- While an AI `send_command` runs, the console input shows the placeholder "AI is running a command…". Input stays
  enabled.
- **AI operations** set the tab busy (`setBusy("ai", …)`): the device bar shows the busy text ("AI: Flashing…") and
  a **Cancel** button (ADR-0244). Cancel ends the tool call with `cancelled`. Closing the tab or quitting asks the
  existing busy question ("An AI assistant is running an operation on this device. Cancel it and close the tab?").
- A user operation on a tab makes AI operation tools return `internal.busy`; console tools still work.

### Texts

All new GUI texts are English source + Czech (ADR-0027). MCP wire texts stay English (ADR-0103) except backend
error texts, which come in the GUI language (ADR-0240).

## CLI

None (the CLI side is in 0013).

## Design

### Modules

```
app/gui/src/mcp/   mcp_registration.{h,cpp}   registerMcp(AppViewModel&): keys, controller, section (ADR-0243)
                   mcp_controller.{h,cpp}     QML singleton McpController (state, settings, token, snippets, grants)
                   gui_device_provider.{h,cpp}  IDeviceProvider over the J-Link tabs
                   gui_consent_broker.{h,cpp}   IConsentBroker: rules, grants, queue, timeout
                   consent_view_model.{h,cpp}   the current prompt for ConsentWindow.qml
                   activity_model.{h,cpp}       QAbstractListModel + IActivitySink, ring of 1000
                   client_snippets.{h,cpp}      snippet texts (pure functions)
app/gui/qml/settings/McpSection.qml   app/gui/qml/mcp/ConsentWindow.qml  McpRailIndicator.qml
app/gui/tests/viewmodels/mcp_*_test.cpp   app/gui/tests/tst_mcp.qml
```

`pages/builtin_pages.cpp` calls `registerMcp(app)` after the Flash/Settings registration. The section is a
contributed Settings section (`id "mcp"`, order 50, between nRF Util and About). `Main.qml` instantiates one
`ConsentWindow`.

### Shell changes (small, additive; amend 0004 per ADR-0059 / ADR-0243)

| File | Change |
|---|---|
| `DeviceTabViewModel` | `Q_PROPERTY(QString openedBy …)` (client name, "" = the user; set before `configure`), `Q_PROPERTY(bool aiCommandActive …)` |
| `AppViewModel` | `DeviceTabViewModel* appendTab()` (a Choose-device tab at the end, current tab unchanged; nullptr when full); `tabsJson()` skips tabs with `openedBy` |
| `NavRail.qml` | `McpRailIndicator {}` between the spacer and the global pages |
| `DeviceTabBar.qml` | "AI" badge after the title when `openedBy !== ""` |
| `DeviceBar.qml` | Cancel button when `tab.busyPage === "ai"` → `McpController.cancelOperation(tab)` |
| `ShellInput.qml` / console page | placeholder "AI is running a command…" while `tab.aiCommandActive` |

**Where 0012 meets this plan:** the same files above (0012 changes `DeviceTabViewModel`, `AppViewModel`, `NavRail`,
`DeviceTabBar`, `DeviceBar`), each with a few added lines here; the second merge resolves them by hand. Step D1 then
makes `GuiDeviceProvider` list Bluetooth tabs (`transport() == "ble"`, id `ble:<id>`, `consoleSession()`,
`DeviceRef::jlink` null) and recently seen devices of `BleService`.

### Classes (`hwio::gui`)

```cpp
class McpController : public QObject {          // child of AppViewModel; QML singleton; GUI thread
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)        // mcp/enabled
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY settingsChanged)                  // mcp/port
    Q_PROPERTY(State state READ state NOTIFY stateChanged)       // Off, Starting, Listening, Stopping, Error
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(hwio::gui::ErrorInfo error READ error NOTIFY stateChanged)
    Q_PROPERTY(int activeClients READ activeClients NOTIFY activeClientsChanged)
    Q_PROPERTY(QString address ...) Q_PROPERTY(QVariantList snippets ...) Q_PROPERTY(QVariantList grants ...)
    Q_PROPERTY(hwio::gui::ActivityModel* activity ...) Q_PROPERTY(hwio::gui::ConsentViewModel* consent ...)
    Q_INVOKABLE QString token();  void copyToken();  void regenerateToken();  void retry();
    Q_INVOKABLE QString consentRule(QString category);  void setConsentRule(QString category, bool allow);
    Q_INVOKABLE void revokeGrant(int row);  void copySnippet(int row);  void cancelOperation(DeviceTabViewModel*);
    void shutdown();   // aboutToQuit / destructor: stop the server, deny prompts, forget grants
private:  // destroyed in reverse order
    std::unique_ptr<GuiDeviceProvider> m_provider;  std::unique_ptr<GuiConsentBroker> m_consent;
    ActivityModel* m_activity;  std::unique_ptr<ToolsContext> m_tools;
    std::unique_ptr<McpServer> m_server;  std::unique_ptr<HttpServer> m_http;   // only while on
};
```

- **Lifecycle:** `enabled` → `TokenStore(defaultPath()).loadOrCreate()` → new `Registry` + `registerTools` → new
  `McpServer` (activity sink = `ActivityModel`, broker = `GuiConsentBroker`, instructions as the CLI) →
  `ToolsContext::attachServer` → `HttpServer{port, token}.listen()` → Listening, and one `provider.refresh()`.
  Off / port change / quit: `http.close()`, `server.stop()`; when `callsDrained` (or after 5 s) the server is
  detached and destroyed on the next event-loop turn; a pending start then follows. `McpServer::stop()` is final,
  so every start builds a new server; `ToolsContext` and the provider live as long as the controller.
- **Catalog:** `flash catalog_id` uses the GUI's CHESTER catalog (`FirmwareServices::catalog`, found as a child of
  `AppViewModel`); none → `usage.invalid_argument` as in 0013.
- **Active clients:** the set of client keys seen (activity entries, consent requests) within 60 s plus
  `activeCalls() > 0`; re-evaluated by a 1 s timer only while listening.

```cpp
class GuiDeviceProvider final : public hwio::mcp::tools::IDeviceProvider {
    // list(): J-Link tabs with a probe (id "jlink:<SN>", display name = tab title, profile, Open when the tab
    //   has a session else Available, connection, openedByMcp = !openedBy.isEmpty()), then the probes of
    //   ProbeScanner::last() without a tab (Available / InUseByOtherProcess + owner). Never enumerates.
    // refresh(done): ProbeScanner::scanNow(); `done` on the next `scanned` (≤ 64 waiters; firstScanPending()
    //   while no scan result exists yet).
    // open(): synchronous (R17): a tab with a session → it; a user tab without one → connectDevice() (ADR-0242);
    //   else appendTab() (full → internal.session_limit), openedBy = client name, configure(profile, target,
    //   serial), speed_khz / auto_reconnect applied to the new tab's ConnectionViewModel, connectDevice().
    //   An open error closes a new tab again and is returned.
    // close(): AI-opened tab → AppViewModel::closeTabObject(tab, true) (queued); user tab → mcp.tool_not_allowed
    //   (English call-site text: "The tab of <id> was opened by the user; only the user can close it.").
    // beginBusy/endBusy → setBusy("ai", text, confirmText, cancel) / clearBusy("ai").
    // marker → tab.consoleLog()->appendMarker(Shell, text); setAiCommandHint → tab.aiCommandActive.
    // devicesChanged: tab rows inserted/removed, sessionChanged, configurationChanged, a changed scan.
};

class GuiConsentBroker final : public hwio::mcp::IConsentBroker {
    // Rules: SettingsStore mcp/consent/<id> = ask|allow. Grants: clientKey → categories (ADR-0241), dropped on
    // clientGone, Revoke, token regeneration, server stop. Allowed at once when every category is allowed by a
    // rule or a grant. Otherwise queued (≤ 8 + the one shown, else mcp.consent_timeout); 120 s from showing
    // (injectable for tests); Deny → mcp.consent_denied (detail: tool). The cancel functor withdraws it.
};

class ConsentViewModel : public QObject {    // the shown prompt: title, client, action, device, tab index,
    // detail rows [{label, value, warning}], remember label, seconds left, queued count;
    // Q_INVOKABLE deny(), allowOnce(), allowForClient(), showDevice(), placeWindow(QWindow*), alert()
};

class ActivityModel : public QAbstractListModel, public hwio::mcp::IActivitySink {  // ring of 1000, newest first
    // roles: time, client, tool, device, arguments, result, isError, duration; Q_INVOKABLE copyAll(), clear()
};
```

### Data flow and threading

Everything on the GUI thread (ADR-0097). `HttpServer` runs on the main event loop; tool handlers use the tab's
session and `ConsoleLog` directly; long jobs use the server's `Offloader` (ADR-0162). The probe list comes from the
existing `ProbeScanner` thread (no new enumeration). The prompt is QML on the same thread. No new threads.

### Settings keys added (ADR-0025)

| Key | Type | Default |
|---|---|---|
| `mcp/enabled` | bool | `false` |
| `mcp/port` | int 1024–65535 | `47800` |
| `mcp/consent/flash` · `…/erase` · `…/memory-write` · `…/uicr` · `…/connect` | `ask`\|`allow` | `ask` |

The token is **not** a setting: `<AppConfigLocation>/mcp-token` (ADR-0102, `TokenStore::defaultPath()`), shared with
`hwio mcp --token-file` when pointed there. The diagnostic report lists `mcp/*` keys only (no token file).

### Security (ADR-0102, ADR-0188, ADR-0182 — enforced by `hwio_mcp` / `hwio_mcp_tools`, unchanged)

127.0.0.1 only; bearer token (constant-time, 250 ms delay and close on failure); Host / Origin / Sec-Fetch-Site
checks; body and connection limits; `read_memory to_file` only as a plain file name in the dump folder. The GUI
adds nothing that bypasses them: it never logs the token, never puts it in the activity list, and shows it only on
Show / Copy. C1 tests a wrong token, a foreign Origin and a wrong Host against the GUI's server.

### Error cases (GUI-originated)

| Case | Code | Shown |
|---|---|---|
| Port in use / listen failed | `mcp.port_in_use` / `mcp.listen_failed` | section state text + Retry; rail dot red |
| Token file unreadable / wrong owner | `mcp.token_file` | section ErrorPanel with "Regenerate token" |
| Consent prompt not answered / queue full | `mcp.consent_timeout` | to the AI; activity row |
| Tab limit reached on `connect` | `internal.session_limit` | to the AI; no new tab |
| User closed the AI's tab mid-call | `mcp.device_not_open` | to the AI |
| `disconnect` of a user's tab | `mcp.tool_not_allowed` | to the AI |

### Limits

Activity 1000 entries · consent queue 8 (+1 shown), 120 s per shown prompt · refresh waiters 64 · grants until
client end / 30 min idle / revoke / token change / server stop · active client = request in the last 60 s ·
active-client timer 1 s while listening · shutdown drain 5 s.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| GUI consent | ask + remember per client / always ask / settings only | ask, remember per client | ADR-0101 |
| Activity display | markers + panel + indicator / panel / markers | markers + MCP section list + rail indicator | ADR-0105 |
| Shared shell | no lock / lock input | no lock, hint in the input | ADR-0105 |
| Where the prompt appears | own on-top window / + notification / native notification / in-window dialog | own always-on-top window + taskbar/dock alert | ADR-0108 |
| Keep running in the tray | no / optional / always with MCP | no, closing the window quits | ADR-0108 |
| Token storage | owner-only file / INI / keychain | owner-only file | ADR-0102 |
| Port | 47800 fixed / 8090 / next free | 47800, error if busy | ADR-0102 |
| Backend error language over MCP (2026-11-03) | amend ADR-0103 / English default text / extend `hwio::Error` | amend: GUI language | ADR-0240 |
| Client identity for grants, R6 (2026-11-03) | `clientInfo.name` + more forgetting / legacy sessions only | `clientInfo.name`, forgotten on token change, server stop, revoke | ADR-0241 |
| `connect` on a probe with a user tab (2026-11-03) | connect that tab / refuse | connect that tab, it stays the user's | ADR-0242 |
| Structure and BLE (2026-11-03) | extension + J-Link first / AppViewModel owns / wait for 0012 | extension, J-Link now, BLE step D1 after 0012 | ADR-0243 |
| Minor defaults (2026-11-03): section and layout, rail indicator, AI badge / append / no restore, no tab switch, Esc = Deny, snippets, keys, 60 s, activity in memory, dump folder, Cancel in the device bar, screenshots instead of mockups | accept / change | accepted | ADR-0244 |

## Implementation steps

Phase A — controller and provider
- [ ] A0 Prerequisite check: 0013 phases A–C, 0025 and 0004/0005/0006/0021/0028 implemented (otherwise stop and report)
- [ ] A1 `ActivityModel`, `ClientSnippets` + tests
- [ ] A2 `GuiConsentBroker` + `ConsentViewModel` (rules, grants, queue, timeout, withdrawal) + tests
- [ ] A3 Shell additions (`openedBy`, `aiCommandActive`, `appendTab`, restore skip) + `GuiDeviceProvider` + tests
- [ ] A4 `McpController` + `registerMcp` (lifecycle, port change, token, state, active clients, shutdown) + tests

Phase B — QML
- [ ] B1 `McpSection.qml` (state, port, token, snippets, permissions, grants, activity)
- [ ] B2 `ConsentWindow.qml` (flags, screen, alert, Show device, countdown); `McpRailIndicator.qml`; "AI" badge;
  device-bar Cancel; console input hint
- [ ] B3 Czech translations (0 unfinished); accessibility (names, focus order, prompt announcement); screenshots
  from Qt Quick tests (light/dark, EN/CS, 960 × 600) and a short entry in `plan/gui/mockups/index.html`

Phase C — verification
- [ ] C1 GUI integration test (GTest, in-process HTTP client against the controller's server, fake J-Link):
  token/Origin/Host refusals; `list_devices`; `connect` → prompt → Allow once → AI tab with badge; `flash` on the
  tab → prompt → Allow once → markers, busy, activity, result; Deny; a user tab → `disconnect` refused; two clients
  (one denied, one allowed); 32 in-flight `wait_for_state` calls keep the event loop responsive (timer latency
  < 50 ms)
- [ ] C2 Manual (by the user): Claude Code against the GUI on Linux, Windows, macOS; token file permissions per OS;
  port in use; regenerate token; quit while an AI flash runs; the prompt over other apps with the main window
  minimised and on another virtual desktop (Windows 10/11, macOS, GNOME X11, GNOME and KDE Wayland; record where
  focus is not given)
- [ ] C3 Update roadmap, `0004-mcp.md`

Phase D — after 0012 is in `main`
- [ ] D1 Bluetooth tabs and recently seen BLE devices in `GuiDeviceProvider` (ADR-0243) + tests

## Tests and verification

- **Unit (GTest):** activity ring at 1000, roles, copy, clear; snippets (port, token, masked, CLI path, JSON valid);
  broker: rules (allow → no prompt), grants per client and category, revoke, `clientGone`, forget on token change /
  stop, queue overflow, timeout (short injected timeout), withdrawal of the shown and of a queued prompt, several
  categories in one prompt; provider: ids and list from tabs and the scanner, `connect` opens an AI tab with
  `openedBy`, connects a user tab (ADR-0242), limit → `internal.session_limit`, `disconnect` of a user tab →
  `mcp.tool_not_allowed`, tab closed mid-call → `mcp.device_not_open`, busy set/cleared, markers, hint, AI tabs not
  in `ui/tabs`; controller: off → listening → error (port in use) → retry; port change restarts; regenerate
  replaces the token (old one refused, new one accepted); disable while a call runs; shutdown.
- **Qt Quick Test (`tst_mcp.qml`):** section states (off, listening, error) in light/dark; permission radios write
  the keys; consent window per category (detail rows, countdown, Esc = Deny, flags, no transient parent, buttons);
  rail indicator states; layout at 960 × 600 in Czech; screenshots.
- **Integration:** C1.
- **Sanitizers / tidy / qmllint / translations / clang-format** as in M3 (`linux-tsan` runs the GUI tests).
- **Manual (by the user, before Done):** C2.

## Risks and open points

- **Focus on Windows / Wayland:** the OS may show the prompt without focus (ADR-0108); C2 records it.
- **Prompt bursts:** queue limit, per-client grants and Always allow keep them bounded.
- **Programmatic tab opening** bypasses the Choose-device page; it uses the same `configure` / `connectDevice`
  path, so validation and errors are the same.
- **Merge with 0012:** small conflicts in the shell files listed above (ADR-0243).
- **Claude Desktop** cannot use the GUI's HTTP server without a bridge; its snippet runs `hwio mcp` separately.
