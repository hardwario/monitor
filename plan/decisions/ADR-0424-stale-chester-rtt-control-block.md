# ADR-0424 — Stale CHESTER RTT control block after flashing another firmware

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, as recommended) (plan [0060](../features/0060-test-feedback-2026-11.md), D4; after the hardware retest B0)

## Context

After flashing a different CHESTER application (Clime v4.0.1 → Serial RS-485 v4.0.1) the console reports
`target.rtt_buffer_not_found` ("Terminal") until the device is powered off (tester, 2026-11-20). Probable cause:
the old firmware's RTT control block survives the reset in RAM and Zephyr's STRONG_CHECK init keeps it, so buffer
0's name pointer no longer points to "Terminal". Monitor resolves buffers by name only (ADR-0034, as rttt 1.9.2 and
py-hardwario); the old Qt app used fixed indexes 0 / 1. The H6 fix (re-armed search, `b3b443f`) does not help.
Plan 0057 (ADR-0398) adds an immediate index fallback for the generic `nrf` profile only. For CHESTER an immediate
fallback could attach to the bootloader's control block found right after a reset (H6), which never has
"Terminal".

## Options considered

1. Hint only: the error suggests switching the device off and on.
2. **Hint, plus a late index fallback for the CHESTER app target: buffer 0 / 1 when "Terminal" is still missing
   after 5 s of re-armed searches; a zero-sized buffer 0 is never taken.**
3. Immediate fallback as ADR-0398.
4. After a flash by Monitor, clear stale "SEGGER RTT" IDs in target RAM before the final reset.

## Decision

Option 2 (accepted 2026-11-24), once the hardware retest confirms the cause; option 4 is evaluated in that retest only.

## Consequences

- Deliberate deviation from rttt / py-hardwario (name-only); the log says when the fallback was used.
- Reuses `RttNames` index fallbacks of plan 0057 (merge order: 0057 first).
- A stale block at another address than the new firmware's could still be picked; the retest checks it.
