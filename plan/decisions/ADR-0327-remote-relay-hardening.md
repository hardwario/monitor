# ADR-0327 — Remote session relay hardening (protocol 2)

- **Status:** Superseded by ADR-0334 (never sent to the backend team; the relay stays unchanged)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Context

ADR-0320 requires a hardened relay before Monitor hosts or joins sessions. Today the session ID is a browser-made
6-digit number without authentication.

## Decision (proposal to the backend team)

- `POST /api/v1/sessions` → `{sessionId, hostToken, joinSecret, expiresAt}`. The server allocates the session.
  `sessionId` and `joinSecret` are ≥ 128-bit random (base32).
- Link: `https://terminal.hardwario.com/?session=<sessionId>#k=<joinSecret>`. The secret is in the fragment, so it
  never reaches server logs.
- The Socket.IO namespace connect needs `auth: {hostToken}` or `auth: {joinSecret}`; a wrong one gets `connect_error`.
  The server rate-limits attempts per IP and per session.
- `write` is relayed only from authenticated joiners. Max 4 joiners. The session ends when the host ends it, at
  `expiresAt`, or after the host has been disconnected longer than a grace period.
- `wss` only. `GET /api/v1/info` → `{protocol: 2, …}`. Monitor refuses protocol < 2.
- The web Terminal moves to the same flow and the old 6-digit sessions are switched off.

## Consequences

The backend team implements and deploys it (step A0 of plan 0044). Changes they need get recorded in a new ADR.
