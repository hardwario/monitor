# 0045 — User modes: Basic, Advanced, Expert

- **Status:** In progress (approved 2026-11-12, Matej Strnad; all decisions D-1..D-7 confirmed)
- **Milestone:** 2.0.0-rc.4 (ADR-0359)
- **Author:** Matej Strnad, Claude
- **Related ADRs:** ADR-0359 (three modes, scope, rc.4, first start), ADR-0360 (expert switch replaced by Expert mode,
  amends ADR-0236), ADR-0361 (hidden setting revealed for one visit), ADR-0362 (upgrading users choose too; section
  "Advanced" renamed "Expert")
- **Depends on:** 0033 / ADR-0236 (expert maintenance actions), PageRegistry settings sections

## Context

Settings has grown to seven sections (General, Console, J-Link, nRF Util, AI assistants (MCP), Advanced, About) with
about 35 rows. Most users need the language, the theme and the console file; J-Link speed, RTT latency, the MCP
server or the remote support server only confuse them. One switch already hides dangerous maintenance actions
("Show expert maintenance actions", `advanced/expertActions`, ADR-0236). The user wants modes — Basic, Advanced,
Expert — that decide how much the app shows, mainly in Settings.

## Goals / Non-goals

Goals
- One setting **Mode** (Basic / Advanced / Expert) that decides which Settings sections and rows are visible.
- Expert mode replaces the "Show expert maintenance actions" switch (Erase everything, Recover, LTE MCU maintenance).
- The mode is asked once at the first start and can be changed at any time in Settings › General.
- Nothing a user needs is unreachable: an error or a guide that links to a hidden setting still shows it.

Non-goals
- Modes do not change values: a hidden setting keeps its value and keeps working (a custom J-Link library path stays
  in use in Basic mode).
- No change to the CLI, MCP tools or the backend (the mode is a GUI presentation setting).
- Other pages (Console toolbar, Flash forms, menus) are not simplified in this plan, except the expert maintenance
  actions that ADR-0236 already gates.

## User experience

### Mode selector

Settings › General, first row, visible in every mode:

**Mode** — `Basic` · `Advanced` · `Expert` (segmented control), with one line under it that describes the selected
mode:

- Basic: "The essentials: language, look and console files."
- Advanced: "Also J-Link, nRF Util, AI assistants and console options."
- Expert: "Everything, including timing options and expert maintenance actions."

