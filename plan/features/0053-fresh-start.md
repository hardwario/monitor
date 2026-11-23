# 0053 — Fresh start: the app opens with one Choose device tab

- **Status:** In progress (approved 2026-11-19, Matej Strnad; D-1 and D-2 decided by the user, D-3 and D-4 as
  recommended, not objected; steps 1–5 implemented 2026-11-19, not yet built)
- **Milestone:** 2.0.0-rc.5
- **Author:** Claude (with Matej Strnad), 2026-11-19
- **Related ADRs:** ADR-0388 (accepted; supersedes the tab-restore part of ADR-0061), ADR-0043 (device tabs),
  ADR-0083 (tab entries with `transport`), ADR-0244 (AI tabs are not saved), ADR-0359–0362 (user modes)
- **Depends on:** nothing

## Context

Since M3 (ADR-0061) the app saves the tab list at quit (`ui/tabs`: profile, target, J-Link serial number or BLE
device) and recreates the tabs at the next start in the *Disconnected* state, without checking whether the hardware
is there. The setting **Settings › General › Reopen tabs at start** (`ui/restoreTabs`) is on by default.

Reported by the user on 2026-11-19: someone who opens the app after some time, with a different J-Link and a
different device, sees the old tabs as disconnected. **Connect** on such a tab tries the saved serial number only and
fails with `probe.not_found` ("No J-Link probe found. The USB cable is disconnected or the J-Link is not powered."),
although a J-Link is connected. The way out (**Choose another probe**) appears only after that error.

The user's decision (2026-11-19): the app starts fresh every time.

## Goals / Non-goals

**Goals**
- At every start the app shows one *Choose device* tab, as on a first start. Nobody meets a tab of hardware that is
  gone.

**Non-goals**
- No probe enumeration at start, no "probe not connected" marking of tabs, no smarter Connect. (Considered on
  2026-11-19 as the alternative; not needed when tabs are not restored.)
- No opt-in setting to bring the tabs back.
- The window geometry, the mode, the language and every other setting stay remembered as today.
- No change to the CLI or to MCP.

## User experience

- Start: one *Choose device* tab. With the first-run setup wizard (plan 0008) or the mode question (plan 0045) the
  order of those is unchanged.
- The switch **Reopen tabs at start** is removed from **Settings › General**; there is no way to get the tabs of
  the last session back (decided by the user 2026-11-19, D-2).

## CLI

None.

## Design

- `AppViewModel`: remove `restoreTabs()`, `saveTabs()`, `parseSavedTabs`, the `Entry` type of the saved list, the
  `m_restoring` flag if nothing else needs it, and the `SettingKey`s `ui/restoreTabs` and `ui/tabs`. The constructor
  adds the one *Choose device* tab directly. Callers of `saveTabs()` (tab added / closed / configuration changed,
  quit) lose that call; the window geometry is still saved.
- `SettingsViewModel`: remove the `restoreTabs` property, getter, setter and the key from its change handling.
- `GeneralSection.qml`: remove the row; `tst_settings.qml` drops `restoreTabsSwitch` from its list.
- Old settings files keep `ui/tabs` and `ui/restoreTabs` unread. To verify in step 1: that the settings store
  tolerates keys it does not know (no warning dialog, no reset); if it does not, stop and ask.
- Comments that mention `ui/tabs` (`gui_device_provider.cpp`, `device_tab_list_model.h`, `device_tab_view_model.*`,
  `app_view_model.h`) are updated; `DeviceTabViewModel::configurationChanged` stays (other listeners).
- Tests: remove or rewrite the cases that use `ui/tabs` / `ui/restoreTabs` in `app_view_model_test.cpp`,
  `ble_gui_test.cpp`, `mcp_provider_test.cpp`, `settings_view_model_test.cpp`; `settings_store_test.cpp` uses
  `ui/restoreTabs` only as an example key of a test schema and may keep it.
- Translations: "Reopen tabs at start" leaves the sources; `update_translations`, 0 unfinished.
- Docs `docs/<lang>/getting-started.md` (en, cs, de, pl, sk): the sentence "At the next start the tabs come back,
  disconnected (…)" becomes "The app always starts with one Choose device tab."; the setting leaves the two
  settings tables. `plan/0003-ux-and-wizards.md` and the settings inventory in the plans where they list it.

Threading, errors, limits: unchanged.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| D-1 Start state | fresh start / keep restoring (ADR-0061) / restore but mark absent probes | **fresh start** (2026-11-19) | ADR-0388 |
| D-2 The switch | keep it, off by default / remove the feature | **remove it** (2026-11-19) | ADR-0388 |
| D-3 Users of older versions | — (no setting left: everyone starts fresh) | follows from D-2 | ADR-0388 |
| D-4 Release | 2.0.0-rc.5 / later | **2.0.0-rc.5** (as recommended, 2026-11-19) | — |

## Implementation steps

- [x] 1. Verify that the settings store ignores unknown stored keys (`ui/tabs`, `ui/restoreTabs` of older versions).
      *Done 2026-11-19:* `SettingsStore` reads only registered keys, one at a time (`settings_store.cpp` `value()`);
      `Settings::open` never enumerates keys except the migration snapshot, which is restored verbatim
      (`libs/core/src/settings.cpp` `migrate()`); the only open-time warnings are a newer schema (read-only) and an
      unwritable file. Unknown keys stay in the file unread.
- [x] 2. Remove the restore and the save from `AppViewModel`, the property from `SettingsViewModel`, the row from
      `GeneralSection.qml`; update comments.
- [x] 3. Tests: remove / rewrite the restore tests; new test: a settings file with `ui/tabs` (two tabs) and
      `ui/restoreTabs = true` starts with one Choose device tab and no warning.
- [x] 4. `update_translations` (one string leaves), 0 unfinished in cs, de, pl, sk.
- [x] 5. Docs in en, cs, de, pl, sk; plan documents that list the setting. *Also* `docs/<lang>/bluetooth.md`: the
      sentence that Bluetooth tabs come back after a restart is removed.
- [x] 6. Build, ctest (debug, asan, tsan), tidy, format, qmllint, `check_translations` — one preset at a time,
      `nice -n 19 ionice -c 3`, `--parallel 5`.
      Verified 2026-11-19/23: debug 1832/1833 (the one failure, `ConsoleViewModelTest.LearnsLazilyWhenReadingOnConnectIsOff`,
      passes 5/5 alone; unrelated), qmllint, `check_translations` 0 unfinished, tidy 0 findings, ASan 1832/1832, TSan
      1769/1769.
- [ ] 7. Manual: quit with two tabs, start again → one Choose device tab; Settings › General has no such switch.
      Status → Done.

## Tests and verification

Unit tests of step 3; Qt Quick `tst_settings.qml` without the switch; sanitizers and static checks of step 6; the
manual check of step 7 on Linux.

## Risks and open points

- A user who liked the restored tabs loses them for good. The release notes must say so.
- Plan 0052 (views instead of dialogs, PR #13) and the wizard branch touch `app_view_model.cpp` too; this change
  removes about 100 lines there, so expect a conflict when the branches meet.
