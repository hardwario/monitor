# 0055 — AI assistant permissions: no flicker, and one row for all

- **Status:** In progress (approved 2026-11-19, Matej Strnad: "Approve as shown" — the mock and D-1 to D-3 as
  recommended)
- **Milestone:** 2.0.0-rc.5
- **Author:** Claude (with Matej Strnad), 2026-11-19
- **Related ADRs:** ADR-0390 (accepted); ADR-0101 (consent categories), ADR-0161, ADR-0164 (warnings),
  ADR-0359–0361 (user modes)
- **Depends on:** plan 0050 (in this branch); implemented after plan 0054 in the same branch

## Context

Reported by the user on 2026-11-19 about **Settings › AI assistants (MCP) › Permissions** (five rows, each with the radio
buttons **Ask** / **Always allow**):

1. Choosing one radio button makes **all** radio buttons flicker.
2. There is no way to set all rows at once ("allow all / ask for all").

Cause of 1: the rows are a `Repeater` over `McpController.rules`, a `QVariantList` property
(`app/gui/qml/settings/McpSection.qml:274`, `app/gui/src/mcp/mcp_controller.cpp:250`). Every change of one rule
emits `rulesChanged`, the whole list is a new value, and the `Repeater` destroys and recreates every row. The
keyboard focus is lost with it.

## Goals / Non-goals

**Goals:** a click changes only the clicked row, the focus stays on it; one control sets all rows.

**Non-goals:** no change to the categories, to what needs consent, to per-client grants or to the consent prompt
(plan 0054). No change to the CLI (`--allow` flags) or to MCP.

## User experience

```
Permissions                                   Ask      Always allow
What an AI assistant may do without asking.

All                                           ( )         ( )        ← new row; neither is set when rows differ
──────────────────────────────────────────────────────────────────
Flash firmware                                (•)         ( )
Erase the device                              (•)         ( )
Write memory                                  (•)         ( )
  Writing memory can make the device run arbitrary code.
Write the UICR                                (•)         ( )
Connect devices                               (•)         ( )
```

- **All › Ask** sets every row to Ask at once, without a question.
- **All › Always allow** asks first (it includes erasing, UICR and memory writes):
  "Always allow everything? An AI assistant will be able to flash, erase and write the memory of your devices
  without asking you." **[Cancel] [Always allow everything]**. Cancel changes nothing.
- The **All** row shows Ask or Always allow only when all five rows agree; otherwise neither button is set.
- Clicking a row's button changes that row only; nothing else is redrawn, the focus stays on the clicked button.

## CLI

None.

## Design

- New `app/gui/src/mcp/consent_rules_model.{h,cpp}`: `ConsentRulesModel : QAbstractListModel` (child of
  `McpController`, exactly `GuiConsentBroker::categories.size()` rows = 5, roles `id`, `title`, `hint`, `allow`).
  On `GuiConsentBroker::rulesChanged` it compares each row's `allow` with its cached value and emits `dataChanged`
  for the changed rows only (role `allow`); a language change emits `dataChanged` for the texts. Never a model
  reset.
- `McpController`: the property `rules` becomes `QAbstractItemModel* rules` (CONSTANT); new
  `Q_PROPERTY(int allRules READ allRules NOTIFY rulesChanged)` (0 = mixed, 1 = all ask, 2 = all always allow) and
  `Q_INVOKABLE void setAllConsentRules(bool allow)`, which sets every category that differs through the broker (one settings write per changed
  category; the settings store has no batch write — corrected 2026-11-19). `setConsentRule` stays. (The duplicate
  `rulesChanged` emission assumed here does not exist; nothing was removed.)
- `McpSection.qml`: the `Repeater` uses the model (`required property` roles instead of `modelData`); the **All**
  row above it; the confirmation uses the existing `ConfirmDialog` (or, after plan 0052, its view equivalent).
  Object names `mcpAsk_<id>` / `mcpAllow_<id>` are kept; new `mcpAskAll`, `mcpAllowAll`, `mcpAllowAllConfirm`.
