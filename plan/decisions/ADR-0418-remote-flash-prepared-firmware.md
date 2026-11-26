# ADR-0418 — Remote flash of prepared (pinned) firmware

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended (plan [0059](../features/0059-remote-support-new-devices.md), "Approve as recommended")

## Context

ADR-0323 allows only catalog firmware IDs. STICKER firmware is not in the catalog. Plan 0058 pins a release image
by URL and SHA-256 in the profile (`DesignatedFirmware`, ADR-0408), with debug and provisioning images as Expert-mode
alternatives. The request asks for the debug console remotely. ADR-0363 already lets support request the host's own
recommended firmware (LTE) without an ID.

## Options considered

- Prepared release + debug.
- Release only.
- None.

Provisioning is excluded in every option. Gating the debug image on the customer's UI mode, or not.

## Decision

Proposed (plan 0059, D-13 / D-14), amends ADR-0323:

- A flash request may name `prepared: "<id>"` from the host's hello instead of a catalog ID. The host downloads that
  profile's `DesignatedFirmware` from its pinned URL and checks its pinned SHA-256. No firmware, URL or digest crosses
  the relay.
- STICKER offers `release` and `debug`.
- The customer's UI mode does not gate the request, because the customer confirms a text that names the flavour and
  its battery cost.

## Consequences

Support can get a debug console on a customer's STICKER without the customer switching to Expert mode. The debug
image may be left on the device, which is mitigated by the texts.
