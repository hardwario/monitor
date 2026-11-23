# 0056 — The setup, connect, troubleshooting and flash wizards as views (2.0.0-rc.5)

- **Status:** Approved (2026-11-23, Matej Strnad: "Approve all twelve" — D-1 to D-12 as recommended)
- **Milestone:** 2.0.0-rc.5
- **Author:** Matej Strnad, Claude
- **Related ADRs:** proposed: ADR-0391 (the wizards of plans 0008–0010 and the probe picker follow the views model),
  ADR-0392 (the mode question is part of the setup wizard), ADR-0393 (local-device wizards refuse remote tabs),
  ADR-0394 (DiagWorker requests per requester). Builds on: ADR-0386, ADR-0387 (plan 0052), ADR-0067, ADR-0071,
  ADR-0252, ADR-0290, ADR-0291, ADR-0292, ADR-0297, ADR-0300, ADR-0359, ADR-0362, ADR-0382, ADR-0384.
- **Depends on:** plan 0052 (on `origin/main` since 2026-11-23), plans 0008, 0009, 0010, 0035 (this branch
  `strnad/wizards-rc5`, merged with v2.0.0-rc.4), plan 0045 (user modes), plan 0044 (remote support).

## Context

Branch `strnad/wizards-rc5` (HEAD `cda16b2`, merge base with `origin/main` `c983fa4`) has the probe picker in wizard
mode (plan 0035) and three wizards written for the **modal, single-instance** `WizardDialog` (ADR-0066): first-run
setup (plan 0008), connect and troubleshooting (plan 0009), flash (plan 0010). `origin/main` (`0c8638e`) has plan 0052:
wizards are views of the device tab, one `WizardViewModel` per tab plus `AppViewModel.globalWizard` (ADR-0386);
content dialogs are views (ADR-0387). The user decided on 2026-11-23 to ship the wizards as views in rc.5 (below).

### Verified facts on this branch (HEAD)

| Fact | Where |
|---|---|
| One app-wide `WizardViewModel`, `AppViewModel::wizard()`; hand-off, hand-off-error and the ADR-0300 session signals are connected once, to that instance | `app_view_model.cpp:239-243` |
| The single-instance additions of this branch: signals `handOffError`, `openedOnTab`, `closedOnTab` | `wizard/wizard_view_model.h:130-134` |
| ADR-0300 chain state is one pointer for the whole app: `m_chainTab`; `openChainedWizard`, `onWizardClosedOnTab`, `onHandOffError` | `app_view_model.h:373`, `app_view_model.cpp:1428-1480` |
| `applyEmbeddedOptions` (ADR-0290) and `Wizard::aboutToClose` keep their state per `WizardViewModel` / per wizard: they port to many instances unchanged | `wizard_view_model.cpp` (`m_optionsItem`, `m_tabClosing`), `wizard/wizard.h` |
| Wizards with their own probe picker: connect (`connect_wizard.cpp:474`), troubleshooting (`troubleshooting_wizard.cpp:236`), flash (`flash_wizard.cpp:409`) and setup (read-only, `setup_wizard.cpp:529`), all via `AppViewModel::makeProbePicker` (`app_view_model.h:220`, comment "one per open wizard (ADR-0066)"), refreshed by `probeUsageChanged` (`app_view_model.h:320`) | |
| Wizard-mode picker: rows used by another tab are not selectable, "a modal wizard does not switch tabs" | `probe_picker_view_model.h` header comment, `probe_picker_view_model.cpp:231-233` |
| Connect, flash and troubleshooting change the tab: `chooseAnotherProbe()` (`connect_wizard.cpp:470`, `flash_wizard.cpp:522`) and `configure()` (`connect_wizard.cpp:569`, `troubleshooting_wizard.cpp:607`, `flash_wizard.cpp:459`); both end a remote session (`device_tab_view_model.cpp:287-289`, `:738-742`) | |
| The setup wizard is global (`perTab = false`, `setup_wizard.cpp:963-966`); `StartupCheck` creates it itself and uses `*m_app.wizard()` | `startup_check.cpp:79-92, 103-110` |
| `SetupWizard::connectDevice` closes itself and starts `connect-device` on the current tab ("one wizard at a time") | `setup_wizard.cpp:779-789` |
| The setup probe step starts the J-Link firmware update through `ProbeFirmwareViewModel::requestForSerial` | `setup_wizard.cpp:484-487` |
| Embedded controls reach the single instance through `AppViewModel.wizard` | `qml/wizard/SetupStep.qml:102,122`, `qml/wizard/ProbePickerStep.qml:21` |
| `ErrorPanel` hides "Help me fix this" while any wizard is open (`!AppViewModel.wizard.open`) and has `showWizardButton` (plan 0036 P5) | `components/ErrorPanel.qml:25,177` |
| `ProbeFirmwareDialog`: on failure "Help me fix this" replaces [Close] (plan 0036 P5) | `dialogs/ProbeFirmwareDialog.qml:177-192` |
| The mode choice is a dialog opened by `Main.qml` while `!uiMode.chosen` | `Main.qml:168-182` |
| Every `DiagWorker` request of the same kind replaces a pending one (`workers/diag_worker.cpp:103-124`, whoever submitted it); the dropped caller gets `requestDropped` (queued) and resubmits at once (connect `connect_wizard.cpp:105-110`, troubleshooting `troubleshooting_wizard.cpp:102`, flash `flash_wizard.cpp:157-162`, setup `setup_wizard.cpp:108-113`, `StartupCheck` `startup_check.cpp:42-47`) | |
| The wizards follow their tab: connect listens to `stateChanged`, `sessionChanged`, `busyChanged` (`connect_wizard.cpp:146-148`); troubleshooting and flash to `sessionChanged`, `busyChanged` (`troubleshooting_wizard.cpp:131-132`, `flash_wizard.cpp:201-202`); a check or flash ends when the session goes away (`diagnose_runner.cpp:255-260`, `flash_wizard.cpp:1042-1054`); their operations hold the tab busy (`diagnose_runner.cpp:292-304`, `flash_wizard.cpp:915, 996`) and refuse when another page holds it (`flash_wizard.cpp:828`) | |
| `WizardId` is an enum in `libs/core` (`error.h:148-155`); `WizardRegistry::add` refuses a second registration of an id (`wizard_registry.cpp:15-21`), the first wins | |
| A remote tab is never `ChoosingDevice`: `chooseAnotherProbe()` turns it into a J-Link tab first; `isRemote()` stays true after the session ended until then | `device_tab_view_model.cpp:728-742` |
| References to the single instance in tests: 67 lines added by this branch in Qt Quick tests and QML (`tst_connect_wizard.qml` 29, `tst_flash_wizard.qml` 19, `tst_setup_wizard.qml` 12, `tst_wizard.qml`, `tst_screenshots.qml`, …) plus about 60 uses of the `Popup` API (`.opened`, `.footer`, `.contentItem`) in those files; 159 `wizard()` calls in `app/gui/tests/wizard/*_test.cpp` | |