- New strings ("All", the question, the two buttons) are translated in cs, de, pl, sk.
- Docs: `docs/<lang>/mcp.md` § Permissions, one sentence about the **All** row.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| D-1 Control for all | an **All** row with the same two radio buttons / two buttons above the list | **an All row** (2026-11-19) | ADR-0390 |
| D-2 "Always allow" for all | confirm once / no question | **confirm once**; Ask for all without a question (2026-11-19) | ADR-0390 |
| D-3 Which rows "All" covers | all five / all except memory and UICR writes | **all five** (2026-11-19) | ADR-0390 |

## Implementation steps

- [x] 1. `ConsentRulesModel` + GTests (one rule changed → one `dataChanged`, no reset; language change).
- [x] 2. `McpController`: `rules` model, `allRules`, `setAllConsentRules`; tests (mixed / all ask / all allow, one
      settings write).
- [x] 3. `McpSection.qml`: model roles, **All** row, confirmation; Qt Quick test: clicking one radio button keeps
      the other rows' delegates (same object instances) and the focus; All › Ask; All › Always allow with Cancel
      and with confirm.
- [x] 4. `update_translations`, translate cs, de, pl, sk (0 unfinished); docs in five languages.
      (Implementation note: `docs/en/mcp.md` is the only MCP page and it has no Permissions part — it describes the
      CLI server only — so no docs sentence was added.)
- [x] 5. Build, ctest (debug, asan, tsan), tidy, format, qmllint, `check_translations` — one preset at a time,
      `nice -n 19 ionice -c 3`, `--parallel 5`.
      Verified 2026-11-23 together with plan 0054 (same tree): debug 1867/1868 (unrelated flaky
      `StdinLineReader.NotifyAfterEachPushAndBlockingWhenFull`), qmllint, format, `check_translations` 0 unfinished,
      tidy 0 findings, ASan 1867/1867, TSan 1800/1800.
- [ ] 6. Manual: no flicker on a click; keyboard (Tab, Space) keeps the focus. Status → Done.

Implementation notes (2026-11-19, for review):

- The role is `ruleId`, not `id`, as a precaution (not verified by compiling): `id` is reserved in QML and is
  unlikely to work as the delegate's `required property`. The object names `mcpRule_<id>`, `mcpAsk_<id>`, `mcpAllow_<id>` are unchanged.
- Chosen, revertible: the property is typed `hwio::gui::ConsentRulesModel*` (a `QAbstractListModel`, as `activity`
  is typed `ActivityModel*`) instead of `QAbstractItemModel*`.
- "Stores the settings once": `SettingsStore` has no batch write, so `setAllConsentRules` writes only the categories
  that differ, each once (each write syncs). A single write would need a new `SettingsStore` API — open question.
- The "duplicate `rulesChanged`" did not exist: the controller forwards the broker's signal and emits itself only when
  storing failed; that failure path is kept.
- Chosen, revertible: `allRules` uses the enum `McpController.AllRulesMixed` / `AllRulesAsk` / `AllRulesAllow` (0 / 1 / 2).

Verification notes (2026-11-23):

- `test_permissionsAllRow` failed at the Cancel focus check (ADR-0038): the consent-window cases that run before it
  probably activate the prompt window; observed: the main window was not active, so Cancel had `focus` but not
  `activeFocus`.
  Test fix, no product change: the case activates the main window first, as `test_permissionsKeepTheRowsAndTheFocus`
  already does.
- Results: debug build passed; debug ctest failed 1 of 1868 (`StdinLineReader.NotifyAfterEachPushAndBlockingWhenFull`
  in the untouched `app/cli/tests/terminal_test.cpp`, a newly seen timing flake: it failed 1 of 3 runs alone;
  discounted). Passed: tidy (0 findings), clang-format, `all_qmllint`, `check_translations` (0 unfinished), `check-includes`, ASan
  (1867/1867) passed. TSan: all tests of this plan pass; the preset fails only on two `CatalogFlash.*` tests of
  plan 0054 (see there), so step 5 stays open until that is decided.

## Tests and verification

As in the steps. The flicker itself is tested by object identity: the row delegates found before a click are the
same objects after it.

## Risks and open points

- Plan 0052 (PR #13) turns dialogs into views; the confirmation here must use whatever that plan leaves for
  confirmations (it keeps confirmations as dialogs).
- "Always allow everything" is the most permissive state of the app; D-2 and D-3 decide how easy it is to reach.
