# ADR-0388 — The app starts fresh: tabs are not reopened

- **Status:** Accepted (supersedes the tab-restore part of ADR-0061)
- **Date:** 2026-11-19
- **Decided by:** Matej Strnad (2026-11-19: fresh start; the setting is removed)

## Context

ADR-0061 restores the tabs of the last session in the Disconnected state at every start, without looking at the
hardware. A user who comes back with another J-Link and another device finds tabs that cannot connect: Connect fails
with `probe.not_found` although a J-Link is connected, and "Choose another probe" is offered only after that error.

## Options considered

- **Fresh start (chosen):** one Choose device tab at every start.
  - A: the setting `ui/restoreTabs` stays, default off.
  - **B (chosen):** the setting and the restore are removed.
- Keep restoring, mark tabs whose probe is absent and let Connect offer the connected probe. More code, and the app
  would have to enumerate probes at start, which ADR-0061 avoided.
- Keep ADR-0061 unchanged.

## Decision

- The app starts with one *Choose device* tab.
- The setting **Reopen tabs at start** (`ui/restoreTabs`), the saved list `ui/tabs` and the restore code are
  removed. Old settings files keep the two keys; they are not read.
- The responsive-layout part of ADR-0061 is unchanged. Its tab-restore part is superseded by this ADR.

## Consequences

- Nobody meets a tab of hardware that is gone; the start never depends on what was connected last time.
- Users who work with one fixed J-Link and device open their device from the Choose device tab at each start.
- The release notes of the version that ships this must mention the change.