### Verified facts on `origin/main`

| Fact | Where |
|---|---|
| `WizardLauncher::launch` picks the tab's instance or the global one; a busy instance refuses and is raised | `wizard/wizard_launcher.cpp:47-61` |
| Each tab owns its `WizardViewModel`; `shutdown()` closes it first; page id `wizard`; the page shown before comes back when it closes | `device_tab_view_model.{h,cpp}` (`createWizard`, `onWizardOpenChanged`) |
| `AppViewModel` connects only `handOff` of each tab's wizard (`showWizard` on open/raise) | `app_view_model.cpp:862-879` |
| `ContentViewHost` shows the content view, else the **global wizard**, on the current tab's page area; the tab's own `WizardView` is shown only while no content view or global wizard is shown — a global wizard hides every per-tab wizard | `shell/ContentViewHost.qml:18-19`, `shell/PageHost.qml:259` |
| The mode choice is the content view `modeChoice`, opened by `syncOverlay()` while `!uiMode.chosen`; a global wizard opening closes an idle content view, and closing the mode view stores the preselection | `app_view_model.cpp:255-260, 275, 757-758, 782-783` |
| `WizardView` renders a control only on `Embedded` steps, passes only `vm`, creates `CheckList` only on Checks steps; Choice cards = title + subtitle + selected | `components/WizardView.qml:126-140, 207-222` |
| `ErrorPanel` inside `wizardContent` hides "Help me fix this" and the primary action (ADR-0382) | `components/ErrorPanel.qml` |
| `registerGlobalTestWizard` registers the test wizard as `first-run-setup` at test time (Qt Quick) or after `registerBuiltInPages` (C++): on this branch the real `SetupWizard` is registered first, so the add fails silently | `tests/support/test_wizard.cpp:246`, `tst_gui.cpp:551`, `tst_wizard.qml:250`, `viewmodels/wizard_test.cpp:73` |
| ADR-0184 plain-text allow-list names `components/WizardView.qml` | `tests/cmake/qml_plaintext_allowlist.txt` |
| Release notes of rc.5 | `packaging/release-notes/v2.0.0-rc.5.md` |

A dry-run merge (`git merge-tree --write-tree HEAD origin/main`) conflicts in 15 files: `app/gui/CMakeLists.txt`,
`components/ErrorPanel.qml`, `components/WizardDialog.qml` (deleted on main), `dialogs/ProbeFirmwareDialog.qml`
(deleted on main), `app_view_model.{h,cpp}`, `wizard/wizard.h`, `tests/support/test_wizard.{h,cpp}`,
`tests/tst_wizard.qml`, `tests/viewmodels/wizard_test.cpp`, `i18n/hwio_{cs,de,pl,sk}.ts`. `wizard_view_model.{h,cpp}`,
`wizard_launcher.*`, `device_tab_view_model.*` and `tst_gui.cpp` merge textually but not semantically (they call
`AppViewModel::wizard()`, which main removed).

### What breaks on a plain merge

1. Plan 0052's assumptions are wrong on this branch: four registered wizards have their own probe picker; the setup
   wizard is a production global wizard; the global test wizard collides with it.
2. At the first start both the mode view and the setup wizard want the page area: the setup wizard opening closes the
   mode view, which silently stores the preselected mode.
3. Connect, flash and troubleshooting opened on a joined remote tab call `chooseAnotherProbe()` / `configure()` and end
   the support session.
4. Two wizards in two tabs (or a tab wizard and the setup wizard) now run checks at once: their `DiagWorker` requests
   replace each other's pending request and each resubmits on `requestDropped` — a ping-pong of queued calls on the GUI
   thread for as long as a third request runs.
5. The setup wizard (global) hides every tab's wizard while it is open; a per-tab wizard opened meanwhile is invisible.
6. The connect, troubleshooting and flash wizards lose their modality on their own tab: with the device bar and the
   rail usable (plan 0052), the user can choose another device, connect / disconnect or flash from the Flash page
   while one of them waits on a step. Plan 0052's review of "what the dialog used to block" only knew the LTE guide.

## Decisions already made by the user (2026-11-23, Matej Strnad)

| # | Decision | ADR |
|---|---|---|
| U-1 | First start: the mode question ("How do you want to use HARDWARIO Monitor?", plan 0045) becomes the **first step of the setup wizard**, as ADR-0359 already says. The separate mode view is no longer shown at the first start. | ADR-0392 |
| U-2 | Remote-support tabs (joined, no probe): the connect, flash and troubleshooting wizards **refuse to open with a short message** saying they work on a local device. | ADR-0393 |
| U-3 | The three wizards and the probe picker are converted to the views model of plan 0052 and ship in 2.0.0-rc.5. | ADR-0391 |

## Goals / Non-goals

Goals: this branch merged with `origin/main`; every wizard of plans 0008–0010 runs as a view (per tab: connect,
troubleshooting, flash; global: setup); U-1 and U-2; tests, translations and docs updated; rc.5 ready.

Non-goals: no change of wizard steps or texts beyond U-1/U-2 and the decisions below; no change of the views model
(ADR-0386/0387) itself; no BLE changes (the wizards already guard Bluetooth tabs); no CLI change; the LTE update guide
keeps its behaviour (see Risks).

## User experience

### First start (U-1, D-1 … D-5)

The setup wizard is a global view: it covers the page area of whatever tab is current (device tab, Choose device page
or Settings); the tab bar, the device bar and the rail stay visible (plan 0052). With D-1 (A) its first step,
**Welcome**, reads top to bottom:

```
Welcome to HARDWARIO Monitor                                   (heading, as today)
<welcome body, as today>
Language  [System default ▾]                                   (as today, ADR-0254)
How do you want to use HARDWARIO Monitor?                      (sub-heading, only while the mode is not chosen, D-2)
 ┌ Basic ──────────── <description> ┐
 ├ Advanced ───────── <description> ┤   one selected (accent border), Up/Down + Space, screen reader: radio buttons
 └ Expert ─────────── <description> ┘
You can change it at any time in Settings › General.
                                        [Skip setup]   [Next]   [Cancel]
```

