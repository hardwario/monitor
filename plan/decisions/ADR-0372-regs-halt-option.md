# ADR-0372 — Registers of a running core: an explicit halt option that restores the prior state

- **Status:** Accepted
- **Date:** 2026-11-13
- **Decided by:** Matej Strnad (plan 0049)

## Context

Reading core registers needs a halted CPU (`target.not_halted` otherwise, ADR-0099). Every CLI command is its own
process that opens, connects and closes the J-Link probe. On hardware (CHESTER-M nRF52840, J-Link V9.84)
`hwio chester app halt` followed by `hwio chester app regs` fails with exit 6 "The CPU is running": after the
next connect the core runs again. Our code does not call `JLINKARM_Go` on close; whether the halt is lost in
`JLINKARM_Close` or in `JLINKARM_Connect` is not known yet. Within one process (MCP `halt` → `read_registers` →
`go`) reading works. py-hardwario has no halt / go / regs commands.

## Options considered

How `regs` works across CLI processes:
1. An explicit option `regs --halt`: halt, read, let it run again — one process.
2. `regs` always halts and resumes implicitly.
3. Keep a halt across processes (make `halt` persist until `go` / reset); `regs` stays strict.

State after the read: (a) restore the prior state (resume only if this command halted the CPU), (b) always resume.

Scope: CLI only, or CLI and MCP `read_registers`.

Root cause: measure Close vs. Connect on hardware, or skip.

## Decision

- Option 1: `hwio chester app|lte regs --halt`. Without the option `regs` stays strict (exit 6 on a running core).
  Option 2 was rejected because it stops a running application without being asked; option 3 because it depends
  on vendor library behaviour we do not control and would leave a device halted after the tool exits.
- (a) Restore the prior state: the CPU is resumed only if the command halted it, and it is resumed also when the
  read failed.
- The backend gets the option (`DeviceSession::readRegisters(targetId, {.haltIfRunning = true})`, on the session
  worker); the CLI and MCP `read_registers` (optional boolean `halt`, default false) both expose it.
- Plan 0049 starts with a hardware diagnosis (only with the user's OK at the time) of whether `JLINKARM_Close`
  or `JLINKARM_Connect` resumes the core. If `reset --halt` / `flash --halt` are affected, that is a separate plan.

## Consequences

- `regs` becomes usable from the CLI on hardware; AI clients need one MCP call instead of three.
- The halt / read / go sequence lives in `libs/device`, so the CLI keeps no logic of its own.
- The read briefly stops the firmware (milliseconds); the option makes that explicit.
- A halt-read operation does not change the session's halt bookkeeping (ADR-0157 / ADR-0034), because it restores
  the state it found.
- `--halt` means "stay halted" on `reset` / `flash` but "halt for the read" on `regs`; the help text must say so.
