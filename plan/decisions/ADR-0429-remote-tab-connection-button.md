# ADR-0429 — The connection button of a remote support tab: Leave session / Join again

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, "Approve all", plan 0061)

## Context

In a remote support tab (plan 0044) the device bar's connection button is always disabled, because its enable rule
knows only J-Link and Bluetooth tabs (`ConnectionViewModel::actionEnabled()`). Support can leave a running session
only by closing the tab (field report of 2026-11-24, plan 0061 item 1). The banner's "Leave" action exists only
after the session ended or went idle, and it closes the tab.

## Options considered

- **Button:** (a) "Leave session", the tab and its history stay; (b) "Disconnect" as in a local tab; (c) no button,
  "Leave" in the banner in every state.
- **Afterwards:** (a) the button becomes "Join again" with the same code, the customer allows again; (b) no way
  back from the tab.
- **Safety:** (a) no confirmation, disabled while an operation that support started is running; (b) always a
  confirmation; (c) neither.
- **Banner action that closes the tab:** (a) renamed "Close tab"; (b) stays "Leave".

## Decision

Proposed: (a) in all four. The button's action comes from the join state (`leave` / `join`), not from the
connection state of the customer's device. Leaving writes the marker "— you left the session —"; the banner says
"You left the session. The console stays here so that you can save it.".

## Consequences

- "Leave" has one meaning (leave the session); closing the tab is called "Close tab".
- The customer is asked for consent again on every "Join again"; the host's join rate limits apply.
- The `device.toggle` shortcut works in a remote tab.
- New translatable strings in five languages; Qt Quick tests for the button in a remote tab.