Card names, descriptions, the question and the hint are plan 0045's texts (already translated). Clicking a card only
selects it. The selection is applied and stored (`UiModeController::choose`) when the step is left in any way: [Next],
[Skip setup], × / Esc / [Cancel] (D-4 (A)). The language combo re-translates the cards live.

**(a) Fresh install** (no `ui/mode`, no `setup/completedVersion`): the main window opens with one Choose device tab;
the setup wizard opens at once over its page area (D-5 (A): no wait for the J-Link check), Welcome with the mode cards,
**Advanced** preselected. The user chooses a mode, [Next] goes on to J-Link Software … Done; [Connect a device] on Done
opens the connect wizard on the current device tab. No separate mode view appears.

**(b) Upgrade from rc.3 or older** (no `ui/mode`, no `setup/completedVersion`; rc.3 had no setup wizard): as (a), but
**Expert** is preselected when `advanced/expertActions` was on (ADR-0362); leaving the step removes that key (ADR-0360).
Restored tabs are behind the setup view; the tab bar shows them.

**(c) rc.4 user who already chose a mode** (`ui/mode` set, no `setup/completedVersion`, which rc.4 did not write): the
setup wizard opens once at Welcome **without** the mode block (D-2 (A)); the mode does not change. The release notes
say every rc.4 user sees the setup wizard once (ADR-0071 unchanged).

Other cases:

| Situation | What happens |
|---|---|
| Settings file read-only (`settings.newer_schema`) | No setup wizard (ADR-0071, unchanged). No mode question: the preselection applies for this run, nothing is stored (rc.4 showed a mode dialog whose write failed at every start). |
| `setup/completedVersion` present but `ui/mode` missing or invalid (settings edited, failed write) | D-3 (A): handled like a first start — the setup wizard opens at Welcome with the mode block. |
| J-Link Software missing at a later start (`completed`, `showAtStart`) | "J-Link Software is needed" variant as today; no mode block (the mode is chosen). |
| Help › Setup wizard… | Welcome without the mode block (D-2 (A)); the mode is changed in Settings › General › Mode. |
| Setup started from an error ("Help me fix this", troubleshooting's USB hand-off) | No Welcome step (plan 0008), so no mode block. |

**Settings › General › Mode** (segmented control, plan 0045) is unchanged and stays the only place to change the mode
later. The content view **ModeChoiceView** (`shell/ModeChoiceView.qml`, id `modeChoice`) is removed with D-3 (A); it was
never reachable from Settings (only from `syncOverlay()`).

### Remote-support tabs (U-2, D-7)

On a joined remote tab (`DeviceTabViewModel::isRemote()`, also after the session ended, until the tab chooses a local
device) the connect, flash and troubleshooting wizards do not open. The app shows the toast:

> This wizard works only with a device connected to this computer, not in a support session.

(new translatable string, constant text, context `WizardLauncher`). Entry points and what they do on a remote tab with
D-7 (A):

| Entry point | Result |
|---|---|
| Help › Connect a device…, Troubleshoot connection…, Flash firmware (guided)… (act on the current tab) | toast, nothing opens |
| "Help me fix this" in an `ErrorPanel` of the remote tab whose error names one of the three | toast, nothing opens |
| Flash page "Guide me" (a remote tab's Flash page is the remote one; the button is shown only for application firmware) | toast, nothing opens |
| Choose device "Guide me" / "Connect a device…" | not reachable: a remote tab is never choosing a device |
| Setup wizard Done › [Connect a device] while a remote tab is current | toast, the setup wizard has closed |
| A chained hand-off (ADR-0297/0300) into one of the three | cannot start on a remote tab (no wizard runs there) |
| Help › Setup wizard… | opens (global, does not touch the tab) |

### Wizards as views (U-3)

| Wizard | Instance | Shown |
|---|---|---|
| Connect a device, Troubleshooting, Flash firmware (guided), LTE update | the tab's `WizardViewModel` | in that tab's page area (`WizardView` in `PageHost`), rail entry "Wizard", tab badge "Wizard: <title>" (plan 0052) |
| First-run setup | `AppViewModel.globalWizard` | over the page area of the current tab or Settings (`ContentViewHost`), on every tab while open; no rail entry |

- While a tab's wizard view is open the tab shows the wizard instead of the page; the device bar and the rail stay
  usable; Console / Flash / … can be shown and the wizard keeps running; "Wizard" in the rail or the tab badge shows it
  again at the same step (plan 0052).
- Tabs run their wizards independently. A second wizard in the same tab is refused and the running one is shown.
- With D-6 (A): while the setup wizard is open, a per-tab wizard is refused with the toast "Finish or close the setup
  wizard first." and the setup wizard stays shown (new string). Help › Setup wizard… while tab wizards run is allowed:
  the setup view covers the page areas; the tab wizards keep running underneath, keep their tab badges, and are shown
  again when the setup wizard closes.
- A hand-off between wizards of one tab (troubleshooting → connect, connect → troubleshooting topic, flash → LTE
  update, …) replaces the wizard view without showing the previous page in between (D-8 (A)).
- A hand-off from a tab's wizard to the setup wizard (troubleshooting's failed USB check → setup at its USB step): the
  tab's wizard closes, its probe-only session is released when idle (ADR-0300), the setup view opens. There is no way
  back into the closed tab wizard (unchanged; ADR-0297).
- The J-Link firmware update started from the setup probe step opens the "Update J-Link firmware" content view over the
  setup view (a content view wins in `ContentViewHost`); when it closes the setup wizard is shown again at its probe
  step. On failure "Help me fix this" replaces [Close] only when the troubleshooting wizard can start now
  (`WizardLauncher::canStart`, see Design), i.e. not while the setup wizard is open (D-6 (A)) and not on a remote tab.

### The wizard's own tab while it is open (D-11)

With D-11 (C): while connect, troubleshooting or flash is open on a tab, that tab's **Choose another device**
(device-bar title, Ctrl+Shift+D, the tab's Choose device page) is refused with the toast "Close the wizard first: it
works on this tab's device." (new string); the wizard owns the tab's device choice. Connect / Disconnect / Reconnect in
the device bar (and F4 / Ctrl+R) stay usable: the wizards follow `sessionChanged` / `stateChanged` (a running check or
flash holds the tab busy, which already disables the action; between steps a connect promotes the wizards'
probe-only session, ADR-0300, and the wizard shows the new state). A flash started from the Flash page while a wizard
waits on a step holds the tab busy; the wizard's own operations refuse while another page holds it (as today). An AI
assistant (MCP) taking the tab holds it busy (`ai`) the same way. The LTE update guide and the setup wizard are not
affected.

### Two tabs, each with a probe picker ("In use (tab N)")

Each wizard has its own wizard-mode picker (up to 8 tabs + the global setup wizard = 9 pickers, all on the one
`ProbeScanner`). The rule stays (D-9 (A)): a probe that a tab uses shows "In use (tab N)" and cannot be selected in any
wizard's picker; a wizard never takes another tab's probe. When tab 1's wizard configures its tab with probe P
(`configure` → `configurationChanged` → `probeUsageChanged`), tab 2's wizard picker re-reads the scan in the same event
turn: P becomes "In use (tab 1)", its selection is cleared and [Next] is disabled until another probe is selected. A
wizard's own tab is also "another tab" for its picker (unchanged: the connect and flash wizards return the tab to its
picker first, ADR-0252).

## CLI

None.

## Design

### Class and file mapping (this branch's single-instance API → per-tab + global)

| This branch (single instance) | After this plan |
|---|---|
| `AppViewModel::wizard()` / `AppViewModel.wizard` | removed (main): `DeviceTabViewModel::wizard()` / `tab.wizard`; `AppViewModel::globalWizard()` / `AppViewModel.globalWizard` |
| `connect(m_wizard, handOff / handOffError / openedOnTab / closedOnTab)` once | `AppViewModel::addTab` connects `handOff` (main) and `handOffError` of each tab's wizard; the constructor connects `handOff` and `handOffError` of the global one |
| `WizardViewModel::openedOnTab` / `closedOnTab` | removed with D-8 (A): `DeviceTabViewModel::onWizardOpenChanged` (main) calls `keepWizardSession()` on open and `releaseWizardSessionWhenIdle()` on close unless a chain is pending |
| `AppViewModel::m_chainTab`, `onWizardClosedOnTab` | removed (D-8 (A)); `DeviceTabViewModel::setWizardChainPending(bool)` / `wizardChainPending()` |
| `AppViewModel::openChainedWizard(id, tab, context)` | kept; sets `tab->setWizardChainPending(true)` for a per-tab target on the same tab, queued `m_launcher->openForTab`, clears the flag after the open (or the failed open releases the session, as today) |
| `WizardViewModel::handOffError` | kept (per instance) |
| `WizardViewModel::applyEmbeddedOptions`, `Wizard::aboutToClose`, `m_tabClosing` | kept unchanged (per instance) |
| `WizardLauncher` (one instance check) | main's per-instance launcher + `WizardDescriptor::localDeviceOnly` (U-2), the D-6 rule and `canStart()` |
| `StartupCheck` uses `*m_app.wizard()` | uses `m_app.globalWizard()` to start the setup wizard; waits while `AppViewModel::anyWizardOpen()` (JLinkNeeded only, see below) |
| `makeProbePicker(QObject* parent)` | unchanged signature; comment: one per open wizard, at most 9 |
| `WizardDialog.qml` (branch changes: ADR-0290/0291/0292, checks wrapper) | `WizardView.qml` (main) gets them: control on any step that names one, `applyEmbeddedOptions` on load and on `changed`, `enabled: modelData.enabled !== false` on actions; main's Checks `Loader` is kept (it replaces the branch's wrapper) |
| `ProbeFirmwareDialog.qml` (branch: P5) | `ProbeFirmwareView.qml` (main) gets P5 with `AppViewModel.launcher.canStart(error.wizard, -1)` |
| `ErrorPanel.qml` (branch: `showWizardButton`, `!AppViewModel.wizard.open`) | main's `inWizard` + the branch's `showWizardButton`; `!AppViewModel.wizard.open` is dropped (a busy tab instance refuses and shows its wizard, ADR-0386) |
| `ModeChoiceDialog.qml` (branch) / `ModeChoiceView.qml` (main) | removed (D-3 (A)); `AppViewModel` loses the `modeChoice` view id and its `syncOverlay` / `closeView` branches |
| Embedded controls use `AppViewModel.wizard` | every registered control declares `property WizardViewModel host: null`; `WizardView` binds it (see "Embedded contract") |

