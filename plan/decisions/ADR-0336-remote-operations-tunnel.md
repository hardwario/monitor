# ADR-0336 — Remote operations tunneled in at and write

- **Status:** Accepted
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

Amends ADR-0322 (transport of the operation requests).

## Context

The relay drops every event except `at`, `sync` and `write` (ADR-0334). The web Terminal ignores an `at` whose
`event` is not `state`, `write` or `read`. A web Terminal host writes every `write` payload to the device as
`String(payload) + "\r\n"`.

## Decision

- **Host → joiners:** `at {event: "hwio", data: {v: 1, type, …}}` with `type` one of `hello` (app, version, device
  name/serial/transport/firmware, `ops: true|false`), `op-accepted`, `op-denied`, `op-progress` (max 4/s),
  `op-result` (`ok`, error code). The web Terminal ignores these.
- **Joiner → host:** `write` whose payload is a JSON **object** `{hwio: {v: 1, type: "hello"|"op-request", …}}`.
  It is never a string, so a typed shell line can never be mistaken for a tunnel message. A Monitor joiner sends
  tunnel objects only after it has received the host's `hello` in this session. A web Terminal host never sends
  `hello`, so its device never receives them (a spoofed `hello` at worst makes the device print an
  unknown-command line).
- The host validates every field and accepts tunnel messages only from an approved session state (ADR-0335).
  Only one operation runs at a time (`busy` otherwise). The firmware comes only from the catalog by ID (ADR-0323).
- The joiner accepts responses only for its own pending operation id. Results are shown as "reported by the
  customer's Monitor".

## Consequences

- Works on the unchanged relay. Web Terminal peers see only the console.
- Every participant receives the tunnel traffic too (the relay broadcasts). It contains no secrets.
