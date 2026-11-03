# 0012 — M5 BLE GUI: Bluetooth in Choose device and the device tab

- **Status:** In progress (revised 2026-11-03 against the code of `main` c29ffc8; scope split by ADR-0224; approved 2026-11-03 by Matej Strnad; implementation started 2026-11-03 on `strnad/ble-gui-0012-1facc7`)
- **Milestone:** M5 (plan 2 of 2, ADR-0078)
- **Author:** Matej Strnad (with Claude), 2026-10-10; revision 2026-11-03
- **Related ADRs:** ADR-0043, ADR-0050, ADR-0052, ADR-0058, ADR-0059, ADR-0061, ADR-0063, ADR-0079–ADR-0084,
  ADR-0086, ADR-0089, ADR-0128, ADR-0166, ADR-0181, ADR-0200, ADR-0224, ADR-0225, ADR-0226
- **Depends on:** [0011](0011-m5-ble-backend-cli.md) (BLE backend, `ConsoleSession`, `BleSession`, `BleScanner`,
  fake BLE API; phases A–E done), [0004](0004-m3-gui-shell.md) (shell, picker, device bar, tab restore),
  [0005](0005-m3-console.md) (console page), [0021](0021-gui-feedback-round-1.md) / [0028](0028-gui-feedback-round-2.md)
  (device info, console file names, flash bar, toasts)
- **Moved out (ADR-0224):** the BlePairing wizard, the setup-wizard Bluetooth step, the wizard illustrations and
  the passkey read from the PIB → [0031](0031-ble-pairing-wizard-setup-step.md) (Draft, after 0008 / 0009)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

## Revision 2026-11-03 (what changed against the 2026-10-10 draft)

- Prefix `hio` → `hwio` everywhere (ADR-0181): `hwio::gui`, `hwio ble …`, `hwio chester app pib write`.
- **Scope split (ADR-0224):** the GUI has the wizard framework (0007) but no registered wizard; 0008 (first-run
  setup) and 0009 (connect / troubleshooting) are approved and not implemented. G4 (BlePairing wizard) and G5
  (setup Bluetooth step) depend on them and moved to plan 0031 together with the wizard illustration slots and the
  PIB passkey read. Until then pairing help lives in the ErrorPanel and the device bar (ADR-0224).
- **View-model structure (ADR-0225):** no `ChooseDeviceViewModel`; each tab gets a `BlePickerViewModel` next to its
  `ProbePickerViewModel` and a `transport` property. One `BleHost` + `BleScanner` in `AppViewModel`, created lazily.
- **Minor defaults confirmed (ADR-0226).**
- Later GUI plans are respected: device info / `info show` (0021, ADR-0166) and console file names from the
  firmware identity (0028, ADR-0200) work on any `ConsoleSession` (`DeviceInfoWatcher`, `ShellQueryQueue` already
  take a `ConsoleSession`); the Flash bar (0028) is part of the Flash page, which a BLE tab disables; toasts and
  "Copy" (0028, ADR-0202) unchanged.

## Context

