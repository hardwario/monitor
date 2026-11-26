# ADR-0386 — Wizards are views of the device tab, one per tab

- **Status:** Accepted (supersedes ADR-0066; amended by ADR-0391 (2026-11-23): a per-tab wizard is refused while the global setup wizard is open (except support's remote LTE update request); connect / troubleshooting / flash own their tab's device choice)
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (plan [0052](../features/0052-views-instead-of-dialogs.md))

## Context

ADR-0066 showed wizards in a modal dialog of at most 720 × 560 px, one in the whole app. In rc.4 testing the dialog
hid content below the fold (issue #10), the console could not be seen during a wizard, and a second device could not
run its own wizard. The user asked to use the app window instead of dialogs.

## Options considered

1. A view in the device tab's page area; tab bar, device bar and rail stay (chosen; ADR-0066 option 2).
2. A view covering the whole window until the wizard ends.
3. Keep the modal dialog, larger.

## Decision

- A wizard opened for a device tab replaces that tab's page area: header (title, step list, × Cancel), scrolling
  content (ADR-0384 hints), pinned footer. Steps, texts and the engine (ADR-0067) are unchanged.
- **One wizard per device tab**; tabs run wizards independently. A second wizard in the same tab is refused and the
  running one is shown. Global wizards (first-run setup) are global views (ADR-0387), one at a time.
- **The rail stays usable**: other pages of the tab can be shown while the wizard keeps running. A "Wizard" rail
  entry (badge while an operation runs) and a badge on the device tab show it again where it was.
- Esc = Cancel while the wizard view has focus; Enter = default button; Cancel asks while an operation runs; closing
  a tab with a running wizard asks as for any busy tab.

## Consequences

- One `WizardViewModel` per device tab plus a global one; the launcher picks the instance.
- Wizards get the full page area (no early scrolling); the console can be watched during a wizard.
- Two tabs can run hardware operations at once (each on its own probe/session, as already possible from pages).
- Every Qt Quick test that used `wizardDialog` changes.
