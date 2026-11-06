# ADR-0276 — STM32 read-out protection: detect, explain, unlock only on confirmation

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (plan 0039 planning session)

## Context

STM32 RDP level 1 blocks debugger access to flash; the original Murata firmware of the Type ABZ sets it on first boot.
Level 1 → 0 mass-erases flash and data EEPROM. Level 2 is permanent. For nRF the equivalent is `recover` (ADR-0039).

## Options considered

1. **Detect RDP, explain, offer "Unlock (erases the chip)" with a confirmation; never automatic (chosen).**
2. Report only; the user unlocks with STM32CubeProgrammer.

## Decision

- Flash / erase on a level 1 chip fail with `target.protected` (STM32 text) and offer **Unlock…**; the confirmation
  (ADR-0038) states that flash and data EEPROM are erased. CLI `hwio stm32 unlock` / `hwio chester lrw unlock` ask or
  need `--yes`; MCP needs an explicit confirm argument plus the GUI approval.
- Level 2 → `target.permanently_protected`; nothing is attempted.
- The unlock only ever programs RDP = `0xAA` (level 0). No code path may program `0xCC`; a unit test asserts it.
- The J-Link library must never unlock or mass-erase on its own; plan 0039 Phase 0 proves that or disables it.

## Consequences

- One more destructive operation with its confirmation texts, docs and tests.
- The implementation (own option-byte sequence vs J-Link built-in) is decided in plan 0039 Phase 0.
