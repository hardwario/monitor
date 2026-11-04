# ADR-0253 — The guided flash wizard has no firmware-kind step

- **Status:** Accepted (plan 0035)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

ADR-0245 limits the 0010 wizard to application firmware; LTE firmware goes through `lte-update` (ADR-0234). 0010
still has a step "What do you want to flash?" with two kind cards (0021 amendment).

## Options considered

1. **No kind step: the wizard is "Flash application firmware"; the Firmware step has a link "Updating the LTE
   firmware? [Update LTE firmware…]" that closes this wizard and opens `lte-update` (chosen).**
2. Keep the two kind cards; LTE hands off to `lte-update` at once.

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04).

## Consequences

- Steps: Probe · Cable · Firmware · Confirm · Flash · Done.
- `firmware.out_of_range` with an image that fits the LTE MCU explains "This is LTE firmware" and offers the
  `lte-update` hand-off instead of switching a kind.
