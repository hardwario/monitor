# ADR-0393 — The connect, flash and troubleshooting wizards refuse to open on a remote-support tab

- **Status:** Accepted
- **Date:** 2026-11-23
- **Decided by:** Matej Strnad (the refusal, 2026-11-23; D-7 of plan
  [0056](../features/0056-wizards-as-views-rc5.md) confirmed as recommended, 2026-11-23)

## Context

A tab that joined a remote support session (plan 0044, transport `remote`) has no probe. The connect, flash and
troubleshooting wizards change the tab's device (`chooseAnotherProbe()`, `configure()`), which ends the support
session.

## Options considered

- Behaviour: refuse with a message / let them run and end the session / hide them.
- Presentation (D-7): a toast from every entry point / a toast from the Help menu and in-page buttons hidden / the
  wizard view opens with one info step.

## Decision

- **Decided 2026-11-23 (Matej Strnad):** on a remote-support tab the connect, flash and troubleshooting wizards refuse
  to open with a short message saying they work on a local device.
- The check is in `WizardLauncher::launch()`, the one path of every opening (Help menu, "Help me fix this", "Guide me",
  hand-offs, the setup wizard's [Connect a device]); `WizardDescriptor::localDeviceOnly` marks the three. It applies
  while `DeviceTabViewModel::isRemote()` is true, also after the session ended.
- Message (constant, translated): "This wizard works only with a device connected to this computer, not in a support
  session."
- Decided (D-7 (A)): shown as a toast; no entry point is hidden.
- The setup wizard (global) and the LTE update guide are not affected.

## Consequences

- No support session is ended by a wizard; one new string in four translations; docs (remote support, five languages)
  say the three wizards are not available on a support-session tab.
