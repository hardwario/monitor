# ADR-0290 — Wizard steps pass options to their Embedded control (`embeddedOptions`)

- **Status:** Accepted (plan 0008; extends the wizard contract of plan 0007 / ADR-0067)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (2026-11-06, question Q1 of the 0008 implementation)

## Context

Plan 0035 (ADR-0252) made the probe list a shared Embedded step `probe-picker`; plan 0008 step 5 uses it read-only
(no selection). `ProbeList.readOnly` and `ProbePickerStep.readOnly` existed after 0035 phase A, but the wizard
contract had no way to set them: `WizardDialog.qml` loaded the step's control and bound only `vm`
(`Wizard::embeddedViewModel()`). Plan 0035 left open how the flag reaches the step.

## Options considered

1. A `readOnly` property on `ProbePickerViewModel`, read by `ProbePickerStep.qml`.
2. **A generic `QVariantMap embeddedOptions` on `WizardStep`; `WizardDialog.qml` applies each key to the loaded
   control (chosen).**
3. A second registered control name (`probe-picker-readonly`) or a thin wrapper QML file.

## Decision

Option 2. `WizardStep::embeddedOptions` (in `toMap()` as `embeddedOptions`) holds property values for the step's
Embedded control. `WizardViewModel::applyEmbeddedOptions(item)` sets them; `WizardDialog.qml` calls it when the control
is loaded and on every change of the wizard. Behaviour:

- A key that is not a property of the control, and the reserved key `vm` (set by the dialog), is **ignored and logged
  as a warning** (category `hwio.gui`, all builds); the other keys are still applied.
- A key that an earlier step set and the current step leaves out gets the control's own value back (remembered per
  control object the first time the key is set), so a control used by several steps never keeps a stale option.
- The setup wizard's step 5 passes `{readOnly: true}`; 0009 / 0010 may pass their own options.

## Consequences

- Generic: any Embedded control can be configured per step without new C++ API on its view model.
- Options are plain QVariant values set by name; a typo shows up as a warning in the log and in the GTest of the
  wizard that uses it, not at compile time.
- Plan 0007's contract and plan 0035 § A refer to this ADR.
