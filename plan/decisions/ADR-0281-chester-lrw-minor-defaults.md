# ADR-0281 — CHESTER LoRaWAN module: GitHub redirect, card name, module check, wizard later

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (approved plan 0040 with all recommended options)

## Context

Plan 0040 left proposed decisions D2–D5 for review (D6, the CHESTER v2 LoRaWAN chip, stays open).

## Options considered

See plan 0040, "Decisions requiring user confirmation", D2–D5.

## Decision

- **D2:** the designated-firmware downloader may follow an HTTPS → HTTPS redirect to `*.githubusercontent.com`
  only (amends ADR-0037 for designated firmware); any other cross-host redirect or a downgrade to HTTP fails. The
  SHA-256 check (ADR-0165) is unchanged.
- **D3:** the Flash page card is called **"LoRaWAN module"**.
- **D4:** after flashing `lrw`, the page offers **Check module**: reconnect to `app`, send the application's LoRaWAN
  status command (found in Phase 0 P0.4) and show whether the module answered.
- **D5:** the flash wizard (0010) gets no LoRaWAN choice in rc.4; a later plan adds it.

## Consequences

- The downloader gets a host allow-list for redirects with tests.
- "Check module" depends on the CHESTER application's shell; when the command is unknown the button is hidden.
