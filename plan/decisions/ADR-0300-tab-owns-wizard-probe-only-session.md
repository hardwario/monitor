# ADR-0300 — The tab owns the wizards' probe-only session

- **Status:** Accepted (plan 0009 review; refines ADR-0295 and ADR-0297)
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (2026-11-09, decision A of the 0009 review)

## Context

The flash, Connect and troubleshooting wizards open a probe-only session through the tab for Diagnose and operations.
Each wizard decided on its own whether it had opened the session (`hadSession = tab->session() != nullptr`) and closed
it when it ended without a hand-off. The review of `strnad/impl-0009` found sessions that nobody closed: a hand-off kept
the session, but the receiving wizard did not know it was its to close (flash failure → [Help me fix this] → close;
Connect → [Flash firmware…] → close; [Open Settings]; [Setup wizard…]), a kept flag stayed set after a failed connect,
and a wizard closed during a running Diagnose left it running.

## Options considered

1. **One owner on the tab: the tab records that its probe-only session belongs to the wizards, the session passes
   between wizards chained on that tab and is released when the last of them closes (chosen).**
2. Each wizard adopts the session it receives through a hand-off (a parameter of the hand-off).
3. Never keep a probe-only session across a hand-off (the next wizard opens a new one).

## Decision

Option 1.
- `DeviceTabViewModel::ensureWizardSession()` opens the probe-only session (`ensureSession(false)`) and marks a newly
  opened one as the wizards'. The mark ends when the session is promoted (`connectDevice`, the Connect wizard's
  [Connect], the `open-console` hand-off) or released.
- A hand-off to another **per-tab** wizard on the same tab (`open-wizard:<id>`, `Wizard::handOffError`) keeps the
  session for it (`AppViewModel` remembers the tab until the next wizard has opened; a failed open releases it).
- When a wizard on the tab closes otherwise — Close / Cancel, a hand-off to a page (`open-settings:…`,
  `open-flash-page:…`) or to a global wizard (setup) — the tab releases the wizards' session **when idle**: after the
  tab is no longer busy and the running operation (a cancelled Diagnose) has ended.
- Opening a wizard on the tab cancels a pending release.

## Consequences

- No wizard closes a session itself; `DiagnoseRunner` and the flash wizard use `ensureWizardSession()`.
- The Flash page after `open-flash-page:…` opens its own session (the wizards' one is released).
- Tests cover every chained path ending with no session on the tab.
