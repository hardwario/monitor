# ADR-0291 — Setup wizard: J-Link firmware actions on the test step and the Done summary

- **Status:** Accepted (plan 0008; implements ADR-0255 for the setup wizard)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (2026-11-06, questions Q2 and Q3 of the 0008 implementation)

## Context

ADR-0255 places [Update J-Link firmware…] on the `probe-firmware` row of the setup wizard's test step (plan 0008
step 5, as amended by 0035). The check list (`CheckListModel` / `CheckList.qml`) has no per-row buttons, the
`probe-firmware` check gives one row for all probes, and the outdated serial number was only part of the error's
detail text. `ProbeFirmwareViewModel::requestForSerial(serial)` needs a serial number. Plan 0008's Done summary
names "J-Link 683012345 ✓", but the read-only picker of step 5 selects no probe.

## Options considered

- Placement: (a) a generic per-row action in `CheckListModel`; **(b) step-level actions (`WizardStep::actions`)
  (chosen)**; (c) a button on the picker row.
- Serial: (i) a structured field in the check result; (ii) the first outdated probe; **(iii) one button per outdated
  probe, taken from `EnvironmentFacts::probes[].firmware` (chosen)**.
- Summary: **all serial numbers the `probes` check found (chosen)**; the count; the first one.

## Decision

- While the `probe-firmware` row is a Warning, the test step has one action per probe whose
  `EnvironmentFacts::probes[].firmware` is `Outdated` (the facts of the step's last finished check request; never
  parsed from the error text): "Update J-Link firmware %1…" → `ProbeFirmwareViewModel::requestForSerial(serial)`.
- When `ProbeFirmwareViewModel::blockedReason(serial)` is not empty (a tab uses the probe: "Disconnect tab %1 first.",
  ADR-0246; another update; an AI assistant), the action is shown **disabled with that reason as its text**. For this
  the action map of `WizardStep::actions` gets an optional `enabled` key (default `true`).
- The actions follow tab changes (`AppViewModel::probeUsageChanged`) and the update's state; a finished update runs
  the step's checks again.
- The Done summary lists every serial number the `probes` check found ("J-Link ✓ 683012345, 683000001").

## Consequences

- No change to `CheckListModel` or to the diag checks; the facts are already collected for the diagnostic report.
- The actions are in the dialog's footer (the place of `WizardStep::actions`); several outdated probes give several
  buttons.
- The troubleshooting topic `probe-firmware` (0009) can reuse the same pattern.
- The summary is shown by the setup wizard's control on the Done step (ADR-0292).