### `WizardLauncher` (U-2, D-6)

```cpp
struct WizardDescriptor {            // wizard/wizard.h
    WizardId id = WizardId::Troubleshooting;
    bool perTab = true;
    // ADR-0393: needs a device connected to this computer; refused on a remote tab.
    bool localDeviceOnly = false;
    std::function<Wizard*(QObject* parent)> create;
};

class WizardLauncher : public QObject {  // wizard/wizard_launcher.h
public:
    // `toast` = AppViewModel::showToast (refusal messages); may be empty (tests).
    WizardLauncher(const WizardRegistry& registry, WizardViewModel& global,
                   std::function<DeviceTabViewModel*(int tabIndex)> tabAt,
                   std::function<void(const QString&)> toast, QObject* parent = nullptr);
    Q_INVOKABLE [[nodiscard]] bool canOpen(const QString& wizardId) const;        // registered (unchanged)
    // Registered, has its tab, not refused by U-2 / D-6, its instance free: launch() would open it now.
    Q_INVOKABLE [[nodiscard]] bool canStart(const QString& wizardId, int tabIndex = -1) const;
    ...
};
```

`launch()` order: not registered → false; per-tab without tab → false (log); per-tab and `tab->isRemote()` and
`localDeviceOnly` → toast (U-2 text), false; per-tab while `m_global.isOpen()` → toast "Finish or close the setup wizard
first.", `emit m_global.raiseRequested()`, false (D-6 (A)); instance busy → raise (main); else create and start. Every
opening path goes through `launch()` (`open`, `start`, `openForTab`, chained opens, `DeviceTabViewModel::openWizard`,
`SetupWizard::connectDevice`). Registrations: connect, troubleshooting and flash set `localDeviceOnly = true`; LTE
update and setup do not. With D-12 (A), `tabAt(-1)` on the Settings tab returns the last device tab
(`m_lastDeviceTab`) instead of null, so Help › Connect a device… and Setup › [Connect a device] from the Settings tab
open on that tab (today they do nothing there). The D-6 refusal does not apply to an open that comes from a remote LTE
update request (`RemoteShareViewModel`, ADR-0363): `DeviceTabViewModel::openWizard(id, fromRemote)` passes it through
`TabContext::openWizard` to `WizardLauncher::openForTab(..., fromRemote = true)` (part of D-6).

