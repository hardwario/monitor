# ADR-0292 — A wizard step of any kind may show an Embedded control below its content

- **Status:** Accepted (plan 0008; extends the wizard contract of plan 0007 / ADR-0067)
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (2026-11-09, approval of the 0008 implementation)

## Context

ADR-0067 renders each step kind with a fixed component; `WizardDialog.qml` loaded a step's named control
(`WizardStep::embedded`) only for kind Embedded. The setup wizard (plan 0008) needs controls that no standard kind has:
the language combo and "Don't show at start" on its Welcome step (Info), the commands, a file dialog and extra buttons on
its Checks steps, and a summary with versions and serial numbers on Done. ADR-0184 forbids runtime data (versions,
paths, serial numbers) in `body`, which the dialog shows as StyledText; such texts need a plain-text label.

## Options considered

1. **Any step may name an Embedded control; the dialog shows it below the step's own content (chosen).**
2. Make those steps kind Embedded with a wizard-specific control that also renders checks, links and the Done mark
   (as the LTE update wizard does).
3. Add the missing pieces (commands on Checks steps, combos, file dialogs, a summary list) to the dialog itself.

## Decision

Option 1. When `WizardStep::embedded` is set, `WizardDialog.qml` loads the registered control for every step kind,
below the illustration, heading, body, link, check list and choices, with `vm` = `Wizard::embeddedViewModel()` and the
step's `embeddedOptions` (ADR-0290); `WizardViewModel::embeddedSource` is set for it. Steps without `embedded` render as
before. Runtime texts stay out of `body` (ADR-0184) and go into the control as plain text.

## Consequences

- The step kinds keep their meaning (step list, Done mark, Close button, Checks rows); a wizard adds only what is
  specific to it.
- The setup wizard's control `setup-step` (`app/gui/qml/wizard/SetupStep.qml`) uses this on Welcome, its Checks steps
  and Done.
- A control on a non-Embedded step must keep its implicit height independent of the dialog's width (an Item + Column
  root, as `ProbePickerStep.qml`), or the dialog's layout warns about recursive rearranges.
