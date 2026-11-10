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

Option 1, in both directions, and the worker thread becomes an owned `std::jthread`:

- **Worker → client.** The worker pushes a `RelayEvent` (connected, connection lost, failed, warning, incoming event)
  into a `BoundedQueue` owned by `RelayClient` (std::mutex, visible to TSan) and posts one payload-free wake-up,
  coalesced by an atomic flag as in `DeviceSession`; the client drains the queue on its thread and emits its
  unchanged public signals. Bound: `RelayLimits::incomingEventQueue` (4096 entries, one slot reserved for the final
  failure). When full, a warning is dropped and logged; any other notification ends the connection with
  `remote.protocol_error`. Incoming `at` events are never dropped silently.
- **Client → worker.** Sends go through a second `BoundedQueue` (`RelayLimits::outgoingSendQueue`, 4096 entries) and
  the same kind of wake-up. When full, the oldest droppable send is dropped (`internal.queue_full` warning). The
  byte-bounded outgoing queue inside the worker (1 MiB) is unchanged. `shutdown()` sends what is still waiting, so
  the tunnel `end` (ADR-0354) goes out before the disconnect.
- **Worker thread.** The worker runs on a `std::jthread` owned by `RelayClient` (decided by the user on 2026-11-10,
  amending the "own QThread" of plan 0044 § Threading), with a `QEventLoop` on the thread Qt adopts. `stop()` shuts
  the worker down, quits the loop and joins the thread. Reason: Qt does not join a `QThread` in a way TSan can see,
  so when a new relay thread (a code collision, a second session) reused the thread id, TSan's thread registry
  aborted (`sanitizer_thread_registry.cpp` CHECK); a joined `std::jthread` avoids that.

## Consequences

- All `libs/remote` tests run under TSan without suppressions for Qt's queued calls.
- The public `RelayClient` API and signals are unchanged.
- Two more bounded queues, documented in plan 0044 § Limits.
- The `race:QMetaType::destroy` and `race:QMetaCallEvent::~QMetaCallEvent` TSan suppressions are no longer needed.
