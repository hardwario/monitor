# ADR-0274 — STM32 chips: a CHESTER LoRaWAN card plus a generic STM32 device

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (plan 0039 planning session)

## Context

HARDWARIO devices carry STM32 chips (the Murata Type ABZ LoRaWAN module on CHESTER, other STM32L0 chips). The user
wants to pick which chip it is and flash it; chips on CHESTER should have their firmware prepared. The Flash page has
two cards today (ADR-0174), the Add device picker chooses a profile and a target (ADR-0050).

## Options considered

1. **A third card "LoRaWAN module" on the CHESTER Flash page (prepared firmware) plus a generic "STM32 chip" device
   in Add device for any curated chip with the user's own file (chosen).**
2. One "STM32 chip" flow everywhere; CHESTER chips appear as presets.
3. CHESTER only, no generic STM32 flashing.

## Decision

- New device profile `stm32` ("STM32 chip"): one target per curated part (ADR-0275); in Add device the target picker
  is labelled "Chip". No console, no BLE, no catalog.
- CHESTER targets that are STM32 chips (plan 0040: `lrw`) are ordinary targets of the CHESTER profile and get their
  own card on the CHESTER Flash page, with the prepared firmware preselected (ADR-0278).
- `TargetProfile` gets a vendor field (Nordic / STM32); the part data of an STM32 chip is defined once and shared by
  the `stm32` profile and CHESTER.

## Consequences

- Supplements ADR-0174 (a third card only on profiles that have an STM32 target) and ADR-0050 (third profile).
- Tabs without an RTT console must be supported (Flash and Maintenance only).
- New HARDWARIO STM32 chips = a part entry (+ optionally a CHESTER target and prepared firmware), no code.
