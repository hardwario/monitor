# 0046 — rc.4 fixes: LTE update confirmation, dark-theme button contrast, remote session fixes

- **Status:** In progress (approved 2026-11-12, Matej Strnad)
- **Milestone:** 2.0.0-rc.4
- **Author:** Matej Strnad, Claude
- **Related ADRs:** ADR-0366 (confirm before the erase), ADR-0367 (filled buttons in the dark theme),
  ADR-0368 (remote LTE update questions go to support), ADR-0369 (support's result of a remote operation),
  ADR-0171 (hexdump lines are log output; A5 is a bug fix under it)
- **Depends on:** plan 0033 / ADR-0234 (guided LTE update), plan 0044 / ADR-0363 (remote LTE update)

## Context

Found while testing rc.4:

1. The guided LTE update erases the application first and only then asks for the LTE connector; the user learns
   about the cable move after the application is gone.
2. Filled blue buttons in the dark theme have near-black text that is hard to read.
3. In a remote LTE update the customer was asked the decisions (Continue, Continue without a copy, Write back);
   they belong to support (ADR-0368).
4. Support's Flash card kept "Done (reported by the customer's Monitor)." without saying what or when (ADR-0369).
5. Support's remote console showed Zephyr `LOG_HEXDUMP` continuation lines as console output (local tabs hide
   them by default, ADR-0171).

## Goals / Non-goals

Goals: say what the LTE update will do and wait for Continue before anything changes; readable filled buttons.
Non-goals: no change of the update order (ADR-0234), no other colour changes.

## User experience

### LTE update confirmation (ADR-0366)

After the APP connector is found, a step **Ready to update the LTE MCU**:

"The application on this CHESTER will be erased. Then you move the J-Link cable to the LTE connector, and at the end
back to APP; the application is written back." — **Continue** · **Cancel** (nothing changed).

### Remote LTE update: support decides (ADR-0368)

- The customer's wizard / terminal shows the explanation and the cable instructions plus "HARDWARIO Support
  confirms the update. Keep the J-Link cable on the APP connector for now." — no Continue button / no y/N.
- Support's Flash card shows the question with **Continue** · **Cancel**; `hwio remote join --lte-update` prints
  it with `[y/N]` (and a `question` event in `--json`).
- Backup failed: support is asked "The application could not be copied. Continue without a copy?"; the customer
  sees "HARDWARIO Support decides whether to continue without a copy."
- The copy is always written back automatically; no answer within the operation timeout (60 s), or the session
  ends: cancelled before the erase, nothing changed.
- A host or joiner without the feature (not in the hello / request) keeps the ADR-0366 behaviour.

### Support's operation result (ADR-0369)

- Support's Flash card: "Application firmware flashed on the customer's device (14:02).", "Customer's device reset
  (14:02).", "LTE firmware updated, application written back (14:02)." etc.; errors keep the error message with the
  operation name. A success clears when support leaves the Flash page or starts another operation; an error stays
  until × or another operation. Every result is also a status line in support's console of that remote tab.
- `hwio remote join` prints the same operation names.

### Hexdump lines in support's console (A5)

Hexdump continuation lines received from the host are log lines (level Hexdump), hidden by default as in local tabs.

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

- ADR-0368: tunnel `op-question` {id, qid, question} host → joiners and `op-answer` {id, qid, yes} joiner → host;
  the host's hello says `opQuestions`, a joiner that answers sends `questions: true` in its lteUpdate request.
  `RemoteHost` accepts an answer only from an allowed participant, only for the open question (operation id and
  the host's random qid), once; a timer of `operationTimeout` answers no; the session's end answers no.
  `LteUpdateReporter::setAskSupport()` asks on `Confirm` / `BackupFailed` and applies the answer;
  `LteUpdateWizard` and `GuidedTerminal` show no button / question when `supportDecides()`; a remote update
  uses `autoRestore`. `RemoteOperationClient::setAnswersQuestions()`, `question()`, `answer()`.
- ADR-0369: `RemoteJoinViewModel` builds the result text from the operation kind/target and the local time,
  clears a success on leaving the Flash page or a new request, keeps an error until `dismissResult()`; the
  result goes to the tab's console as a status line. The CLI joiner uses the same names.
- A5: `RemoteConsoleSession` classifies a received line with `device::isHexdumpLine` (Channel::Log,
  LogLevel::Hexdump) before `parseZephyrLevel`, as `BleSession` does.

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D-1 | What before the erase | confirm step / check LTE first | confirm step (2026-11-12) | ADR-0366 |
| D-2 | Filled buttons, dark theme | white on darker blue / bold black | white on darker blue (2026-11-12) | ADR-0367 |
| D-3 | Who answers the remote LTE update's questions | customer / support | support, copy always written back (2026-11-12) | ADR-0368 |
| D-4 | Support's result of a remote operation | as is / named, timed, not lingering | named, timed, success clears, console line (2026-11-12) | ADR-0369 |

## Implementation steps

- [x] A1 `LteUpdateProcedure` step `Confirm`; unit tests (stops before backup, cancel changes nothing, continue runs
      the rest)
- [x] A2 GUI wizard page; CLI guided terminal and `remote host` question; remote phase `confirm`; tests (Qt Quick,
      CLI e2e with the fake board, remote e2e)
- [x] B1 `accentFill` / `onAccentFill`, all filled accent controls; contrast test (≥ 4.5:1 both themes)
- [x] A3 Remote LTE update questions to support (ADR-0368): tunnel messages and validation, host/joiner,
      GUI Flash card question, customer side without a button, CLI join y/N; tests (tunnel, RemoteHost, remote e2e
      CLI joiner yes/no/timeout, `remote host` with support yes/no, Qt Quick joiner question, wizard customer side)
- [ ] A4 Support's operation result (ADR-0369): named with time, success clears, error until ×, console line; CLI
      names; Qt Quick tests
- [ ] A5 Hexdump continuation lines in support's console are Log/Hexdump (ADR-0171); unit test + hidden by default
- [ ] C1 Translations (0 unfinished), docs (LTE update pages ×5, remote-support ×5, CLI), screenshots, release notes rc.4
- [ ] C2 Verification: full ctest, ASan/TSan for changed tests, clang-tidy, qmllint, clang-format, check_translations

## Tests and verification

As in the steps; manual: the wizard on a real CHESTER up to the confirmation (Cancel), dark theme buttons by eye.

## Implementation notes

To be confirmed by the user:

- The relay does not report who sent a `write` nor that a joiner left. The host therefore accepts an `op-answer`
  from any allowed participant that knows the operation id and the open question's random qid (in practice the
  joiner that started the operation); "support leaves" is detected as no answer within the operation timeout
  (60 s), or as the session's end.
- After the remote session ended (the reporter is abandoned), a question that comes later in the same update
  (e.g. backup failed after the session ended) goes to the customer again, as in a local update (ADR-0366).
- The customer can still Cancel the wizard while support's question is open (only Continue is removed).
- The customer-side behaviour is tested in the view-model test (`FlashTest.RemoteLteUpdateQuestionsGoToSupport`)
  rather than in Qt Quick: a Qt Quick test would need the fake nRF Util and a designated-firmware server in the QML
  test process.