Changing the mode takes effect at once (sections appear / disappear, the Flash page's maintenance updates).

### What each mode shows

| Section / row | Basic | Advanced | Expert |
|---|---|---|---|
| General: Mode, Language, Theme, Reopen tabs at start | ✓ | ✓ | ✓ |
| Console: Font size, Show timestamps, Wrap long lines, Save console to log files, Log folder | ✓ | ✓ | ✓ |
| Console: Suggest commands, Read the command list on connect, Remember commands + Clear history | | ✓ | ✓ |
| J-Link: Library, Default speed, Auto-reconnect | | ✓ | ✓ |
| J-Link: RTT latency, Reset on connect | | | ✓ |
| nRF Util (section) | | ✓ | ✓ |
| AI assistants (MCP) (section) | | ✓ | ✓ |
| Expert section (today "Advanced"): Support server | | | ✓ |
| About: Version, Updates (automatic check), Command line, Licences, Links | ✓ | ✓ | ✓ |
| About: Updates › include pre-releases | | ✓ | ✓ |
| Flash › Maintenance: Erase everything, Recover, LTE MCU picker | | | ✓ |

The "Show expert maintenance actions" switch is removed (Expert mode is that switch). The protected-MCU hand-off of
ADR-0236 (Recover shown once for that MCU) works in every mode.

### Links from errors and guides (D-4, ADR-0361)

When an error's suggestion, a wizard or the Flash page opens Settings at a section or row that the mode hides, that
section (or row) is shown for this visit with a note above it: *"Shown because it was needed. It is part of the
Advanced mode."* (or Expert). Leaving Settings hides it again.

### First start (D-3, ADR-0359)

On the first start (no `ui/mode` stored), before the first tab is used, a dialog **How do you want to use HARDWARIO
Monitor?** offers the three modes as large cards (title + the description line), with **Advanced** preselected, and
**Continue**. "You can change it at any time in Settings › General." Closing the dialog keeps the preselection
(and stores it). When
the first-run setup wizard arrives (plan 0008), this becomes its first step.

### Upgrade from rc.3 and older (D-5, ADR-0362)

Upgrading users also see the first-start dialog once. It preselects the migrated mode: Expert when
`advanced/expertActions = true`, otherwise Advanced. Closing the dialog keeps the preselection.

## CLI

None. The CLI has no modes (the mode is GUI-only; CLI output and options are unchanged).

## Design

- **Setting:** `ui/mode`, string, one of `basic` | `advanced` | `expert`, registered in `AppViewModel` next to the
  other `ui/` keys (`SettingKey` with `oneOf` validator). Missing = not chosen yet (first start).
- **Type:** `enum class UiMode { Basic = 0, Advanced = 1, Expert = 2 }` in `app/gui/src/viewmodels/ui_mode.h`
  (Q_ENUM in a QML-registered namespace), with `parseUiMode` / `uiModeId` and `atLeast(UiMode current, UiMode needed)`.
- **Owner:** a small `UiModeController` (QObject, owned by `AppViewModel`, documented parent ownership) reads and
  writes `ui/mode`, computes the preselection for upgrading users (D-5), exposes `mode`, `chosen` (false until the first-start choice) and
  `bool shows(UiMode needed) const`. `SettingsViewModel` and `FlashViewModel` take it by reference.
- **Sections:** `PageRegistry::addSettingsSection(id, title, qml, order, UiMode minimum = UiMode::Basic)`;
  `SettingsViewModel::sections` filters by the mode plus the section revealed for this visit (D-4). Registrations:
  nRF Util, MCP → Advanced; the Expert section → Expert.
- **Rows:** `SettingsRow.qml` gets `property int minimumMode: UiMode.Basic`; its `visible` binds to
  `SettingsViewModel.shows(minimumMode) || SettingsViewModel.revealedRow === rowId`. The per-row minimum is declared
  where the row is (presentation only, no logic in QML); a C++ test reads the QML-declared table through a Qt Quick
  test.
- **Expert actions:** `FlashViewModel::expertActions()` becomes `m_mode.shows(UiMode::Expert)`; it listens to the
  controller's `modeChanged` instead of the store key. `advanced/expertActions` is read only for the upgrade
  preselection and removed from the store once the mode is chosen.
- **Reveal for one visit:** `SettingsViewModel::openAt(section, row)` (used by the existing "scroll to section" entry
  points) sets `revealedSection` / `revealedRow` when hidden by the mode; cleared when the Settings page closes.
- **First start:** `ModeChoiceDialog.qml` in `app/gui/qml/shell/`, opened by `Main.qml` when `!chosen`; it calls
  `UiModeController.choose(mode)`.
- **Threading:** GUI thread only. **Errors:** a failed write of `ui/mode` (read-only settings file) shows the existing
  settings write error; the mode then applies for this run only.
- **Translations:** all new strings in cs, de, pl, sk; glossary entries for the mode names.
- **Docs:** `docs/<lang>/settings.md` (or the existing settings page) — the modes table; `getting-started.md` — the
  first-start dialog; release notes rc.4.

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D-1 | Ship in which release | rc.4 (hold) / rc.5 | rc.4, rc.4 waits (2026-11-12) | ADR-0359 |
| D-2 | Reach | Settings only / Settings + expert actions / whole app | Settings + expert actions (2026-11-12) | ADR-0359, ADR-0360 |
| D-2b | Names | Easy-Medium-Expert / Easy-Standard-Expert / Basic-Advanced-Expert | Basic / Advanced / Expert (2026-11-12) | ADR-0359 |
| D-3 | Where chosen | Settings only / + first run | + first run, default Advanced (2026-11-12); the first-run wizard does not exist yet → first-start dialog | ADR-0359 |
| D-4 | Hidden setting needed by an error or guide | reveal for the visit / switch the mode / hint only | reveal for the visit (2026-11-12) | ADR-0361 |
| D-5 | Upgrading users | migrate silently / also show the first-start dialog | first-start dialog, migrated mode preselected (2026-11-12) | ADR-0362 |
| D-6 | Name clash: Settings section "Advanced" vs mode "Advanced" | rename to "Expert" / "Other" / move Support server | rename to "Expert" (2026-11-12) | ADR-0362 |
| D-7 | Row mapping | the table above / changes | the table above (2026-11-12) | ADR-0359 |

## Implementation steps

- [x] A1 `UiMode`, `UiModeController`, `ui/mode` key, migration from `advanced/expertActions`; unit tests
- [x] A2 `PageRegistry` minimum mode per section, `SettingsViewModel` filtering + mode property + reveal for one
      visit; `FlashViewModel` expert actions from the mode; remove the switch; unit tests
- [x] B1 QML: Mode selector in General, `SettingsRow.minimumMode` on every row of the table, section rename (D-6),
      reveal note; first-start `ModeChoiceDialog`; Qt Quick tests (each mode shows exactly the table's rows;
      reveal; dialog default Advanced)
- [ ] B2 Screenshots tests updated
- [ ] C1 Translations (0 unfinished), glossary, docs ×5, release notes rc.4
- [ ] C2 Verification: full ctest, ASan/TSan, clang-tidy, qmllint, clang-format, check_translations

## Tests and verification

- Unit: mode parsing / invalid stored value → Advanced (and the dialog again); upgrade preselection (switch on →
  Expert, off/missing → Advanced; the old key removed after the choice); `FlashViewModel` expert actions follow the mode; sections filtered per mode; reveal cleared on
  close.
- Qt Quick: for each mode the visible rows equal the table; changing the mode updates Settings and the Flash
  maintenance at once; a deep link to a hidden row shows it with the note; first-start dialog (Advanced preselected,
  Continue stores it, not shown again).
- Manual: upgrade from rc.3 with the expert switch on and off; first start with a fresh settings file; all five
  languages fit in the segmented control and the dialog.

## Risks and open points

- Support instructions ("open Settings › J-Link …") fail for Basic users unless they switch modes; the docs say which
  mode shows each setting, and error links reveal it (D-4).
- A Basic user with MCP enabled earlier keeps the server running without seeing its section; the status bar's MCP
  indicator (plan 0014) still shows it.
