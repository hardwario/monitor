# ADR-0392 — The mode question is the first step of the setup wizard; no separate mode view at the first start

- **Status:** Accepted
- **Date:** 2026-11-23
- **Decided by:** Matej Strnad (the move into the setup wizard, 2026-11-23); open points D-1 … D-5 of plan
  [0056](../features/0056-wizards-as-views-rc5.md) await confirmation

## Context

ADR-0359 introduced the first-start mode question as a dialog and said the first-run setup wizard (plan 0008) takes it
over as its first step later; ADR-0362 shows it once to users upgrading from rc.3. With plan 0008 merged, the mode view
(plan 0052) and the setup wizard both open at the first start; the setup wizard opening closes the mode view, which
stores the preselected mode without the user choosing.

## Options considered

- Form (D-1): cards on the Welcome step below the language combo / its own Choice step before Welcome.
- Who is asked (D-2): only while `ui/mode` is not chosen / at every run from Welcome.
- Completed setup but no mode (D-3): open setup at Welcome and delete the mode view / keep the mode view for this case /
  nothing.
- Leaving early (D-4): store the selected card / store only on Next or Skip setup.
- First-start timing (D-5): open setup at once when not completed / wait for the J-Link check (≤ 5 s).

## Decision

- **Decided 2026-11-23 (Matej Strnad):** the mode question ("How do you want to use HARDWARIO Monitor?") becomes the
  first step of the setup wizard. The separate mode view is no longer shown at the first start.
- Proposed (D-1 (A)): the three mode cards are on the Welcome step, below the language combo, with the hint "You can
  change it at any time in Settings › General."; preselection as ADR-0362 (Expert when `advanced/expertActions` was on,
  else Advanced).
- Proposed (D-2 (A)): shown only while `ui/mode` is not chosen.
- Proposed (D-3 (A)): `StartupCheck` treats "mode not chosen" like "setup not completed"; `ModeChoiceView` and the
  `modeChoice` content view are removed.
- Proposed (D-4 (A)): leaving the Welcome step in any way ([Next], [Skip setup], ×, Esc, Cancel) stores the selected
  card.
- Proposed (D-5 (A)): when setup is not completed it opens at once, without waiting for the J-Link check.
- Read-only settings: no setup wizard (ADR-0071), so no question; the preselection applies for the run.

## Consequences

- Amends ADR-0359 (where the mode is asked), ADR-0362 (upgrading users are asked in the setup wizard), ADR-0387
  ("Choose mode" is no longer a content view, with D-3 (A)) and the
  ADR-0071 implementation (the "completed" test includes the mode; no wait at the first start).
- rc.3 users and fresh installs see one wizard instead of a dialog plus a wizard; rc.4 users see the setup wizard once
  without the cards.
- Settings › General › Mode is unchanged and is the only place to change the mode later.
- Docs (Modes section, five languages) and the rc.5 release notes change; the mode texts keep their translations.
