# ADR-0275 — Curated STM32 part list; the detected chip must match the chosen one

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (plan 0039 planning session)

## Context

The user picks the STM32 chip. J-Link knows thousands of device names; a wrong choice programs with a wrong flash
loader or address map. STM32 chips report their device ID in DBGMCU_IDCODE (DEV_ID, REV_ID) and their flash size in
a system-memory register.

## Options considered

- List: **compiled-in curated list (chosen)** / curated + free-form J-Link name / the whole J-Link device list.
- Mismatch: **verify and block (chosen)** / detect and preselect / warn only.

## Decision

- STM32 parts are compiled-in data (ADR-0032): id, display name, J-Link device name, IDCODE address, DEV_ID, flash
  size register and expected size, flash / data EEPROM / RAM ranges, protection data. At most 32 parts.
- After connecting, every operation except `info` reads DBGMCU_IDCODE (and the flash size when the part defines it);
  a different chip stops the operation with `target.wrong_chip` before anything is written. When the found chip is a
  curated part, the GUI offers "Use <found>" (never switches silently); the CLI names the matching `--chip` id.
- Address checks (`checkFlashWrite`) apply to STM32 as to nRF: flash and, when allowed, data EEPROM; option bytes
  and system memory are never written by an image.

## Consequences

- A chip outside the list needs a new release (a part entry and its tests).
- Wrong-chip flashing cannot happen through the app.
