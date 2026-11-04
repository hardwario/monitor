# ADR-0252 — Probe picker in wizard steps: shared list component, own view model per wizard; wizard order

- **Status:** Accepted (plan 0035; amends the order part of ADR-0065)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

0008, 0009 and 0010 all use an Embedded `probe-picker` step (0007 contract). The probe list exists only inside
`app/gui/qml/pages/ChooseDevicePage.qml`, bound to the tab's `ProbePickerViewModel`, with Connect and "switch to the
tab that uses this probe" behaviour. ADR-0065 let 0008, 0009 and 0010 run in parallel after 0007.

## Options considered

For the picker:
1. **Extract the probe card into `components/ProbeList.qml`; each wizard creates its own `ProbePickerViewModel` (Qt
   child of the wizard, sharing the app's `ProbeScanner`); in a wizard, rows used by another tab are disabled with
   "In use (tab N)" instead of switching tabs; the Choose device page keeps its behaviour (chosen).**
2. Reuse the tab's `ProbePickerViewModel` inside the wizard (selection changes leak into the page).
3. A separate wizard-only list (duplicated QML).

For the order:
1. **Picker extraction → 0008 ∥ 0010 → 0009 (chosen)** — 0009 last, because its topics hand off to 0010 and
   0036 and its Done step is reached from 0008.
2. 0008 ∥ 0009 ∥ 0010 (ADR-0065).

## Decision

Option 1 for both (accepted by Matej Strnad, 2026-11-04).

## Consequences

- A refactoring of the Choose device page with its Qt Quick tests kept green before any wizard work.
- `ProbePickerViewModel` gains a wizard mode (no Connect, no tab switch) and a `selectedSerial()` accessor.
