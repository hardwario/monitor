# ADR-0282 — STM32L0 unlock ends with a canonical RDP word; the L010 value line is only named in the hint

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (plan 0039 Phase 0, implementation session)

## Context

Plan 0039 Phase 0 (CHESTER-S1 controller, STM32L0 cat. 3, J-Link 9.84, 2026-11-06):

- The own RDP level 1 → 0 sequence (ADR-0280 D7) works: PECR and option-byte keys, write the RDP word with RDP `0xAA`
  and its complement, `OBL_LAUNCH`; the chip mass-erases flash and data EEPROM and comes back at level 0. In two
  runs the stored word read back `0x015500AA` instead of the canonical `0xFF5500AA` (upper complement byte `0x01`);
  FLASH_OPTR still loaded RDP `0xAA` and FLASH_SR showed no OPTVERR. Writing the canonical word again at level 0 (no
  regression, so no erase) and reloading fixed it.
- The STM32L010 value line has the same DEV_ID and flash sizes as other STM32L0 parts but a smaller data EEPROM
  (ST cmsis_device_l0: L010x8 256 B vs L05x 2 KiB, L010xB 512 B vs L07x 6 KiB); the app cannot tell them apart.

## Options considered

- Unlock end: **rewrite the word when it is not canonical (chosen)** / verify FLASH_OPTR only.
- Value line: **one entry per category and size, the hint says "not for STM32L010" (chosen)** / separate L010
  entries the user must pick.

## Decision

- `unlock` = clear FLASH_SR error flags → unlock PECR and the option bytes → write the RDP word (RDP `0xAA`, byte 1
  kept from FLASH_OPTR as ST's HAL does, complement in bits 31:16) → `OBL_LAUNCH` → reconnect and re-identify →
  success needs FLASH_OPTR RDP = `0xAA` and no OPTVERR. When the stored word at `0x1FF80000` is then not canonical,
  the same write is repeated once at level 0 and the option bytes are reloaded again; a still non-canonical word is
  logged as a warning (the chip is at level 0 and usable). `0xCC` is never written (ADR-0276).
- The curated parts stay one entry per category and flash size (ADR-0279); the chip picker hint and the docs say
  that the STM32L010 value line is not covered (its data EEPROM is smaller).

## Consequences

- One more option-byte write per unlock (at level 0, no erase), covered by fake J-Link tests.
- An STM32L010 user can still flash the code flash with the matching entry; EEPROM records beyond the real EEPROM fail
  on the probe.
