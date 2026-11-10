# ADR-0354 — Remote session: the host announces the end with a tunnel "end" message

- **Status:** Accepted
- **Date:** 2026-11-10
- **Decided by:** Matej Strnad (plan [0044](../features/0044-remote-support-session.md) implementation, asked by the
  implementing agent)

Amends ADR-0336 (the host → joiner tunnel types).

## Context

The relay does not announce that a socket left the namespace (`~/terminal/server/index.js`), and ADR-0336 had no message
for the end of sharing. A Monitor joiner could not tell that a Monitor host ended the session: its tab stayed "joined"
until the relay connection failed or the 30-minute idle notice.

## Options considered

1. An `end` tunnel message from the host.
2. No message: the joiner shows only the idle notice, as for a web Terminal host.

## Decision

Option 1. When a Monitor host's session ends for any reason (End sharing, a refused or unanswered join, collision,
idle or lifetime limit, the console closed), it sends `at {event: "hwio", data: {v: 1, type: "end", code}}` before it
disconnects, where `code` is the hwio::Error id of the reason (`cancelled` for End sharing). The web Terminal ignores
it. A Monitor joiner that receives it ends its session (Stopped, remote.host_ended) and shows "The session ended."; the
tab stays read-only. A joiner accepts `end` only after the host's `hello` (as the other host messages).

## Consequences

- The joiner UX of plan 0044 ("The session ended.") works for Monitor hosts. With a web Terminal host only the idle
  notice remains.
- Any participant could send a forged `end` through the relay; at worst it ends a Monitor joiner's view early (it does
  not affect the host or the device), and the joiner can join again.
- The amended ADR gets an "Amended by ADR-0354" note.
