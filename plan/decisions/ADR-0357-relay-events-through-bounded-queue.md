# ADR-0357 — Remote session: relay worker events reach RelayClient through a BoundedQueue

- **Status:** Accepted
- **Date:** 2026-11-10
- **Decided by:** Matej Strnad (plan [0044](../features/0044-remote-support-session.md) implementation, asked by the
  implementing agent)

Amends ADR-0321 (QtWebSockets + own Socket.IO client: how the relay thread talks to its owner).

## Context

`RelayClient` owns a worker on its own QThread (the QWebSocket, Engine.IO / Socket.IO parsing). The worker reported
events, connection changes and errors to `RelayClient` by queued signals. Qt hands queued signal arguments over inside
uninstrumented libQt6Core (futex-based QMutex), so ThreadSanitizer reported every hand-off as a race. The only
suppressions that would match (`QtPrivate::FunctorCall`, `libQt6Core`) would also hide real races in our own queued
slots, and the plan requires all `libs/remote` tests to run under TSan.

## Options considered

1. Pass the worker's events through `hwio::BoundedQueue` plus a queued wake-up, the pattern `DeviceSession` uses
   (ADR-0031).
2. Leave the affected relay tests out of the TSan preset.
3. A broad TSan suppression of Qt's queued-call templates.

## Decision

Option 1. The worker pushes its events (Socket.IO event, connected / lost / restored, error, ended) into a bounded
queue owned by `RelayClient` (std::mutex, visible to TSan) and posts a payload-free wake-up to the owner's thread; the
owner drains the queue and emits its public signals there. Commands to the worker (send, stop) stay as they are. The
queue has a documented bound; when it is full the oldest droppable entries are dropped like the outgoing queue, and a
non-droppable overflow ends the session with a protocol error.

## Consequences

- All `libs/remote` tests run under TSan without suppressions for Qt's queued calls.
- The public `RelayClient` API and signals are unchanged.
- One more bounded queue to document in plan 0044 § Limits.
