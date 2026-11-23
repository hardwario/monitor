# ADR-0391 — The wizards of plans 0008–0010 and the probe picker follow the views model in 2.0.0-rc.5

- **Status:** Accepted
- **Date:** 2026-11-23
- **Decided by:** Matej Strnad (the conversion and the release, 2026-11-23); open points D-6, D-8, D-9 of plan
  [0056](../features/0056-wizards-as-views-rc5.md), D-11 and D-12 await confirmation

## Context

The setup (plan 0008), connect and troubleshooting (0009) and flash (0010) wizards and the wizard-mode probe picker
(0035, ADR-0252) were written for one modal wizard dialog (ADR-0066). Plan 0052 (ADR-0386, ADR-0387) replaced it by
views: one `WizardViewModel` per device tab plus a global one. This branch's single-instance code (hand-off signals
connected once, the ADR-0300 chain pointer `m_chainTab`, `StartupCheck` on `AppViewModel::wizard()`, embedded controls on
`AppViewModel.wizard`) does not work with many instances, and a global view hides per-tab wizards.

## Options considered

- Conversion and release: convert in rc.5 / ship rc.5 without the wizards / keep the dialog for them.
- Per-tab wizard while the global setup wizard is open (D-6): refuse and show setup / open above setup on its tab /
  close setup.
- ADR-0300 chain state (D-8): per tab / in `AppViewModel` as a set.
- Wizard-mode picker rows used by another tab (D-9): not selectable / click switches to that tab.

## Decision

- **Decided 2026-11-23 (Matej Strnad):** the setup, connect, troubleshooting and flash wizards and the probe picker are
  converted to the views model of plan 0052 and ship in 2.0.0-rc.5. Connect, troubleshooting and flash (and the LTE
  update guide) run in the tab's `WizardViewModel`; setup in `AppViewModel.globalWizard`.
- Proposed (D-6 (A)): while the setup wizard is open a per-tab wizard is refused with "Finish or close the setup wizard
  first." and the setup wizard is shown; opening setup while tab wizards run is allowed (it covers them, as in 0052).
- Proposed (D-8 (A)): the tab owns the ADR-0300 state: it keeps its wizards' probe-only session while its wizard is open
  or a chained wizard of the same tab is pending, releases it when idle otherwise, and keeps the wizard page between
  chained wizards. `WizardViewModel::openedOnTab/closedOnTab` and `AppViewModel::m_chainTab` go away.
- Proposed (D-9 (A)): a wizard's picker keeps "In use (tab N)" rows not selectable; a wizard never takes another tab's
  probe. Up to 9 pickers (8 tabs + setup) share the one `ProbeScanner`.
- Proposed (D-11 (C)): while connect, troubleshooting or flash is open on a tab, that tab's "Choose another device"
  is refused with a toast (the wizard owns the device choice); connect / disconnect / reconnect stay usable and the
  wizards follow the session; their operations stay guarded by the tab's busy state.
- Proposed (D-12 (A)): opened from the Settings tab, a per-tab wizard opens on the last device tab.
- Proposed (part of D-6 (A)): support's remote LTE update request (ADR-0363) opens its guide even while the setup
  wizard is open.
- Every embedded control declares `property WizardViewModel host` (set by `WizardView`) instead of using an app-wide
  instance.

## Consequences

- Amends ADR-0300 (chain state per tab), ADR-0252 (the "modal wizard" reason becomes "never another tab's probe") and
  ADR-0386 (the D-6 exception to "tabs run wizards independently"; D-11).
- `WizardLauncher` gains `canStart()` and the D-6 rule; `StartupCheck` uses the global instance and waits on any open
  wizard only for the J-Link-needed variant.
- About 130 Qt Quick test lines and 160 C++ test calls move from the single instance to per-tab / global instances.
