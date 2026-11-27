# ADR-0280 — STM32 flashing: `.bin` input, own unlock sequence, console-less tabs, MCP tools

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (approved plan 0039 with all recommended options)

## Context

Plan 0039 left four proposed decisions (D5, D7, D8, D9) for review.

## Options considered

See plan 0039, "Decisions requiring user confirmation", D5, D7, D8, D9.

## Decision

- **D5:** `.bin` files are accepted; the start address defaults to the part's flash start (`0x08000000`), CLI
  `--address` overrides it. `.bin` input is bounded by the part's flash size.
- **D7:** the RDP level 1 → 0 unlock uses our own option-byte sequence from the family data, not the J-Link
  library's built-in unsecure handling. Phase 0 P0.4 confirms it on hardware; if it shows the own sequence is not
  viable, the plan is updated and the user is asked before anything else is used.
- **D8:** a tab of a chip without a console keeps its Console page, which shows "This chip has no console in
  HARDWARIO Monitor."; Flash and Maintenance work as usual.
- **D9:** MCP tools `stm32_info`, `stm32_flash`, `stm32_erase`, `stm32_unlock` (and the `lrw` target through the
  existing tools); `stm32_unlock` needs an explicit `confirm: true` argument and the GUI approval (ADR-0101).

## Consequences

- One more input format (`FirmwareImage::fromBinary`) with its tests.
- The unlock is testable on the fake J-Link and never depends on undocumented library behaviour.
