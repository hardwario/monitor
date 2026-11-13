# ADR-0358 — Remote session: the host confirms delivery before it disconnects

- **Status:** Accepted
- **Date:** 2026-11-11
- **Decided by:** Matej Strnad (plan [0044](../features/0044-remote-support-session.md) D1 check, asked by the
  implementing agent)

Amends ADR-0354 (how the tunnel `end` is delivered).

## Context

The D1 check against the real relay (`~/terminal` `server/index.js`, Socket.IO 4) on 2026-11-11 showed that a joiner
never received the host's `end`. The host wrote `end`, the Socket.IO DISCONNECT and the WebSocket close at once; they
arrive in one TCP read. Socket.IO 4 dispatches a received event on the next tick and drops it when the socket has
disconnected by then, but it handles DISCONNECT and a closed transport immediately. The in-process `FakeRelay` handled
events synchronously, so the tests did not see it.

## Options considered

1. A round trip before the disconnect: CONNECT to a namespace the relay always refuses, wait for its CONNECT_ERROR,
   then disconnect; give up after a short timeout.
2. A fixed delay after `end` (for example 300 ms), then disconnect.

## Decision

Option 1. On shutdown the relay worker sends everything still queued, then a CONNECT to `/hwio-end` (the relay accepts
only `^\d+$` namespaces, so it answers CONNECT_ERROR with no other effect). The relay handles packets of one socket in
order, so its answer proves that everything sent before it was handled. Only then the worker sends DISCONNECT and closes
the WebSocket. It gives up waiting after `RelayLimits::closeTimeout` (2 s) and closes anyway. `FakeRelay` dispatches
events asynchronously like Socket.IO 4, so the tests cover this.

## Consequences

- A Monitor joiner reliably sees "The session ended." when a Monitor host ends sharing in any way.
- End sharing, closing the tab and quitting while sharing can take one relay round trip, at most 2 s.
- The relay logs one refused namespace connect per ended session.
