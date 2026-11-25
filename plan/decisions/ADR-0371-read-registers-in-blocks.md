# ADR-0371 — Registers are read in blocks of 32; unconfirmed registers are left out

- **Status:** Accepted
- **Date:** 2026-11-13
- **Decided by:** Matej Strnad (plan 0048)

## Context

`read_registers` (ADR-0099) passed the whole `JLINKARM_GetRegisterList` list (115 entries on nRF52840) to one
`JLINKARM_ReadRegs` call. J-Link Software V9.84 handles only about 45 registers per call: it returns 0, writes
junk into the remaining values and leaves their status bytes untouched. Smaller calls (≤ 20 measured, any mix of
indexes) return correct values with status 0. The adapter did not evaluate the statuses (pylink calls them
unreliable) and zero-filled them, so the junk was reported as register values (plan 0048, measurement table).

## Options considered

Where: (1) inside the J-Link adapter (`JLinkLibrary`), (2) in `JLinkProgrammer`.
How: (a) `JLINKARM_ReadRegs` in blocks of at most 32, (b) `JLINKARM_ReadReg` per register.
Unconfirmed register: (i) leave it out and log, (ii) fail the whole call, (iii) report the value as null.
Registers the core does not have (`*_NS`, `*_S`, `PAC_KEY_*`, …): keep them, or filter by core.

## Decision

- The J-Link adapter reads in blocks of at most `maxRegistersPerRead = 32`. The vendor quirk stays in
  `libs/hw/jlink`; `JLinkApi::readRegisters` gains a `confirmed` output span so callers know which values the
  library confirmed.
- The status buffer is pre-filled with a marker (`0xFF`); a register is confirmed only when the library wrote
  status 0 for it.
- `JLinkProgrammer` leaves unconfirmed registers out of the result and logs them (bounded); if none is confirmed
  the call fails with `TargetConnectFailed`. No schema change, no new error code.
- Registers listed by the library that the core does not have stay in the output; filtering by core is a
  possible later plan.

## Consequences

- `read_registers` / the CLI register command show correct values for the full list (4 vendor calls instead of 1).
- A future library with a smaller limit or unreliable statuses leads to omitted registers plus a log line, never
  to wrong values.
- The fake J-Link library and the scripted API gain an optional per-call limit (`readRegsLimit`) so tests cover
  the vendor behaviour.
- The `JLinkApi` interface change touches the mock and the scripted API.
