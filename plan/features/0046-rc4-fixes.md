# 0046 — rc.4 fixes: LTE update confirmation, dark-theme button contrast

- **Status:** Approved (2026-11-12, Matej Strnad)
- **Milestone:** 2.0.0-rc.4
- **Author:** Matej Strnad, Claude
- **Related ADRs:** ADR-0366 (confirm before the erase), ADR-0367 (filled buttons in the dark theme)
- **Depends on:** plan 0033 / ADR-0234 (guided LTE update), plan 0044 / ADR-0363 (remote LTE update)

## Context

Found while testing rc.4:

1. The guided LTE update erases the application first and only then asks for the LTE connector; the user learns
   about the cable move after the application is gone.
2. Filled blue buttons in the dark theme have near-black text that is hard to read.

## Goals / Non-goals

Goals: say what the LTE update will do and wait for Continue before anything changes; readable filled buttons.
Non-goals: no change of the update order (ADR-0234), no other colour changes.

## User experience

### LTE update confirmation (ADR-0366)

After the APP connector is found, a step **Ready to update the LTE MCU**:

"The application on this CHESTER will be erased. Then you move the J-Link cable to the LTE connector, and at the end
back to APP; the application is written back." — **Continue** · **Cancel** (nothing changed).

### Filled buttons (ADR-0367)

Dark theme: highlighted buttons, checked segments and toggles use `#1D63D8` with white text. Light theme unchanged.

## CLI

`hwio chester lte flash --guided` and `hwio remote host` (remote `lteUpdate`): before the backup, the same text and
`Continue? [y/N] `. No → exit as cancelled, nothing changed. `--yes` (where the command has it) answers yes.
Without a terminal the guided flow keeps its existing behaviour for missing terminals (refused).

## Design

- `device::LteUpdateProcedure`: new step `Confirm` (step id `confirm`) between `ConnectApp` and the backup;
  `confirm()` / `cancel()` move on. The prompt text comes from the procedure (translated at creation).
- `LteUpdateWizard`: a stage for `Confirm` with Continue as the primary action; remote-started updates also stop
  there (the customer confirms); the 60 s progress repeat keeps the joiner's timeout from firing.
- `GuidedTerminal`: asks the y/N question on `Confirm` through its existing `ask`.
- `Theme.qml`: `accentFill`, `onAccentFill`; `Main.qml` / buttons, `ConsoleSearchBar.qml` and every other filled
  accent control use them (Material highlighted buttons: set the Material accent for highlighted buttons to
  `accentFill`, or style them explicitly — whichever keeps `accent` for text/borders).

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D-1 | What before the erase | confirm step / check LTE first | confirm step (2026-11-12) | ADR-0366 |
| D-2 | Filled buttons, dark theme | white on darker blue / bold black | white on darker blue (2026-11-12) | ADR-0367 |

## Implementation steps

- [ ] A1 `LteUpdateProcedure` step `Confirm`; unit tests (stops before backup, cancel changes nothing, continue runs
      the rest)
- [ ] A2 GUI wizard page; CLI guided terminal and `remote host` question; remote phase `confirm`; tests (Qt Quick,
      CLI e2e with the fake board, remote e2e)
- [ ] B1 `accentFill` / `onAccentFill`, all filled accent controls; contrast test (≥ 4.5:1 both themes)
- [ ] C1 Translations (0 unfinished), docs (LTE update pages ×5, CLI), screenshots, release notes rc.4
- [ ] C2 Verification: full ctest, ASan/TSan for changed tests, clang-tidy, qmllint, clang-format, check_translations

## Tests and verification

As in the steps; manual: the wizard on a real CHESTER up to the confirmation (Cancel), dark theme buttons by eye.
