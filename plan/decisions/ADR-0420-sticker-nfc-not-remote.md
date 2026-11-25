# ADR-0420 — STICKER over NFC is not shared in a remote session (for now)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended (plan [0059](../features/0059-remote-support-new-devices.md), "Approve as recommended")

## Context

The NFC STICKER session (0041) is request / reply over the ST25DV mailbox with AES-CCM, keyed from the customer's
plaintext key CSV (ADR-0344). MCP is read-only (ADR-0347). The GUI (0042 / 0043) is not built. The relay cannot
authenticate senders, and `at` events reach unapproved participants.

## Options considered

- N0: not supported.
- N1: the customer sends a redacted read-only snapshot.
- N2: support requests reads.
- N3: support requests changes, each confirmed with a diff.

## Decision

Proposed (plan 0059, D-18):

- N0 in plan 0059. N2 becomes a later plan after 0042 / 0043.
- N3 only with its own ADR.
- In every option, the secret key and vendor token never leave the host. The relay carries only decoded values with
  the ADR-0347 redaction.
- Joiner requests are JSON intents. The host builds every frame with its own counter, so there is no frame replay
  over the relay.

## Consequences

Support gets no NFC view yet. The protocol (caps, ADR-0414) leaves room for it.
