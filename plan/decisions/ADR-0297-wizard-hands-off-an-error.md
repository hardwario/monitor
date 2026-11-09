# ADR-0297 — A wizard can hand an error off to the error's own wizard

- **Status:** Accepted (plan 0009; extends the wizard contract of plan 0007 / ADR-0067)
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (2026-11-09, question Q1 of the 0009 implementation)

## Context

A wizard hands off to another wizard with the action `open-wizard:<id>` (plan 0010): `AppViewModel` opens it through
`WizardLauncher::openForTab`, which passes no error, so the other wizard starts at its beginning. Plan 0009 needs
another wizard at a specific error: the Connect wizard's Check step offers the troubleshooting topic `probe-busy` when
the probe cannot be opened, and the troubleshooting topics `probe-busy` / `modem-failed` send a failed USB access /
nRF Util check to the setup wizard's step 3 / 4. The flash wizard's [Help me fix this] after a failed flash (for
example `target.program_failed`) opened the troubleshooting topic list instead of the `program-failed` topic.

## Options considered

1. `open-wizard:<id>:<error code>`: `AppViewModel` builds the context with `makeError(code)` (the detail is lost).
2. **A new signal `Wizard::handOffError(ErrorInfo)` carrying the full error; the wizard the error names opens with it
   as context (chosen).**
3. No context: the other wizard opens at its start.

## Decision

Option 2. `Wizard::handOffError(const ErrorInfo&)` is forwarded by `WizardViewModel::handOffError(error, tab)`;
`AppViewModel::onHandOffError` opens the wizard named by `ErrorInfo::wizard()` (when registered) on the same tab with
the error as context, queued after the handing-off wizard has closed (`WizardLauncher::openForTab(id, tab,
context)`). The handing-off wizard emits `finished(false)` right after it and keeps its session for the next owner
(as with `handOff`). The flash wizard's [Help me fix this] uses it.

## Consequences

- The error's detail is kept (troubleshooting's introduction, the setup wizard's start step).
- `open-wizard:<id>` stays for hand-offs without an error (LTE update, flash wizard, Connect wizard).
