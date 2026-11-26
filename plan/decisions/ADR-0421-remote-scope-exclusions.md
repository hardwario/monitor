# ADR-0421 — STM32 chip, CHESTER LoRaWAN module and non-CHESTER Bluetooth stay out of remote scope

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended (plan [0059](../features/0059-remote-support-new-devices.md), "Approve as recommended")

## Context

The STM32 chip (0039) has no RTT console and no prepared firmware; it is flashed from user files only. The CHESTER
`lrw` target (0040) has no console of its own. A cross-target `lrw` flash from an `app` tab pauses the `app` console,
which would be the shared one. Plans 0039 / 0040 are not merged (pull request #18). Only CHESTER has a Bluetooth
profile.

## Options considered

- Exclude them.
- Add `lrw` reset and prepared flash now.

## Decision

Proposed (plan 0059, D-19): excluded. The operations matrix (ADR-0417) can add `lrw` later as data.

## Consequences

Remote support for a LoRaWAN module issue goes through the CHESTER `app` console. Flashing `lrw` stays local.
