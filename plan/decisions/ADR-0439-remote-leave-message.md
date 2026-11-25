# ADR-0439 — Remote support session: a joiner tells the host that it leaves

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, "Add a leave message", plan 0061 Amendment 2, D14)

## Context

With "Leave session" (ADR-0429) support can leave a remote session and stay in the tab. The customer's Monitor
cannot notice: the relay sends nothing when a socket disconnects and the tunnel has no leave message. After a
leave the customer's banner still says that support can send commands, and nothing is written to the audit trail.

## Options considered

- (a) A new tunnel message `leave` from the joiner, sent only to a host whose hello lists the capability `leave`
  (ADR-0414); the host withdraws the participant's permission, writes an audit line and updates its banner.
- (b) Leave it: the customer learns nothing until the idle rules end the session.
- (c) Change the relay to announce disconnects (rejected: the relay is shared with the web Terminal and the mobile
  app and is out of scope, ADR-0334).

## Decision

(a). Protocol version stays 1. The message is best effort (Leave session, tab close, quit) and is never sent to a
host without the capability. A running operation started by that participant is not cancelled. A forged `leave`
can only withdraw permission.

## Consequences

- Between two Monitors of 2.0.0-rc.5 or newer the customer sees and has on record that support left; with older
  Monitors, the web Terminal or the mobile app as host nothing changes.
- The host's permission is one flag for "a participant": with several participants a `leave` of one withdraws it
  for all, and the customer allows again.
- Needs the capability list of plan 0059; it is implemented on a branch that contains plans 0059 and 0061.
