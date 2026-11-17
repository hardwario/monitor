# 0052 — Wizards and content dialogs become views in the app window

- **Status:** In progress (approved 2026-11-17, Matej Strnad; decisions D-1 to D-8)
- **Milestone:** 2.0.0-rc.5
- **Author:** Matej Strnad, Claude
- **Related ADRs:** ADR-0386 (wizards are views of the device tab; supersedes ADR-0066), ADR-0387 (content dialogs
  are views of the page area); unchanged: ADR-0067 (wizard engine), ADR-0384 (scroll hints)
- **Depends on:** plan 0051 (branch `strnad/rc4-issues`; this plan is stacked on it: branch `strnad/no-dialogs`)

## Context

The user asked (2026-11-17) to get rid of dialogs and use the app window instead. Today wizards run in a centred
modal `WizardDialog` of at most 720 × 560 px (ADR-0066): content scrolls early (issue #10), the console cannot be seen
during a wizard, and only one wizard can run in the whole app. Six other content dialogs have the same size limits.

Dialogs in `app/gui/qml` today:

| Kind | Components | This plan |
|---|---|---|
| Wizards | `components/WizardDialog.qml` (all wizards: connect, troubleshooting, LTE update, setup, …) | view in the device tab (ADR-0386) |
| Content dialogs | `dialogs/ReportDialog.qml`, `dialogs/ProbeFirmwareDialog.qml`, `remote/JoinDialog.qml`, `remote/ShareDialog.qml` (a `ConfirmDialog`), `shell/ModeChoiceDialog.qml`, `components/ShortcutOverlay.qml` | view in the page area (ADR-0387) |
| Confirmations | `components/ConfirmDialog.qml` (yes/no, the wizard Cancel question, Leave it erased / Write back / Back) | unchanged: small modal |
| Separate windows | `AlreadyRunningDialog.qml`, `mcp/ConsentWindow.qml` | unchanged (shown before/outside the main window) |
| Native, popups | FileDialog / FolderDialog, menus, tooltips, `AdvancedPopover`, `HelpMenu`, `HistoryPopup`, `SuggestionList`, toasts | unchanged |

## Goals / Non-goals

Goals: wizards and content dialogs use the whole page area; a wizard per device tab; the rest of the app stays usable.
Non-goals: no change of wizard steps, texts or the step engine (ADR-0067); confirmations, native file dialogs and
popups stay; no OS full-screen mode.

## User experience

### Wizards (ADR-0386)

- A wizard opened for a device tab replaces that tab's page area (right of the rail, below the device bar). The
  tab bar, the device bar and the rail stay visible.
- Layout of the view: header (wizard title, step list — compact "Step 2 of 4" below 800 px page width — and ×
  Cancel), content (illustration, text, embedded controls; max content width 880 px, centred; scrolls with the
  ADR-0384 hints), footer pinned at the bottom (Back left; step actions, default button, Cancel right).
- **One wizard per device tab.** Another tab can run its own wizard at the same time. Opening a second wizard in the
  same tab is refused and shows the running one (as ADR-0066 did app-wide).
- **The rail stays usable.** Choosing Console / Flash / … while a wizard runs shows that page; the wizard keeps
  running. The rail gets an entry "Wizard" (icon + title tooltip, badge while an operation runs) at the top of the
  tab's rail items while a wizard is open; choosing it, or the launcher, shows the wizard again where it was. The
  tab in the tab bar shows a small wizard badge.
- Closing a tab with a running wizard asks as today (busy tab).
- Esc = Cancel only while the wizard view has focus; Enter = default button (as ADR-0066).
- The hand-off buttons ("Open console", "Open Flash page") close the wizard and show the page, as today.
- Global wizards (first-run setup) behave as global views (below).

### Content views (ADR-0387)

- Report a problem, Update J-Link firmware, Join support session, Share this session, Choose mode, Keyboard
  shortcuts open as a view in the page area of the current tab (or over the Choose device page / Settings when that
  is current), with a header (title, × Close / Back) and the same content and buttons as today, pinned footer.
- Closing returns to the page that was shown before. Switching tabs while such a view is open closes it, except
  while it runs an operation (report collecting, J-Link firmware update): then it stays with that tab/global slot
  and is shown again on return (J-Link firmware update: the tab switch is blocked, as the probe is busy — same as
  today's non-closable state).
- One content view at a time (app-wide); opening another replaces it unless one runs an operation (then refused,
  the running one is shown).
- Choose mode at first start and the setup wizard: shown in the page area of the Choose device page.

### Confirmations

Unchanged small modal `ConfirmDialog`, now centred over the view that asked.

## CLI

None (GUI only).

## Design

- **Wizard state per tab.** `AppViewModel.wizard` (one `WizardViewModel`) becomes one `WizardViewModel` per
  `DeviceTabViewModel` (`DeviceTabViewModel::wizard()`, Q_PROPERTY) plus one global instance for global wizards
  (`AppViewModel.globalWizard`). `WizardLauncher::launch()` picks the tab's instance; "already open" is checked per
  instance. Tab close destroys its wizard after the existing busy check. The rail's "Wizard" entry is a page id
  `wizard` shown while `tab.wizard.open`.
- **QML.** `WizardDialog.qml` → `components/WizardView.qml` (an `Item`/`Page`, not a `Popup`): same header, content
  `ScrollView` with ADR-0384 scroll bar, fade and `ensureVisible()`, footer. `TabView.qml` / `PageHost.qml` shows it
  for page id `wizard`. A small `components/ViewFrame.qml` (header + scroll content + pinned footer) is shared by the
  wizard view and the content views.
- **Content views.** `AppViewModel.overlayView` (string id: `report`, `probeFirmware`, `join`, `share`, `modeChoice`,
  `shortcuts`, empty) with `openView(id)` / `closeView()`; `Main.qml` puts a `Loader` above `tabStack` /
  `globalPageLoader` in the page area. The six components become `Item`-based views in the same folders (renamed
  `*View.qml`), keeping their object names for tests where possible. Their view models (`AppViewModel.report`,
  `AppViewModel.probeFirmware`, …) are unchanged.
- **Toasts** stay in `Overlay.overlay`. `ConfirmDialog` parents to `Overlay.overlay` as now.
- **Focus.** Opening a view moves focus to its heading (as `focusHeading()` does); closing returns focus to the page.
- **Minimum window 960 × 600, Czech:** footer buttons must fit (Flow wrap allowed in the footer only).

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D-1 | Which dialogs become views | wizards + content dialogs / wizards only / everything | wizards + content dialogs; confirmations stay (2026-11-17) | ADR-0386, ADR-0387 |
| D-2 | Where a view sits | page area of the tab / whole window | page area, tab bar and rail stay (2026-11-17) | ADR-0386 |
| D-3 | Wizards at once | one per tab / one app-wide | one per device tab (2026-11-17) | ADR-0386 |
| D-4 | Rail during a wizard | usable, wizard keeps running / disabled | usable, "Wizard" rail entry (2026-11-17) | ADR-0386 |
| D-5 | Views without a tab | page area / whole window | page area of the current tab (2026-11-17) | ADR-0387 |
| D-6 | Branch | stacked on rc4-issues / same branch / later | stacked, `strnad/no-dialogs` (2026-11-17) | — |
| D-7 | Content view on tab switch | closes unless busy / stays per tab | closes unless busy (2026-11-17) | ADR-0387 |
| D-8 | Finding a running wizard | rail entry + tab badge / banner on every page | rail entry + tab badge (2026-11-17) | ADR-0386 |

## Implementation steps

- [x] A1 Per-tab `WizardViewModel`, global instance, launcher per instance; C++ tests (two tabs run wizards at
      once; second wizard in one tab refused; tab close ends its wizard; the hand-offs)
- [x] A2 `ViewFrame.qml`, `WizardView.qml` in the tab's page area, rail entry and badge, tab badge; remove
      `WizardDialog.qml`; Qt Quick tests (`tst_wizard.qml`, `tst_flash.qml` LTE update, rail switch and back keeps
      the step, Esc / Enter, 960 × 600 Czech fits)
- [x] A3 Content views: report, J-Link firmware, join, share, mode choice, shortcuts; `overlayView`; Qt Quick tests
      updated (`tst_report*`, `tst_remote.qml`, mode choice, shortcuts)
- [ ] A4 Screenshots (`tst_screenshots_*`), docs (all pages that say "dialog" for these, 5 languages), ADR-0066
      status Superseded, design-system.md (views, ViewFrame)
- [ ] C1 Translations (new strings: "Wizard", tooltips; 0 unfinished)
- [ ] C2 Verification

## Tests and verification

All builds and tests run with `nice -n 19 ionice -c 3`: full linux-debug ctest; ASan/TSan for changed tests;
linux-tidy zero findings; `all_qmllint`; `check_translations`; clang-format-21; `tools/check-includes.sh`;
`tools/check-lfs.sh`. Manual: two tabs with wizards at once (fake J-Link), switching to Console during the LTE
update and back, every content view at 960 × 600 in Czech, keyboard only (Tab / Esc / Enter), screen reader names.

## Risks and open points

- Two wizards running hardware operations at once use two probes; the per-tab session already serialises its own
  probe (one session per probe), so no new sharing — but a probe picker in one wizard must not offer the probe of
  another tab (existing rule; verify).
- Large QML change: every Qt Quick test that finds `wizardDialog` changes.

## Implementation notes

Small choices made while implementing (not architectural; listed for the review).

- **A1** The tab owns its `WizardViewModel` (a Qt child, created from `TabContext::wizards`, the application's
  `WizardRegistry`; a tab built without one gets an empty registry). `DeviceTabViewModel::shutdown()` closes the
  tab's wizard first (review V2), so `AppViewModel::removeTab` and the destructor need no own call. The page id
  `wizard` is accepted by `setCurrentPage` only while the wizard is open (also while choosing a device); it is not a
  registered page (no Ctrl+N shortcut, no page view model) and is never saved (`ui/tabs` has no page).
- **A1** Closing a wizard shows the page that was shown before the wizard view was shown the last time (after Console
  → Wizard → Cancel that is Console), unless a hand-off chose another page meanwhile (the hand-off comes before
  `finished`, so "Open console" is kept).
- **A1** Opening a tab's wizard, and refusing a second one in the same tab, makes that tab current and shows its
  wizard view (`raiseRequested` / `openChanged` handled in `AppViewModel::addTab`).
- **A1** Tests: `registerGlobalTestWizard` (the test wizard as `first-run-setup`, not per tab) covers the global
  instance, which no production wizard uses yet.
- **A2** `ViewFrame` is a `FocusScope` with the page background (`Theme.bg`; the fade uses it too). Header, content and
  footer share one centred column of at most 880 px; the scroll bar stays at the right edge of the page area.
- **A2** The wizard footer stays one `RowLayout` (Back left; actions, default button, Cancel right) — at the minimum
  window size the page area is about 900 px wide, so no wrapping was needed in Czech (the test checks every footer
  button lies inside the view).
- **A2** The rail entry and the tab badge use the existing `info` icon: a new Material Symbols glyph would have to
  be downloaded from the pinned upstream commit (ADR-0060), which this session did not do. A dedicated glyph can
  replace it later by adding the file only.
- **A2** While the tab chooses a device the other rail items stay disabled as before; the Choose device page comes
  back when the wizard closes.
- **A2** The Checks step's `CheckList` is created only on a Checks step (a Loader): hidden inside the new layout it
  sized itself from its own width ("recursive rearrange" warnings).
- **A2** The wizard's Cancel confirmation (`ConfirmDialog`) is centred over the wizard view (its parent).
- **A2** Global wizards are shown by `ContentViewHost` in the page area of the current tab (or over Settings); a tab
  switch does not close them (they are app-wide; a second one is refused).
- **A2** `ViewFrame`'s header and footer layouts set `Layout.fillWidth: false` and `Layout.fillHeight: false` (nested
  layouts fill by default), so they keep the centred column width and the content takes all free height; the tests
  check the content height at 960 × 600.
- **A3** `AppViewModel.overlayView` is the id of the open view only while its tab (or the Settings tab) is current;
  internally the view remembers its tab (`QPointer`). The report and the J-Link firmware update open and close their
  views themselves (`ReportViewModel::show()` from the Help menu or the crash notice, the firmware banner), so
  `AppViewModel` follows their state; `openView("report")` starts collecting. `shortcutOverlayVisible` stays as the
  property of the "shortcuts" view (F1, Ctrl+/, tests).
- **A3** "Runs an operation" = the report collecting or saving, the J-Link firmware update writing. While it writes,
  a tab switch is refused with the toast "Wait until the J-Link firmware update has finished." (new string), and the
  rail does not replace the view either; while the report collects a rail click is ignored (its Cancel is in the
  view). Closing the tab of a running view moves the view to the tab shown next.
- **A3** Closing the mode choice in any way (×, Esc, a tab switch, another view) stores the preselection, as Esc did;
  otherwise it would stay unchosen and come back.
- **A3** A tab's wizard that opens closes an idle content view of that tab (the wizard would be hidden beneath it); a
  global wizard does so too.
- **A3** Focus: a view focuses its title heading when it opens, except Join (the code field), Share (Cancel, the
  default), the mode choice (Continue) and the J-Link firmware confirmation (Cancel, ADR-0132) — as the dialogs did.
  Closing a view or the wizard view gives the focus back to the page.
- **A3** Footers: the content views keep their right-aligned `Flow` (wraps when narrow); the J-Link firmware update
  uses a row [Cancel] [Update] so that Tab goes from Cancel to Update as before. Share now names its buttons
  `remoteShareStart` / `remoteShareCancel` (it is no `ConfirmDialog` any more); the other object names are kept
  (`reportDialog`, `probeFirmwareDialog`, `remoteJoinInputDialog`, `remoteShareDialog`, `modeChoiceDialog`,
  `shortcutOverlay`). Content widths: wizard and report 880 px, shortcuts 720 px, join / share / mode choice 560 px.

