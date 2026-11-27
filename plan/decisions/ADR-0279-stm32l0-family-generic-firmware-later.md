# ADR-0279 — First STM32 scope: the whole STM32L0 family, generic; prepared firmware added later

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (plan 0039 planning session)

## Context

Known STM32 chips on HARDWARIO products are all STM32L0: the Murata Type ABZ LoRaWAN module (STM32L072CZ, cat. 5) and
the controller of the CHESTER-S1 IAQ sensor board in CHESTER Clime IAQ (cat. 3, 64 KiB, measured 2026-11-06). The
S1 firmware source is unknown. The user: "do it generically for these chips, we'll get the firmware later; they are
similar STM32 everywhere."

## Options considered

1. Only the two known chips, each with its own target and firmware.
2. **The whole STM32L0 family as generic curated parts; prepared firmware per part when available (chosen).**

## Decision

- Plan 0039 describes STM32L0 identification (DBGMCU_IDCODE `0x40015800`, flash size `0x1FF8007C`), the flash
  interface and the RDP option-byte data **once per family**; the parts are entries per category and flash size
  (cat. 1, 2, 3, 5), named by category and size with known HARDWARIO uses as hints.
- The CHESTER-S1 chip is flashed through the generic STM32 device with the user's own file; no S1 target and no S1
  firmware in rc.4. When HARDWARIO provides the firmware, a small plan adds a `DesignatedFirmware` entry (and, if
  wanted, a CHESTER target `s1`).
- Other families (STM32WL, G0, …) are added by new plans as family + parts data.

## Consequences

- One protection / identification implementation covers every HARDWARIO STM32 chip known today.
- Within a category and size the app cannot tell parts apart (same DEV_ID); one entry and one J-Link device name per
  group, confirmed in plan 0039 Phase 0.