D-11 (C): `DeviceTabViewModel::chooseAnotherProbe()` (all its callers: device bar, shortcut, MCP hand-over) refuses
with the toast while `m_wizard->isOpen()` and the open descriptor is `localDeviceOnly`, except when the wizard itself
calls it (`chooseAnotherProbeForWizard()`, used by the connect and flash wizards instead of `chooseAnotherProbe()`).

### `DeviceTabViewModel` (ADR-0300 per tab, D-8 (A))

```cpp
void setWizardChainPending(bool pending);           // AppViewModel::openChainedWizard
[[nodiscard]] bool wizardChainPending() const noexcept;
```

`onWizardOpenChanged()` (main): on open → `keepWizardSession()`; on close → unless `m_wizardChainPending`:
restore the page shown before (main) and `releaseWizardSessionWhenIdle()`; while a chain is pending the page stays
`wizard` (no flicker, focus stays in the view) and the session is kept. The queued open clears the flag; a failed open
clears it, restores the page and releases the session. The flag is also cleared by `shutdown()`. Not emitted for the
global instance (it has no tab); a tab → setup hand-off is not a chain (setup is global), so the tab releases its
session as today.

### `StartupCheck` (ADR-0071 table unchanged; D-3, D-5)

```
completed = setup/completedVersion present AND ui/mode chosen           (D-3 (A))
run(): not registered or read-only        -> None (unchanged)
       !completed                          -> Welcome at once, no J-Link check (D-5 (A))
       else                                -> the jlink-software check, then JLinkNeeded or None (unchanged)
```

