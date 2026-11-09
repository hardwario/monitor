# ADR-0295 — The flash wizard flashes on the probe-only session; [Open console] promotes it

- **Status:** Accepted; the session ownership part is refined by ADR-0300 (the tab owns the wizards' probe-only
  session; only `open-console` / a promotion and hand-offs to per-tab wizards keep it). (plan 0010; amends the 0010 text "promotes it with `startConnection(attachRtt = false)`
  before flashing")
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (2026-11-09, approved the implementation choice)

## Context

Plan 0010 (written before the 0007 reconciliation) says the flash wizard opens a probe-only session when the tab has
none and promotes it with `startConnection(attachRtt = false)` before flashing. The 0007 reconciliation left only
`DeviceTabViewModel::ensureSession(bool connectTarget)`; a session promoted without RTT reports Connected, so the Done
step's [Open console] would show a console that never attaches. Operations (Diagnose, flash) already run on a
probe-only session (ADR-0069), and `AppViewModel`'s `open-console` hand-off promotes a probe-only session with RTT
(`connectDevice()`).

## Options considered

1. **No promotion before flashing: Diagnose and the flash run on the tab's session or on the probe-only session the
   wizard opened; [Open console] hands off `open-console`, which promotes it with RTT; a probe-only session the
   wizard opened is closed when the wizard closes without a hand-off (chosen).**
2. Promote with `startConnection(false)` before flashing and add a way to attach RTT later (new session API).
3. Promote with RTT before flashing (`ensureSession()`): the flash pauses and restores the console.

## Decision

Option 1.

## Consequences

- No new session API; the probe and its lock stay held from the cable check to the end of the flash.
- After [Close] without a hand-off the tab is Disconnected with its probe (the probe-only session is released);
  after a hand-off (`open-console`, `open-flash-page:…`, `open-wizard:…`) the session stays for the next owner.
