# ADR-0278 — Prepared CHESTER LoRaWAN module firmware: lora-modem 1.4.1, pinned download

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (plans 0039 / 0040 planning session)

## Context

The CHESTER LoRaWAN module (Murata Type ABZ, STM32L072CZ) should be flashable with prepared firmware (ADR-0274).
The LTE firmware already uses a designated firmware pinned in the profile (ADR-0165).

## Options considered

- Source: **pinned download, as ADR-0165 (chosen)** / bundled in the app as a Qt resource / a HARDWARIO-hosted
  manifest read at run time.
- Firmware: **hardwario/lora-modem v1.4.1, asset `lora-modem-1.4.1.chester.hex` from the GitHub release (chosen)** /
  a HARDWARIO-hosted copy / another firmware.

## Decision

- The CHESTER `lrw` target (plan 0040) gets a designated firmware: name "lora-modem for CHESTER", version 1.4.1, URL
  `https://github.com/hardwario/lora-modem/releases/download/v1.4.1/lora-modem-1.4.1.chester.hex`, the SHA-256
  measured when it is added, kind Intel HEX.
- Download, verification, caching and cache re-verification follow ADR-0165 / ADR-0037.
- Designated firmware can be defined per part (plan 0039 G6), so another LoRaWAN chip on a later CHESTER gets its
  own entry.

## Consequences

- A new lora-modem version needs an app release.
- The downloader must follow GitHub's HTTPS redirect to its asset host (plan 0040 D2) or the file is re-hosted.
