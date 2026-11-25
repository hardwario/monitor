# ADR-0414 — Remote session protocol: capabilities and optional fields, `v` stays 1

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended (plan [0059](../features/0059-remote-support-new-devices.md), "Approve as recommended")

## Context

Plan 0059 adds devices whose targets are not `app` / `lte` and firmware that is not in the catalog. The tunnel of
ADR-0336 is fixed to CHESTER. `op-request.target` is the enum `app | lte`. Older peers reject `v != 1`
(`libs/remote/src/tunnel.cpp:105-107`) and unknown error ids in `op-denied` / `op-result` / `end` (`tunnel.cpp:70-80`).
An older host answers an undecodable request with nothing, so the joiner waits 90 s (`remote_host.cpp:455-460`,
`remote_operation_client.h:35`). The relay broadcasts, and the host cannot tell who sent a `write` or whether a
participant left. Unknown fields of the existing messages are ignored by older peers (`tunnel.cpp:124-178`).

## Options considered

1. Optional fields plus a `caps` list in both hellos; `v` stays 1.
2. `v: 2` messages.
3. New relay events (the relay drops them).

For error ids: (a) a baseline `code` plus an optional `detail.code`; (b) send new ids as they are; (c) choose per peer
(impossible, because messages are broadcast).

For old joiners and a non-CHESTER host: (a) legacy `ops: false`; (b) `ops: true` with the old `app` / `lte` UI.

## Decision

Proposed (recommended in plan 0059, D-2 / D-3 / D-4 / D-16):

- `v` stays 1. The host hello gets the optional fields `caps`, `profile`, `console` and `targets`
  (`[{id, kinds, catalog, prepared:[{id,name,version}]}]`). The joiner hello gets `caps`. All are bounded: `caps` ≤ 16,
  `targets` ≤ 8, `prepared` ≤ 4, ids `[a-z0-9-]` ≤ 32 bytes.
- `op-request.target` is a profile target id string. A joiner sends a target other than `app` / `lte`, or
  `prepared`, only when the host hello lists it. The host validates against its own profile in every case.
- The legacy `ops` / `opKinds` describe only what a 2.0.0-rc.4 joiner can request correctly. They are unchanged on
  CHESTER and `ops: false` on other profiles.
- `op-denied`, `op-result` and `end` carry a `code` from the frozen 2.0.0-rc.4 baseline set plus optional
  `detail: {code}` with the current id. The mapping is by category and covered by a test.
- New host message types are sent only behind a cap seen in the session's last joiner hello. Older joiners show
  unknown types as warnings.

## Consequences

Old peers keep working and degrade to console only, never into a silent timeout. One mapping table must be kept up
to date when error ids are added (a test fails otherwise). Capabilities apply per session, not per participant. With
mixed old and new joiners, the legacy fields decide what the old ones see.
