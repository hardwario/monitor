# ADR-0321 — QtWebSockets in the backend, own Engine.IO / Socket.IO client

- **Status:** Accepted (amended by ADR-0357)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Context

The relay uses Socket.IO v4 (Engine.IO v4) over WebSocket. ADR-0004 allows only QtCore, QtNetwork and QtBluetooth
in `libs/*`. socket.io-client-cpp pulls in websocketpp, asio and RapidJSON, uses its own threads and exceptions
(ADR-0007).

## Decision

Extends ADR-0004: `libs/remote` may also link **QtWebSockets** (LGPL, dynamic). We implement Engine.IO v4 /
Socket.IO v5 ourselves: text packets, events, namespace auth, ping/pong. No binary, no ACKs. Every packet is
size-checked and parsed with Qt JSON.

## Consequences

- Small, auditable client with bounded buffers and `std::expected` errors.
- QtWebSockets must be deployed in every package (0015). Tests use `QWebSocketServer` as a fake relay.
- We maintain the protocol subset ourselves.
