# ADR-0365 — A joiner accepts relay events up to 512 KiB so the host's whole command list arrives

- **Status:** Accepted (amends ADR-0328 limits and ADR-0364)
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (review of the plan 0044 amendment implementation,
  plan [0044](../features/0044-remote-support-session.md) § Remote autocomplete)

## Context

ADR-0364 lets a Monitor host send its command tree (`commands`, up to 256 KiB, 4000 nodes, depth 8). Every relay
event was limited to 64 KiB on receipt (plan 0044 § Limits), so the first implementation sent a large tree without
its help texts, or not at all.

## Options considered

1. Keep 64 KiB; send large trees without help texts, or not at all.
2. Raise the limit a joiner applies to incoming relay events so a 256 KiB tree in its tunnel encoding always fits
   (chosen).

## Decision

- A joiner (Monitor in a remote tab, `hwio remote join`) accepts incoming relay events up to **512 KiB** (a 256 KiB
  tree plus its envelope, with a generous margin; the tree travels as a JSON object, not escaped twice). Larger events are dropped as before.
- A host keeps the **64 KiB** limit for incoming events: what it receives comes from joiners and stays small.
- The host sends the whole tree, help texts included, when it fits the joiner's tree bound (256 KiB) and event limit
  (512 KiB); otherwise it falls back to the tree without help texts (logged), or sends none.
- The joiner's validation of the tree (256 KiB, 4000 nodes, depth 8, name charset) is unchanged.

## Consequences

- Support gets the full completion, help texts included, for any device the bounds allow.
- A joiner may buffer up to 512 KiB per event; the incoming queue bound (ADR-0357) is unchanged.
- The relay must pass such events: the Socket.IO server's default 1 MB message limit covers them.