After 0011 the backend and the CLI speak BLE, but the GUI only knows J-Link tabs. The old app had a separate
Bluetooth page (SCAN / CONNECT buttons, a device list with a name filter, RSSI colour, a shell without a log pane,
the warning "If the pairing window does not appear when you connect, try pairing the CHESTER device using the
bluetooth system tool", toasts such as "Bluetooth is turned off"). Its pain points: the selection jumped to the top
whenever a device appeared, scanning needed a click every 5 s, every failure was reported as "probably unpaired",
and there was no help beyond the warning text. BLE in the GUI is a gap for the first release candidate.

## Goals / Non-goals

**Goals (this plan)**
- Choose-device page: **J-Link | Bluetooth** switch; BLE device list with continuous scan (ADR-0081); Connect.
- BLE tabs: title, device bar (BLE fields), Console page bound through `ConsoleSession` (ADR-0079), log pane hidden
  while empty, Reset hidden, Flash disabled with a reason, auto-reconnect option, tab restore (ADR-0083).
- Pairing help without a wizard (ADR-0224): device-bar pairing state, ErrorPanel actions [Pair] / [Open Bluetooth
  settings] / [Open Privacy settings], passkey hint from the profile.
- Fake-BLE end-to-end path for view-model and Qt Quick tests (`HWIO_FAKE_BLE_SCENARIO`, 0011 / ADR-0087).
- EN + CS texts, accessibility, mockup entries for the picker and the BLE tab.

**Non-goals**
- BlePairing wizard, setup-wizard Bluetooth step, wizard illustrations, PIB passkey read → plan 0031.
- Backend changes (0011). If the GUI needs one, stop and amend 0011.
- Firmware update over BLE, BLE settings of the device (passkey change: `hwio chester app pib write`).
- MCP access to BLE tabs (0014).

## User experience

### Choose device (amends ADR-0050 / 0004)

```
┌ Choose a device to connect ────────────────────────────────────────────┐
│  Connect via  [ J-Link | Bluetooth ]                                   │
│ ┌ Device ─────────────────────────────────────────────────────────────┐│
│ │ 🔍 Filter by name…                           ☐ Show all devices      ││
│ │ ▂▄▆█  CHESTER-ABC123   F1:22:33:44:55:66   −58 dBm                   ││
│ │ ▂▄▆_  CHESTER-XYZ789   C0:11:22:33:44:55   −81 dBm  In use (tab 2)   ││
│ │ ▂___  CHESTER-OLD001   …                   not seen recently (dimmed)││
│ │ ◌ Scanning for Bluetooth devices…                                   ││
│ └─────────────────────────────────────────────────────────────────────┘│
│ ┌ Device type ─ [CHESTER ▾] ───────────────────────────── [Connect] ──┐│
└────────────────────────────────────────────────────────────────────────┘
```

- The switch remembers the last choice (`ui/lastTransport`, default `jlink`). The J-Link view is unchanged; a new
  tab opens in the last transport.
- **Bluetooth view:**
  - Scans while visible (ADR-0081): one `BleScanner` scan restarted every 10 s; rows sorted by RSSI, but **the
    selected row keeps its selection and keyboard focus** when the order changes (legacy bug); new rows are
    announced to screen readers at most once per 5 s ("3 devices found").
  - Row: signal icon (4 levels: ≥ −55, ≥ −67, ≥ −80, below; RSSI 0 = unknown → no number), name, id (hidden on
    macOS, where the id is a UUID — shown in the tooltip instead), RSSI, badges "In use (tab N)" (choosing it
    switches to the tab) or "In use by another program (hwio, PID 1234)" (row disabled), "not seen recently"
    (dimmed after 15 s without an advertisement, removed after 60 s).
  - Filter field (substring of the name or id, case-insensitive), "Show all devices" (rows without a profile show
    "Unknown device" in the type column and can be connected only with the device type picked below).
  - Exactly one connectable match → preselected. Enter / double-click = Connect.
  - **Empty state** after the first 10 s scan: "No CHESTER found nearby — Make sure the device is powered and its
    Bluetooth is on." with the profile's power hint and [Show all devices].
  - **Adapter / permission problems** replace the list with an ErrorPanel (`ble.adapter_not_found`,
    `ble.adapter_off`, `ble.stack_unavailable`, `ble.permission_denied`); the panel re-checks every 2 s
    (non-blocking, see Data flow) and disappears when the
    adapter becomes ready. `ble.permission_denied` on macOS has [Open Privacy settings]
    (`x-apple.systempreferences:com.apple.preference.security?Privacy_Bluetooth`). "Help me fix this" appears by
    itself once plan 0031 registers the wizards (ErrorPanel rule of 0007).

### BLE tab

- **Tab title** `CHESTER · CHESTER-ABC123` (ADR-0081) with the Bluetooth icon instead of the probe icon; state dot
  as 0004.
- **Device bar:** `[ᛒ CHESTER-ABC123 · −58 dBm] State text … [Connect | Disconnect | Cancel] [⋯]`
  - States as 0004 with BLE texts: *Connecting…* → *Waiting for pairing — enter the passkey in the system dialog
    (default 123456)* (after `pairingPrompt`; the passkey part is the profile's hint) → *Connected · MTU 247*;
    *Reconnecting… (device not in range)*.
  - ⋯ popover for BLE: Auto-reconnect, "Save console to log file". J-Link fields (speed, latency, reset on open,
    target picker) are not shown. Per-device memory `ble/<id>/…` (LRU 32, as ADR-0052).
- **Rail:** Console; Flash disabled with the tooltip "Flashing needs a J-Link. Open a J-Link tab to flash this
  device." (ADR-0226).
- **Console page:** unchanged UI, bound to the tab's `ConsoleSession`. The log pane is hidden while the Log channel
  is empty (ADR-0082) and the shell pane takes the full width; the "Reset ▾" action is hidden (no `Reset`
  capability); markers "— connected (Bluetooth) —", "— disconnected: out of range —". Batch, history, find,
  suggestions and `info show` (0021) work as on J-Link.
- **Errors:** ErrorPanel per 0004; primary action for `ble.*` codes = Reconnect, except:
  - `ble.not_paired`, `ble.pairing_failed` (ADR-0224): primary [Pair] on Linux / Windows (`BleSession::pair()`,
    then reconnect on success), [Connect] on macOS (pairing happens on connect); secondary [Open Bluetooth
    settings] on Windows (`ms-settings:bluetooth`); the body adds the passkey hint and "Already paired but it does
    not work? Remove the device in the system Bluetooth settings and pair it again."
  - `ble.device_not_found`: [Choose another device].
- **Restore:** BLE tabs are restored Disconnected (ADR-0083); a missing device gives `ble.device_not_found` on
  Connect.

### Shortcuts

None new. Ctrl+T opens a Choose-device tab in the last transport.

### Texts

English source texts as above. Czech is translated in Qt Linguist as part of done (ADR-0027).

## CLI

None new (0011 has `hwio ble scan | console | command | pair`).

## Design

### Modules

```
app/gui/src/
  viewmodels/ble_picker_view_model.*     BLE list model over the shared BleScanner (roles below) — new
  workers/ble_service.*                  owns BleHost + BleScanner (lazy), scan refcount, adapter polling — new
  viewmodels/device_tab_view_model.*     + transport, blePicker, consoleSession(), configureBle(), BleSession
  viewmodels/connection_view_model.*     + BLE fields and BLE option memory
  viewmodels/console_view_model.*        binds ConsoleSession; operations only through DeviceSession
  viewmodels/app_view_model.*            BleService, ui/lastTransport, tab restore with transport, BLE in-use
  viewmodels/page_registry.h             PageDescriptor::needsOperations (Flash)
qml/pages/ChooseDevicePage.qml (+ TransportSwitch), qml/shell/BleDeviceList.qml, DeviceBar.qml / AdvancedPopover.qml
  (BLE variant), NavRail.qml (disabled reason), ConsolePage.qml (log pane hidden while empty)
assets/icons/bluetooth.svg, signal_0..signal_4.svg (ADR-0060 subset additions, LFS rules unchanged: SVG is text)
tests/ viewmodels/ble_picker_test.cpp, viewmodels/ble_tab_test.cpp, tst_ble.qml (fake BLE scenarios)
```

### View models (`hwio::gui`)

```cpp
// Owned by AppViewModel; GUI thread. BleHost created on the first acquire() (ADR-0225).
class BleService : public QObject {
    void acquireScan(); void releaseScan();                  // refcount; scan runs while > 0, restart 10 s
    [[nodiscard]] const std::vector<ble::BleDeviceInfo>& devices() const;
    [[nodiscard]] std::optional<Error> adapterError() const;  // polled every 2 s while scanning or failing
    [[nodiscard]] ble::BleHost& host();                       // creates it
    signals: void devicesChanged(); void adapterChanged();
};

class BlePickerViewModel : public QAbstractListModel {      // child of DeviceTabViewModel
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, RssiRole, SignalLevelRole /*0–4*/, ProfileRole /*id or ""*/,
                 InUseTabRole /*-1*/, InUseOtherRole /*"hwio, PID 1234" or ""*/, StaleRole, ConnectableRole };
    Q_PROPERTY(bool active ...) Q_PROPERTY(QString filterText ...) Q_PROPERTY(bool showAll ...)
    Q_PROPERTY(int selectedRow ...) Q_PROPERTY(bool canConnect ...) Q_PROPERTY(bool scanning ...)
    Q_PROPERTY(bool scanned ...) Q_PROPERTY(hwio::gui::ErrorInfo adapterError ...) Q_PROPERTY(bool hidesIds ...)
    Q_PROPERTY(QString profileId ...) Q_PROPERTY(QString announcement ...)   // a11y, ≤ 1 per 5 s
    Q_INVOKABLE void choose(int row); Q_INVOKABLE void connectSelected();
    signals: void connectRequested(QString id, QString name, QString profileId);
};

// DeviceTabViewModel (0004) amendment
Q_PROPERTY(QString transport READ transport WRITE setTransport NOTIFY transportChanged) // "jlink" | "ble"
Q_PROPERTY(hwio::gui::BlePickerViewModel* blePicker READ blePicker CONSTANT)
[[nodiscard]] device::ConsoleSession* consoleSession() const;   // J-Link or BLE, nullptr if none
// session() stays: the J-Link DeviceSession*, nullptr for BLE tabs (Flash page, Diagnose, Reset use it)
[[nodiscard]] device::BleSession* bleSession() const;           // pairing action
bool configureBle(const QString& profileId, const QString& deviceId, const QString& name);
Q_INVOKABLE QString pageDisabledReason(const QString& pageId) const;  // "" = enabled
```

- `ConsoleViewModel` (0005) follows `tab.consoleSession()` on `sessionChanged`; Reset uses `tab.session()` and
  `canReset` is false without the `Reset` capability; `hasLogChannel` (capability `LogChannel`) drives the log
  pane; `inputDisabledReason` gains "Waiting for pairing…".
- `ConnectionViewModel`: `transport`, `deviceName`, `rssi`, `mtu`, `pairingPending`; BLE options autoReconnect
  (default false, ADR-0226) and logToFile remembered per device id (`ble/<sanitised id>/…`, `ble/recent`, LRU 32).
- Tab restore (0004, ADR-0083): `ui/tabs` entries gain `transport`; BLE entries `{transport: "ble", profile, id,
  name}`; an entry without `transport` is `jlink` (no schema bump needed: the reader accepts both).
- `PageDescriptor::needsOperations` (Flash = true); the rail disables such a page for a tab without the
  `Operations` capability / with transport `ble` and shows `pageDisabledReason` as the tooltip.

### Data flow and threading

- GUI thread: all view models, `BleScanner` and `BleSession` façades. `BleService` (owned by `AppViewModel`)
  creates the `BleHost` (0011) on the first Bluetooth use (Bluetooth view shown, BLE tab connects), so users who
  never use BLE never start the thread or trigger the macOS prompt; it is stopped on quit after all tabs closed
  their sessions (`BleHost::stop`, ADR-0130).
- One `BleScanner` for the app; pickers hold a scan reference while active; scanning stops when no Bluetooth picker
  is visible. `BleSession` auto-reconnect scans by itself (0011).
- The adapter state is read without blocking the GUI thread: every 2 s while a Bluetooth picker is visible,
  `BleService` posts `QueryAdapter{reply}` (0011 command) and reads the reply queue on the next tick (a reply older
  than one tick is dropped); `BleHost::queryAdapter` (blocking) is not used in the GUI.

### Error cases (GUI-originated)

| Situation | Code | Shown as |
|---|---|---|
| Device chosen without a profile ("Show all") | `usage.invalid_argument` | inline: "Choose a device type first." |
| Restored BLE tab, device gone | `ble.device_not_found` (0011) | ErrorPanel + [Choose another device] |
| [Pair] on macOS | `ble.pairing_not_supported` (0011) | not offered (macOS gets [Connect]) |

### Settings keys added (ADR-0025)

| Key | Type | Default |
|---|---|---|
| `ui/lastTransport` | string `jlink`\|`ble` | `jlink` |
| `ui/tabs` | (0004) entries gain `transport`; BLE entries `{transport, profile, id, name}` | — |
| `ble/autoReconnect` | bool | `false` |
| `ble/<sanitised id>/autoReconnect`, `…/logToFile`; `ble/recent` | per device, LRU 32 | — |

### Limits

Scan restart 10 s, stale 15 s, removal 60 s, list ≤ 256 (backend), screen-reader announcements ≤ 1 per 5 s,
adapter re-check 2 s, remembered BLE devices 32, BleHost links ≤ 8 (backend; BLE tabs beyond that
get `internal.session_limit`).

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Where BLE lives in the GUI | connection type in tab / BLE rail page / global page | connection type in the device tab | ADR-0081 |
| Scan in the GUI | continuous while visible / manual button | continuous while visible | ADR-0081 |
| Pairing help | OS + wizard + app-initiated pairing | as decided; wizard part moved to 0031 | ADR-0084, ADR-0224 |
| Passkey hint | default 123456 / label / PIB only | default 123456; PIB read in 0031 | ADR-0084, ADR-0224 |
| Tab restore for BLE | restore / no | restore Disconnected | ADR-0083 |
| Split (2026-11-03) | phase 1 now + G4/G5 to a new plan / phase 2 in 0012 / wait for 0008/0009 | phase 1 now, G4/G5 → 0031 | ADR-0224 |
| Pairing help until the wizard (2026-11-03) | ErrorPanel actions / text only | ErrorPanel actions ([Pair], settings links, hint) | ADR-0224 |
| View-model structure (2026-11-03) | BlePickerViewModel per tab / ChooseDeviceViewModel | BlePickerViewModel per tab, lazy BleService | ADR-0225 |
| Minor defaults (2026-11-03): thresholds −55/−67/−80 dBm, macOS ids hidden, `ui/lastTransport` = jlink, scan restart 10 s, stale 15 s / removed 60 s, Flash text, BLE auto-reconnect off | accept / change | accepted | ADR-0226 |

## Implementation steps

- [x] G0 Prerequisite check: 0011 phases A–E done; 0004, 0005, 0021, 0028 implemented (otherwise stop and report)
- [x] G1 `BleService`, `BlePickerViewModel` (+ selection stability, stale rows, filter, show all, in-use badges,
  announcements, adapter error) + GTest with the fake BLE API
- [x] G2 `DeviceTabViewModel` transport + `consoleSession()` + `configureBle` + BLE session lifecycle and markers;
  `ConsoleViewModel` on `ConsoleSession`; `ConnectionViewModel` BLE fields and option memory; pairing actions;
  `needsOperations` / Flash disabled reason; tab restore with `transport` + GTest
- [x] G3 QML: transport switch, `BleDeviceList`, BLE device bar and popover, rail disabled reason, console log pane
  hidden while empty; icons (ADR-0060 subset additions)
- [x] G6 Mockups (`gui/mockups/index.html`: BLE picker, BLE tab) and design-system component entries
- [x] G7 Accessibility, Czech translation (0 unfinished), `.ts` fresh; Qt Quick tests with the fake BLE scenario;
  screenshots (light/dark, EN/CS, 960 × 600)

(G4, G5 → plan 0031.)

## Implementation notes (2026-11-03, decided by the implementing agent; for review)

- **Pairing from the ErrorPanel** goes through `BleService::pair()` (a `PairDevice` host command with its own
  subscription, as `hwio ble pair`), not `BleSession::pair()`: after `ble.not_paired` the session is Stopped and
  released, and a new session would keep connecting unpaired while the system pairs. On success the tab connects.
- **Device-bar pairing text** is "Waiting for pairing — enter the passkey in the system dialog"; the profile's passkey
  hint ("The passkey is 123456 unless …") is its tooltip and accessible description and the ErrorPanel note, rather
  than "(default 123456)" in the bar (the default is the profile's, not the GUI's).
- **ErrorPanel** gained two generic properties: `customActionText` (replaces the primary action, used for [Pair])
  and `note` (a line below the suggestion, used for the pairing help). macOS keeps the primary [Reconnect].
- **Flash in a Bluetooth tab** is dimmed but stays focusable/clickable, so its tooltip ("Flashing needs a J-Link …")
  works with mouse and keyboard; a click shows the reason as a toast and the page does not change
  (`AppViewModel::showPage`, `DeviceTabViewModel::setCurrentPage` refuse it). A tab that becomes a Bluetooth tab
  while on Flash switches to Console.
- **Markers in a Bluetooth tab** go to the shell channel only, so the log pane stays hidden (ADR-0082).
- **RSSI in the device bar** comes from `BleSession::rssi()`, i.e. the auto-reconnect background scan; without it
  the chip shows the name only.
- **Adapter ErrorPanel** in the picker: primary [Retry] (re-reads the adapter at once); macOS
  `ble.permission_denied` adds [Open Privacy settings].
- **`AppOptions::bluetooth` / `bleApiFactory`**: tests and builds can switch Bluetooth off or inject the API;
  the default is on with `ble::createBleApi` (the fake BLE API with `HWIO_FAKE_BLE_SCENARIO` in test builds).
- **Icons** (ADR-0060 subset): `bluetooth.svg`, `signal_0..4.svg` (Material Symbols `bluetooth`,
  `signal_cellular_N_bar`, pinned commit; `assets/icons/README.md`).
- **Screenshots**: `plan/features/0012-screenshots/` (Git LFS; `tst_screenshots_ble.qml`, 1280 × 800 and 960 × 600,
  light/dark, EN/CS).
- New fake scenario `tests/ble_scenarios/pair_then_connect.json` (first connect `ble.not_paired`, pairing ok).
- Not done here: HIL with a real CHESTER over BLE, manual checks on Windows / macOS (see Tests and verification).

## Tests and verification

- **GTest (fake BLE scenarios from 0011, `tests/ble_scenarios/`):** picker filtering, `showAll`, in-use badges
  (other tab, other process via a held `BleLock`), selection kept while RSSI reorders, stale/removal timing (fake
  clock), adapter error appears/disappears (`adapter_off`, `permission_denied`, `no_adapter`); BLE tab lifecycle
  (connect, pairing prompt state, reconnect, disconnect, close), the tab's `ConsoleLog` survives a BLE reconnect;
  `info show` on Connected; Reset hidden, log pane flag; [Pair] on `not_paired`; restore with and without
  `transport`; Flash page disabled reason.
- **Qt Quick Tests:** picker and BLE tab in light/dark; 960 × 600 in Czech (geometry); keyboard-only connect;
  Flash rail item disabled with tooltip; log pane hidden while empty.
- **`qmllint`** clean; no logic in QML; `linux-tidy` zero findings; clang-format; `check_translations`.
- **Sanitizers:** ASan+UBSan and TSan run the view-model tests with the fake BLE backend (CI).
- **HIL (CHESTER with Bluetooth shell, not run by the implementing agent):** pick from the list and connect;
  pairing from a fresh bond via [Pair] on each OS; one J-Link tab and one BLE tab to the same CHESTER; out of range
  → reconnect.
- **Manual (by the user, before Done):** Linux, Windows 10/11, macOS 13.3+: permission prompt on first Bluetooth
  view (macOS), denied permission → [Open Privacy settings], adapter off, screen-reader announcement.

## HIL results (Linux, 2026-11-03, `main` 8e650a6)

Setup as in [0011](0011-m5-ble-backend-cli.md) "HIL re-run 2026-11-03" (GNOME 50 Wayland, BlueZ 5.85, CHESTER Clime
IAQ v4.0.1, J-Link on the same board). The GUI ran with its own settings/app-data folders, first under XWayland
(dark theme; synthetic input is blocked on GNOME Wayland, so it was driven through AT-SPI and screenshots), then on
Qt's `vnc` platform (light theme, mouse and keyboard through a small RFB client). UI language Czech (system locale).
Run by Claude; the user allowed free use of the device.

Passed:
- [x] Choose device → Bluetooth: list of nearby CHESTERs (filter field, "Show all devices", "not seen recently"
  rows dimmed), "Scanning for Bluetooth devices…"; the new tab opens in the last transport
- [x] Pick from the list and connect: `Connected · MTU 23`, marker "— connected (Bluetooth) —", log pane hidden,
  Reset hidden, tab title `CHESTER · CHESTER <serial>` with the Bluetooth icon; connect 3–30 s
- [x] Console: commands and output, suggestions from the device's command tree, ↑ history; `info show` silent query
- [x] ⋯ popover shows only "Auto-reconnect" and "Save console to log file"
- [x] Disconnect / Connect; with Auto-reconnect a J-Link reset gives "— disconnected: connection lost —" and
  "— connected (Bluetooth) —" 15 s later (uptime restarted)
- [x] Flash rail item dimmed; click → toast "Flashing needs a J-Link. Open a J-Link tab to flash this device."
- [x] "In use (tab 1)" badge in a second tab's picker
- [x] One J-Link tab and one Bluetooth tab to the same CHESTER at once (RTT log + BLE shell both live)
- [x] Tab restore after closing the window: the Bluetooth tab comes back Disconnected (`transport: ble` saved);
  closing the window releases the link (BlueZ `Connected: no`)
- [x] Pairing on connect with an agent that enters the passkey: the reconnect pairs and connects
- [x] Czech texts throughout (device-supplied help such as "Kernel commands" stays English)

| # | Sev. | Finding |
|---|---|---|
| G-H1 | high | **[Pair] is unreachable and the pairing state never shows** on this firmware: 0011 H11 (the NUS CCCD read in `discoverDetails()` needs encryption). A fresh connect without the passkey ends after 30–64 s with `ble.connect_failed` ("Signal is weak…", primary [Reconnect]) instead of "Waiting for pairing — enter the passkey…" and `ble.not_paired` with [Pair]. Fix: 0011 H11. |
| G-H2 | med | **Signal strength flickers and the picker order jumps**: 0011 H14 — rows alternate between "−58 dBm" and no value / `signal_0`, rows without RSSI (including "not seen recently") sort above all others. Fix: 0011 H14. |
| G-H3 | med | **A restored tab whose device is not around gives "Signal is weak", not `ble.device_not_found` with [Choose another device]** (0011 H13; repro: saved tab with an address that does not exist → Connect). Same message right after the bond was removed in the system. Fix: 0011 H13. |
| G-H4 | med | **SIGTERM ends the GUI without closing sessions** (no handler): the BlueZ link stays up (the CHESTER stops advertising), the GUI log loses its buffered tail. A desktop logout sends SIGTERM. Fix: a self-pipe `QSocketNotifier` for SIGTERM/SIGINT/SIGHUP that calls the normal quit path (the CLI likewise, if not yet). |
| G-H5 | low | **Preselection in a busy place:** "exactly one connectable match → preselected" fires on the first advertisement, so whichever CHESTER advertises first stays selected (kept by id) when 20 more appear. Fix: preselect only after the first 10 s scan, or drop an automatic selection the user did not make when a second match appears. |
| G-H6 | low | The GUI log flushes only on warnings, so `gui-*.log` lags minutes behind (and loses the tail on SIGTERM, G-H4); consider flushing info lines with a short timer. |
| G-H7 | note | Setting the command field's text through AT-SPI (`EditableText.SetTextContents`) does not reach `field.text`; Send then sends an empty line. Probably a Qt Quick accessibility limitation; screen readers type keys, so no action unless reported. |

Not run: [Pair] on each OS (blocked by G-H1 on Linux; Windows / macOS manual checks still open), real out of range
(see 0011), screen-reader announcement, adapter off / permission prompts (manual list above).

## Risks and open points

- **Windows pairing UI** may not show a passkey dialog for app-initiated pairing (0011 risk); the ErrorPanel then
  offers "Open Bluetooth settings".
- **Many advertising devices** (offices, fairs) make the "Show all" list long; bounded to 256 and filterable.