`StartupCheck` keeps creating the `SetupWizard` itself (it passes `Options::mode`, which the registry's factory
cannot) and starts it on `m_app.globalWizard()` instead of `m_app.wizard()`. The "another wizard is open" wait (0035 § C) applies only to the JLinkNeeded decision and waits while
`AppViewModel::anyWizardOpen()` (global or any tab's), using a new signal `AppViewModel::wizardsOpenChanged()` emitted
from every instance's `openChanged`.

### Mode block in the setup wizard (U-1, D-1 … D-4)

- `SetupWizard` gets `Q_PROPERTY(bool modeChoice READ modeChoice NOTIFY changed)` (true on Welcome while
  `!uiMode->chosen()` at the wizard's start), `Q_PROPERTY(QVariantList modes …)` (= `UiModeController::modes()`),
  `Q_PROPERTY(int selectedMode …)` (preselection at start, `UiModeController::preselection()`) and
  `Q_INVOKABLE void selectMode(int mode)`. The mode is applied with `UiModeController::choose()` when Welcome is left
  ([Next], [Skip setup]) and from `aboutToClose()` (×, Esc, Cancel), only when `modeChoice` was shown; a write error is
  reported as today (`writeFailed`, Settings shows it). The mode is snapshotted at start so it does not vanish when the
  user changes Settings › General › Mode meanwhile.
- `qml/wizard/SetupStep.qml` shows the cards below the language row when `vm.modeChoice` (the card component is the
  delegate of the deleted `ModeChoiceView`, moved to `components/ModeCards.qml`; object names `modeCard_<n>`,
  `modeChoiceHint` kept for the tests).
- `AppViewModel`: `syncOverlay()` no longer opens a mode view; `knownView()` loses `modeChoice`; `closeView()` loses
  its mode branch. `UiModeController` comment updated ("the first-start question is the setup wizard's Welcome").

### Embedded contract

Every embedded control (registered: `probe-picker`, `setup-step`, `wizard-error`, `firmware-source`, `flash-summary`,
`phase-progress`, `lte-update`, tests' `test-embedded`) declares `property WizardViewModel host: null`. `WizardView`
sets it with a `Binding` next to the existing `vm` binding. `SetupStep.qml` and `ProbePickerStep.qml` use `host` instead
of `AppViewModel.wizard`. A `wizard_coverage_test` case loads every registered embedded URL and checks the property
exists (a missing one would only warn at run time). `WizardView` loads the control whenever `step.embedded` is non-empty
(ADR-0292) below checks and choices; `applyEmbeddedOptions(item)` on `loaded` and on `host.changed` (ADR-0290).

### `DiagWorker` (D-10 (A), ADR-0394)

`DiagWorker::newRequester()` hands out a `std::uint64_t` token (a counter from 1; 0 = none, as today); each wizard and
`StartupCheck` takes one in its constructor and puts it into `ChecksRequest::requester`. A new request replaces a
pending request of the same kind **and the same requester** (requester 0 keeps the old same-kind rule); `maxQueued` 4 → 12 (8 tabs +
global wizard + `StartupCheck` + report collect + save). A full queue still drops the oldest (`requestDropped`). The
report requests (`Collect`, `Save`) keep "same kind" replacement (one report view). Header comment and limits updated.

### Test support

- Test wizards are registered **before** `registerBuiltInPages` (first registration wins), in C++
  (`wizard_test.cpp`, already so for `connect-device` on this branch) and in `tst_gui.cpp::createApp()`:
  `registerGlobalTestWizard` is called there when the test file is `tst_wizard` (argument check as for
  `realConnectWizard`); `tst_setup_wizard` and the other files get the real `SetupWizard`. `TestSupport::
  registerGlobalTestWizard()` (runtime) is removed. Both `register*TestWizard` functions `qFatal` when `add()` fails, so
  a collision cannot pass silently.
- `TestSupport` gets `tabWizard(index)` (main) and `globalWizard()`; C++ fixtures get `WizardViewModel& tabWizard(app,
  index = current)` and use `app.globalWizard()` for setup.

### Error cases

No new `hwio::Error` codes. Refusals are toasts (U-2, D-6); a failed write of the mode is the existing
`UiModeController::writeFailed`.

### Limits

Wizard instances: ≤ 8 per-tab + 1 global; probe pickers ≤ 9; `DiagWorker` queue 12; chain flag one per tab.

## Decisions requiring user confirmation

The user's decisions U-1 … U-3 are recorded above (2026-11-23). Open:

| # | Question | Options | Recommendation (one line why) | Decision (by user) | ADR |
|---|---|---|---|---|---|
| D-1 | Form of the mode question inside the setup wizard | (A) on the Welcome step, below the language combo; (B) its own Choice step "Mode" before Welcome | (A): the user picks the language before reading the mode texts, and the step list keeps "Welcome" first | **as recommended** (2026-11-23) | ADR-0392 |
| D-2 | When the setup wizard asks the mode | (A) only while `ui/mode` is not chosen; (B) at every run from Welcome, current mode preselected | (A): Settings › General › Mode is the place to change it; repeat runs stay short | **as recommended** (2026-11-23) | ADR-0392 |
| D-3 | Mode not chosen but setup already completed (edited / broken settings) | (A) `StartupCheck` treats it as not completed → setup at Welcome; delete `ModeChoiceView`; (B) keep `ModeChoiceView` only for this case; (C) nothing: preselection for the run, not stored | (A): one path asks the mode, a whole view and its tests go away | **as recommended** (2026-11-23) | ADR-0392 |
| D-4 | Mode when Welcome is left by ×, Esc, Cancel or [Skip setup] | (A) the selected card is stored (as ADR-0362 for the dialog); (B) stored only by [Next]/[Skip setup]; × leaves it unchosen and the question comes back next start | (A): same rule as rc.4, the visible Settings do not change on the next start | **as recommended** (2026-11-23) | ADR-0392 |
| D-5 | Waiting for the J-Link check before the setup wizard at the first start | (A) open at once when not completed (the check matters only for the J-Link-needed variant); (B) keep waiting up to 5 s | (A): without the mode view the first seconds would otherwise show an unexplained Choose device page | **as recommended** (2026-11-23) | ADR-0392 |
| D-6 | A per-tab wizard is asked for while the setup wizard is open | (A) refused with "Finish or close the setup wizard first.", setup shown — except support's remote LTE update request (ADR-0363) on a shared tab, which opens (setup stays shown, tab badge); (B) opens and its tab shows it above the setup view, setup shown on other tabs; (C) closes the setup wizard | (A): the global view would otherwise hide it, and support must not be refused by the customer's setup; (B) needs a second precedence rule in `ContentViewHost` | **as recommended** (2026-11-23) | ADR-0391 |
| D-7 | How the remote-tab refusal (U-2) appears | (A) toast from every entry point, nothing hidden; (B) toast from the Help menu, in-page buttons ("Help me fix this", Flash "Guide me") hidden on remote tabs; (C) the wizard view opens with one info step and [Close] | (A): one code path in the launcher; the user asked for a message, not a hidden button | **as recommended** (2026-11-23) | ADR-0393 |
| D-8 | Where the ADR-0300 chain state lives | (A) per tab (`DeviceTabViewModel` flag; the tab keeps/releases its session from its own wizard's open state); (B) `AppViewModel` keeps it, as a set of tabs instead of `m_chainTab` | (A): the tab owns the wizard and the session, and the page does not flicker between chained wizards | **as recommended** (2026-11-23) | ADR-0391 |
| D-9 | Wizard-mode picker rows used by another tab (justified by modality, which is gone) | (A) keep: not selectable, "In use (tab N)"; (B) clicking them switches to that tab, as the tab's picker does | (A): a wizard never takes another tab's probe; switching away from a running wizard on a click is surprising | **as recommended** (2026-11-23) | ADR-0391 |
| D-10 | Concurrent wizards' checks on the one `DiagWorker` | (A) replace only a pending request of the same requester (integer token), queue 4 → 12; (B) leave it (ping-pong while another request runs); (C) one worker per tab | (A): smallest change that removes the ping-pong; one thread stays | **as recommended** (2026-11-23) | ADR-0394 |
| D-11 | The own tab's device actions while connect / troubleshooting / flash is open (no longer modal) | (A) all stay usable (plan 0052 rule), the wizards follow the session; (B) all blocked on that tab (like the J-Link firmware view's `probeFirmwareShown()`); (C) only "Choose another device" refused with a toast, connect / disconnect / reconnect usable | (C): these wizards own the tab's device choice; connect / disconnect are already followed by the wizards and guarded by the busy state | **as recommended** (2026-11-23) | ADR-0391 |
| D-12 | Help menu wizards and Setup › [Connect a device] while the Settings tab is current | (A) open on the last device tab; (B) do nothing (today) | (A): today the menu item silently does nothing there | **as recommended** (2026-11-23) | ADR-0391 |

## Implementation steps

Builds and tests: always `nice -n 19 ionice -c 3`, `--parallel 5` / `-j 5`, one preset at a time, at most 3 streams in
the session (check `pgrep -af 'ninja|clang|ctest'` and `free -g` first). Commit only with the user's go-ahead.

**A. Merge**

- [ ] A1 `git merge origin/main` into `strnad/wizards-rc5` (with the user's go-ahead). Resolve the 11 source
      conflicts: main's structure wins (`WizardView`, `ProbeFirmwareView`, per-tab `WizardViewModel`, `globalWizard`,
      `overlayView`); the branch's additions are carried into it as listed in "Class and file mapping" (WizardView:
      ADR-0290/0291/0292 and disabled actions; ErrorPanel: `showWizardButton` + `inWizard`; ProbeFirmwareView: P5;
      `wizard.h`: both `aboutToClose` / `handOffError` and `cancelAlternative*`; `test_wizard.*`: both constructors'
      parameters; `CMakeLists.txt`: both file lists, without `WizardDialog.qml` / `ProbeFirmwareDialog.qml` /
      `ModeChoiceDialog.qml`; `tst_wizard.qml`, `wizard_test.cpp`: both sides' tests). Verify: no conflict markers
      (`git diff --check`), `git ls-files -u` empty.
- [ ] A1b Compile fix (mechanical, same merge commit): `AppViewModel::wizard()` → the current tab's wizard or
      `globalWizard()` (setup) in sources and C++ tests; `StartupCheck` → `globalWizard()`; `AppViewModel::addTab`
      connects `handOffError`, `openedOnTab`, `closedOnTab` of each tab instance and the constructor those of the global
      one (keeps `m_chainTab` for now); `SetupStep.qml` / `ProbePickerStep.qml` temporarily on
      `AppViewModel.globalWizard` / `AppViewModel.currentTabViewModel.wizard`. Verify: `linux-debug` builds all targets.
- [ ] A2 Translations of the merge, method that loses nothing: for each language take main's `.ts` as `ours`, add the branch's messages that main lacks (a small
      scratchpad script over both XML files, or `lconvert` with both inputs after checking how it treats duplicates),
      then `update_translations`. Check with a scratchpad script (not committed)
      that every finished entry (context, source, comment) of either parent is finished in the result with the
      translation of one parent, listing entries whose two parents differ (resolved by hand, main's text by default)
      and contexts renamed by 0052 (`WizardDialog` → `WizardView`, `ProbeFirmwareDialog` → `ProbeFirmwareView`):
      moved, not lost.
- [ ] A3 Pass criterion of the merge commit: `linux-debug` configures and builds all targets; `ctest` passes except
      the suites listed in the commit message as ported in B/C (expected: `tst_connect_wizard`, `tst_flash_wizard`,
      `tst_setup_wizard`, `tst_screenshots*`, the global test of `tst_wizard`, `tst_settings` mode tests); every suite
      that exists only on `origin/main` passes; the A2 check reports no lost translation. The failing list goes into the
      plan's notes and each entry is closed by a B/C step.

**B. Model port (U-3)**

- [ ] B1 `DeviceTabViewModel`: chain flag, keep/release from `onWizardOpenChanged`, no page restore while a chain is
      pending (D-8 (A)); remove `WizardViewModel::openedOnTab/closedOnTab`, `AppViewModel::m_chainTab`,
      `onWizardClosedOnTab`. C++ tests: chain within a tab keeps the session and the page; two tabs chaining in the same
      event turn; tab → setup hand-off releases the tab's session.
- [ ] B2 `WizardLauncher`: `localDeviceOnly` registration flags (no refusal yet), D-6 rule with the remote LTE
      exemption, `canStart()`, toast callback, D-12 `tabAt(-1)`. C++ tests: per-tab refused while setup is open (toast,
      setup raised); remote LTE request opens anyway; setup opens while a tab wizard runs; Help menu from the Settings
      tab.
- [ ] B3 `StartupCheck`: `globalWizard()`, `anyWizardOpen()` + `wizardsOpenChanged()` wait (JLinkNeeded only); tests.
- [ ] B4 Embedded contract: `host` on every control, `WizardView` binding, `SetupStep.qml` / `ProbePickerStep.qml`
      use it; `WizardView` ADR-0292 loading and ADR-0290 options; coverage test.
- [ ] B5 `ErrorPanel`, `ProbeFirmwareView` P5 with `canStart`.
- [ ] B6 `DiagWorker` requester token (D-10 (A)), the five callers take one; `DiagWorkerTest` cases: two requesters
      keep their pending requests; same requester replaces; requester 0 keeps the old rule; full queue at 12.
- [ ] B7 D-11 (C): `chooseAnotherProbe()` refused on the wizard's tab (toast), `chooseAnotherProbeForWizard()` for the
      connect and flash wizards; C++ tests: device-bar choose / Ctrl+Shift+D refused while connect is open, allowed
      after it closed and while the LTE guide is open; disconnect during an idle Cable step and connect during Check
      are followed by the wizard; a Flash page flash during an idle flash-wizard step makes the wizard's flash refuse.
- [ ] B8 Test support: global test wizard registered first for `tst_wizard` (`createApp`), `qFatal` on a failed
      registration, `TestSupport::globalWizard()`, C++ `tabWizard(app)`. Verify: `tst_wizard` global test passes against
      the test wizard, not `SetupWizard`.
- [ ] B9 Port the C++ wizard suites to `tabWizard(app)` / `globalWizard()` (`connect_wizard_test.cpp`,
      `troubleshooting_wizard_test.cpp`, `flash_wizard_test.cpp`, `setup_wizard_test.cpp`, the branch's cases in
      `flash_view_model_test.cpp`); each suite green on its own.
- [ ] B10 Port `tst_wizard.qml` and `tst_components.qml` (`wizardDialog` / Popup API → `wizardView` /
      `globalWizardView`, footer object names; main's `tst_wizard.qml` is the pattern); green.
- [ ] B11 Port `tst_connect_wizard.qml`; green.
- [ ] B12 Port `tst_flash_wizard.qml` and the branch's cases in `tst_flash.qml`; green.
- [ ] B13 Port `tst_setup_wizard.qml` (global view) and `tst_remote.qml`; green.
- [ ] B14 Port `tst_screenshots.qml`, `tst_screenshots_flash.qml`, `tst_screenshots_ble.qml` (wizard steps as views);
      green; changed images under Git LFS.

**C. The user's decisions**

- [ ] C1 U-1: mode block in `SetupWizard` + `SetupStep.qml` (`components/ModeCards.qml`), `StartupCheck` (D-3, D-5),
      remove `ModeChoiceView.qml` and the `modeChoice` view; tests: fresh install, rc.3 upgrade with and without
      `advanced/expertActions`, rc.4 user, read-only settings, completed-but-unchosen, × / Esc / Skip setup store the
      selection (D-4), Help › Setup wizard… has no mode block, no mode view at start. Replace the tests of the removed
      view: `tst_settings.qml` `test_modeChoiceDialog` and `test_modeChoiceDialogLayout` (→ the Welcome step's cards,
      same layout checks at 960 × 600 in five languages), `tst_screenshots_flash.qml:277-283` (→ a Welcome screenshot);
      the `AppViewModelTest` cases on `syncOverlay` / `overlayView == "modeChoice"` if any.
- [ ] C2 U-2: refusal in `launch()` with the toast (D-7); tests: each of the three wizards from the Help menu, an
      `ErrorPanel` and the Flash page on a joined fake remote tab (also after the session ended) refuses, nothing opens
      and the session is untouched; setup and LTE update unaffected.
- [ ] C3 Two tabs with wizard pickers: tests for "In use (tab N)" across wizards (D-9) and two wizards running checks at
      once (D-10).

**D. Translations, docs, plans**

- [ ] D1 `update_translations`; translate the new strings (U-2 toast, D-6 toast, any moved mode texts) in cs, de, pl,
      sk per `i18n/GLOSSARY.md` (wizard = průvodce / Assistent / kreator / sprievodca; support session = relace vzdálené
      podpory …); `check_translations` 0 unfinished.
- [ ] D2 Docs in en, cs, de, pl, sk: `getting-started.md` (Modes: the question is the first step of the setup wizard;
      "Guides and other views": the setup, connect, troubleshooting and flash wizards as views; setup hides tab wizards
      while open), `troubleshooting.md` and `flashing.md` (wizards open in the tab), `remote-support.md` (the three
      wizards are not available on a support-session tab); screenshots via `tst_screenshots*` (Git LFS).
- [ ] D3 `packaging/release-notes/v2.0.0-rc.5.md`: the wizards (new in rc.5), the mode question moved into the setup
      wizard, rc.4 users see the setup wizard once.
- [ ] D4 Plans and ADRs: ADR-0391…0394 status per the user's answers; amendment lines in ADR-0359, ADR-0362
      (→ ADR-0392), ADR-0300, ADR-0252 (→ ADR-0391), ADR-0071 (→ ADR-0392 for D-3/D-5), ADR-0386 (→ ADR-0391: the D-6
      exception to "tabs run wizards independently" and D-11), ADR-0387 (→ ADR-0392: "Choose mode" is no content view
      any more), ADR-0068 (→ ADR-0394: the queue rule); amendment notes in plans 0008,
      0009, 0010, 0035, 0045 and an erratum note in plan 0052 (its "no registered wizard has its own probe picker" and
      "no production wizard uses the global instance" no longer hold); `plan/gui/design-system.md` (mode cards in the
      setup step); roadmap.
- [ ] D5 Verification (below); plan status Done.

## Tests and verification

Every build and test command below runs exactly as written: lowest priority, at most 5 jobs, one preset at a time,
never two presets in parallel (check `pgrep -af 'ninja|clang|ctest'` and `free -g` before each; fewer jobs when busy).
Pass these rules on to any subagent that builds or tests.

```sh
nice -n 19 ionice -c 3 cmake --preset linux-debug
nice -n 19 ionice -c 3 cmake --build --preset linux-debug --parallel 5
nice -n 19 ionice -c 3 ctest --preset linux-debug --parallel 5 --output-on-failure
nice -n 19 ionice -c 3 cmake --build --preset linux-debug --parallel 5 --target all_qmllint
nice -n 19 ionice -c 3 cmake --build --preset linux-debug --parallel 5 --target update_translations
nice -n 19 ionice -c 3 cmake --build --preset linux-debug --parallel 5 --target check_translations
nice -n 19 ionice -c 3 cmake --preset linux-tidy
nice -n 19 ionice -c 3 cmake --build --preset linux-tidy --parallel 5          # zero findings
git ls-files -z -- '*.h' '*.cpp' | xargs -0 nice -n 19 ionice -c 3 clang-format-21 --dry-run --Werror
tools/check-includes.sh; tools/check-lfs.sh --untracked

# then, one after the other (not in parallel):
SAN_TESTS='WizardTest|AppViewModelTest|DeviceTab|DiagWorker|StartupCheck|SetupWizard|ConnectWizard|TroubleshootingWizard|FlashWizard|ProbePicker|ProbeFirmware|tst_wizard|tst_setup_wizard|tst_connect_wizard|tst_flash_wizard|tst_settings|tst_shell|tst_remote|tst_probe_firmware'
nice -n 19 ionice -c 3 cmake --preset linux-asan
nice -n 19 ionice -c 3 cmake --build --preset linux-asan --parallel 5
nice -n 19 ionice -c 3 ctest --preset linux-asan --parallel 5 --output-on-failure -R "$SAN_TESTS"
nice -n 19 ionice -c 3 cmake --preset linux-tsan
nice -n 19 ionice -c 3 cmake --build --preset linux-tsan --parallel 5
nice -n 19 ionice -c 3 ctest --preset linux-tsan --parallel 5 --output-on-failure -R "$SAN_TESTS"
```

The test names in `SAN_TESTS` are patterns; the implementer checks them with `ctest -N` first and records the exact
list in the plan's notes.

New tests (beyond the ported ones): listed in B1–B8 and C1–C3. The ADR-0184 allow-list keeps `components/WizardView.qml`
only (no new rich-text file).

Manual checks (linux-debug, fake J-Link with two probes, fake BLE, a fake relay for remote):

1. Fresh settings: the setup wizard opens at once over the Choose device page; mode cards; × stores Advanced; next
   start opens setup again without the cards.
2. rc.3 settings with `advanced/expertActions=true`: Expert preselected; rc.4 settings: setup without cards.
3. Two tabs: connect wizard in tab 1 and flash wizard in tab 2 at the Probe step; connecting tab 1 to probe P marks it
   "In use (tab 1)" in tab 2's picker; both run their checks at once without log spam.
4. Help › Setup wizard… while tab 2 flashes: setup covers; tab badge stays; closing setup shows tab 2's wizard.
5. Joined remote tab: Help › Connect / Troubleshoot / Flash (guided) and an ErrorPanel "Help me fix this" show the
   toast; the session goes on.
6. Setup probe step → Update J-Link firmware → fail → [Close] (not "Help me fix this" while setup is open).
7. Czech at 960 × 600: the Welcome step with the cards and the footer fit; keyboard only (Tab, Up/Down, Space, Enter,
   Esc); screen reader names of the cards.
8. Connect wizard at the Cable step: Ctrl+Shift+D shows the D-11 toast; Disconnect / Connect in the device bar is
   followed by the wizard; after closing it, Ctrl+Shift+D works.
Not covered here: real hardware on Windows / macOS (rc.5 test round).

## Risks and open points

- **Size of the test port** (about 130 QML lines on the single instance and the Popup API, about 160 C++ calls): mostly
  mechanical, but each Qt Quick file needs a run; budget for flaky waits after the view change.
- **The LTE update guide on a joined remote tab:** not part of U-2. Its entry points (other-connector banner, Flash page
  LTE section, the hand-offs from connect / troubleshooting) do not appear on a remote tab as far as read; the
  implementer verifies with a test and asks the user if one does.
- **Setup over running tab wizards** (D-6 (A) keeps main's "setup covers everything"): a long setup hides a tab's
  running flash; the tab badge and the setup's own steps are the only hints. Acceptable for rc.5; revisit after rc.5
  feedback.
- **D-6 (A) and remote LTE update requests:** on a shared (host) tab, support's LTE update request opens the
  `lte-update` wizard on that tab (`remote_share_view_model.cpp:256`, ADR-0363). While the customer's setup wizard is
  open, D-6 (A) without the exemption would refuse it; the exemption is part of the D-6 row. While setup is shown the
  customer sees the guide only through the tab badge (or after closing setup); support sees its progress as today.
- **rc.4 users see the setup wizard once** (ADR-0071 as decided); the release notes must say so.
- **Translations merge**: four large `.ts` conflicts; A2's check script is the guard against a lost entry.
- **`DiagWorker` requester token**: a requester destroyed with a pending request never gets a result (as today with
  generations); the queue still bounds it.
- **Main's 0052 behaviour kept as is** (not re-decided here): content views close on a tab switch; the rail stays usable
  while a tab's wizard runs; no rail entry for the global wizard.
