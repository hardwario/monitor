# 0034 — Pre-release fixes (Settings section scroll, release notes file, plan bookkeeping, CLI)

- **Status:** In progress (approved 2026-11-04 by Matej Strnad; implementation started 2026-11-04)
- **Milestone:** RC follow-up (v2.0.0-rc.3)
- **Author:** Matej Strnad (with Claude), 2026-11-04
- **Related ADRs:** ADR-0238 (new: exit code 130 also "not completed"), ADR-0239 (new: notes file, supersedes
  the "generated notes" detail of ADR-0125), ADR-0172 (Settings as a tab, "Open Settings" scrolls to the section), ADR-0024 (exit codes),
  ADR-0026 (JSON envelope), ADR-0235 (BLE RSSI; `rssiLast` key by user decision)
- **Depends on:** [0033](0033-rc2-feedback.md) (`--guided`, `rssi_last`)

## Context

Clear defects found before the next release candidate; the user approved fixing them on 2026-11-04.

- **B1** `hwio chester lte flash --guided` ends with exit code 0 when the LTE MCU was updated but the application
  was not written back (no copy: the application MCU is left empty). Scripts take that for a complete update.
- **B2** The Settings page opened for the first time with a section ("Open Settings" of an error panel, a wizard
  link) does not scroll to it: `showSection` runs (Qt.callLater) before the section Loaders are laid out, `contentY`
  clamps to 0 and `clearSettingsSection()` drops the request.
- **B3** `release.yml` creates the draft release with `--generate-notes` (a commit list), while the notes are written
  by hand in `packaging/release-notes/<tag>.md` (rc.2).
- **B4** LTE update wizard, step 1 at 960 × 600 without nRF Util: the "nRF Util not found" message is below the
  scroll fold, so Next is disabled with no visible reason; the embedded firmware card shows the Flash page's step
  badge "2" while the header says "Step 1 of 8".
- **H1** `hwio ble scan --json` has the key `rssi_last`; every other key of the output is camelCase
  (`nusAdvertised`, `inUseBy`) — open point of 0033 C.
- **H4** Stale plan bookkeeping: roadmap rows and checklists of work released in v2.0.0-rc.2; plan 0017 still uses
  the `hio` CLI name and the old `hardwario/hio-monitor` repository.

## Goals / Non-goals

Goals: the fixes above. Non-goals: release notes for future versions; MCP plan 0014 and the M6 roadmap row
(another branch); changing the Draft status of 0017/0018; ticking hardware (HIL) or manual checks.

## User experience

- B2: "Open Settings" always shows the requested section at the top of the page (or the end of the page for the
  last sections), also when Settings was not open yet.
- B1: the human text is unchanged in substance; it is now an error (non-zero exit code) with the suggestion
  "Flash an application with `hwio chester app flash`."

- B4: the prerequisite message sits above the firmware card; the firmware cards inside the wizard carry no step
  badge (the wizard header numbers the steps). No new strings (the de/sk/pl translations of the i18n branch are not
  affected; any later new string gets cs here and de/sk/pl on that branch after the merge).

## CLI

- B1: `chester lte flash --guided` without the application written back → `cancelled`, exit code 130 (ADR-0238);
  the 130 row of `plan/0006-cli.md` and `docs/en/cli.md` reads "cancelled or not completed (Ctrl+C, SIGTERM, guided
  procedure ended without restoring the application)".
- H1: `ble scan --json`: `rssi_last` → `rssiLast` (deviates literally from 0033 D5, by user decision 2026-11-04).

## Design

- B2 (`app/gui/qml/pages/SettingsPage.qml`): the requested section is kept in `page.pendingSection`; `scrollToPending()`
  scrolls only when the section is laid out (`y > 0`, the content at least as tall as the section's end, the view has
  a height) and only then calls `AppViewModel.clearSettingsSection()`. Retried on `contentHeight` / height changes of
  the Flickable and on each section's `y` change (queued with `Qt.callLater`, coalesced).
- B3 (`.github/workflows/release.yml`): the gate job fails at once with `::error::` when
  `packaging/release-notes/${GITHUB_REF_NAME}.md` is missing; publish uses `--notes-file` with that file.
  `packaging/RELEASE.md`: notes file step in the checklist, review step.
- H1 (`app/cli/src/ble_commands.cpp`): key renamed; tests and `docs/en/cli.md`. MCP tools have no such key.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Fix B1–B3, H1, H4 | — | approved 2026-11-04 by Matej Strnad | — |
| B3: notes from a file instead of generated (detail of ADR-0125) | file · generated | file (user, 2026-11-04) | ADR-0239 |
| B1: exit code when the application was not written back | `cancelled` 130 · new target error id 6 · 1 | `cancelled` 130, row 130 widened (Matej Strnad, 2026-11-04) | ADR-0238 |
| H1: `rssi_last` → `rssiLast` | — | `rssiLast` (user, 2026-11-04) | — |

## Implementation steps

- [x] B1 `--guided` without the application written back: `ErrorCode::Cancelled` (exit 130) with the message and
  suggestion; `flash_e2e_test` `GuidedLteFlashWithoutACopyExitsNonZero`; 130 row in `plan/0006-cli.md`,
  `docs/en/cli.md`; ADR-0238
- [x] B2 SettingsPage: scroll when laid out, clear the request only after a real scroll; Qt Quick test
  `tst_settings.qml` `test_sectionOnFirstOpen` (fails before, passes after)
- [x] B3 `release.yml` gate check + `--notes-file`; `packaging/RELEASE.md`; ADR-0239
- [x] B4 `app/gui/qml/flash/LteUpdateStep.qml`: `ltePrerequisite` above `lteSourceCard`; `number: 0` on
  `lteSourceCard` and `appSourceCard`; `tst_flash.qml` `test_ltePrerequisiteVisible960` (fails before, passes after)
- [x] H1 `rssiLast` in `ble scan --json`, tests, `docs/en/cli.md`, note in 0033
- [x] H4 roadmap rows consistent with plan statuses ("Released in v2.0.0-rc.2; … open — In progress"); 0001 E2,
  0015 A1–F3 (F2 split: F2b tag protection open) ticked where verifiable; 0001 E3 and 0024 verification stay open;
  0017 `hwio` and `hardwario/monitor`

## Tests and verification

`linux-debug` build, `ctest` (GUI Qt Quick tests and the full run), `all_qmllint`, clang-format-21,
`check_translations` (no new user-visible strings). The release workflow change is checked on the next tag.

## Risks and open points

- B1: the translated `troubleshooting.md` pages describe category `cancelled` generically ("the operation was
  stopped"); left unchanged.
- H4 left open on purpose: HIL and manual checks (0002 G1/G2, 0003 Phase 0/G1/G2, 0007, 0011 F2, 0015 F4, 0016 D1,
  0022, 0024 macOS/Windows/AppImage local check); plans 0008–0010 and 0032 are not implemented.
