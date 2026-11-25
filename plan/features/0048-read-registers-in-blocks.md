# 0048 — read_registers: read in blocks, never report a value the library did not confirm

- **Status:** Done (approved 2026-11-13, Matej Strnad)
- **Milestone:** 2.0.0-rc.5
- **Author:** Claude (with Matej Strnad)
- **Related ADRs:** ADR-0371 (registers are read in blocks of 32, unconfirmed ones are left out), ADR-0099 (MCP tool
  set, `read_registers`), ADR-0028 (own JLinkARM declarations), ADR-0007 (errors)
- **Depends on:** plan 0013 (M6 MCP core, `readRegisters` in `libs/hw/jlink` / `libs/device`)

## Context

Found on 2026-11-13 while testing MCP on the installed 2.0.0-rc.4 (GUI endpoint) with a CHESTER-M
(nRF52840, J-Link serial 801047277, J-Link Software V9.84):

- `read_registers` returned 115 registers. R0–R15, xPSR, the special registers and FPS0–FPS13 were correct.
  FPS14–FPS31, `CycleCnt`, the `*_NS` / `*_S` banked registers, `MSPLIM`/`PSPLIM`, `BASEPRI_BASE0`…, `VPR` and
  `PAC_KEY_*` held garbage that looked like UTF-16 text (`0x0064006F` = "d","o").
- `JLinkProgrammer::readRegisters()` (`libs/hw/jlink/src/programmer.cpp`) passes the whole list from
  `JLINKARM_GetRegisterList` to **one** `JLINKARM_ReadRegs` call; `JLinkLibrary::readRegisters()`
  (`libs/hw/jlink/src/jlink_library.cpp`) zero-fills the status buffer and does not evaluate it (the pylink
  comment calls the statuses unreliable).

Measured directly against `/opt/SEGGER/JLink/libjlinkarm.so` V9.84 (probe released through MCP
`release_probe`, CPU halted; status buffer pre-filled with `0xAA`):

| `JLINKARM_ReadRegs` call | Return | Status bytes written |
|---|---|---|
| all 115 indexes | 0 | first 45 (R0 … FPS13); the rest untouched, values junk |
| the 70 indexes from FPS14 on | 0 | first 45 (FPS14 … `BASEPRI_MAX_BASE0`); stops at `VPR` |
| 20, 20, 18 or 6 indexes (any mix, incl. FPS14, CycleCnt, MSP_NS) | 0 | all, values correct (= `JLINKARM_ReadReg`) |

So the library handles **at most about 45 registers per call**, silently: it returns 0, writes junk into the
rest of `paData` and leaves their `paStatus` bytes as they were. Because we zero-fill the status buffer, even
evaluating the statuses would not have caught it. Every status the library *did* write was 0.

pylink's `register_read_multiple()` and therefore rttt 1.9.2 `read_registers` have the same problem whenever the
list is longer than the limit; there is no reference behaviour to copy.

## Goals / Non-goals

Goals:
- `read_registers` (MCP), `hwio chester app regs` (CLI) and `DeviceSession::readRegisters` return
  correct values for every register the library lists (115 on nRF52840 with V9.84).
- A register whose value the library did not confirm is never reported with a value: it is left out and logged.
- The fake J-Link library and the scripted API reproduce the limit, so a unit/e2e test fails on the old code.

Non-goals:
- No filtering of registers the core does not have (`*_NS`, `*_S`, `MSPLIM`, `PAC_KEY_*`, `VPR` on ARMv7-M).
  With block reads they come back as 0 or as the aliased register; a separate plan may filter by CPUID later.
- No change of the MCP / CLI output schema, no new error code, no new user-visible string.
- No change of the `maxRegisters = 128` cap.

## User experience

None in the GUI (no register view). MCP and CLI: the same JSON shape; the values are now correct; a register the
library did not confirm is absent instead of carrying junk.

## CLI

No new commands or options. The existing register command (`app/cli/src/target_commands.cpp`, `regs`) returns
the corrected list; human and `--json` output formats are unchanged; exit codes unchanged.

## Design

### `libs/hw/jlink` adapter (ADR-0371)

`JLinkApi::readRegisters` gains one output span so the caller learns which values the library confirmed:

```cpp
// JLINKARM_ReadRegs of `indexes` into `values`, in blocks of at most maxRegistersPerRead (ADR-0371: the
// library handles about 45 per call and silently leaves the rest). confirmed[i] = 1 when the library wrote
// status 0 for indexes[i], else 0 (its value is then unspecified). All three spans have the same size.
// Returns the number of confirmed registers (>= 0) or the first negative vendor code.
[[nodiscard]] virtual int readRegisters(std::span<const std::uint32_t> indexes,
                                        std::span<std::uint32_t> values,
                                        std::span<std::uint8_t> confirmed) = 0;
```

- `inline constexpr std::size_t maxRegistersPerRead = 32;` in `jlink_api.h`, next to `maxRegisterListItems`, with
  a comment pointing to ADR-0371 and the measurement above.
- `JLinkLibrary::readRegisters` (`jlink_library.cpp`): size check of the three spans (else -1); for each block of
  ≤ 32 indexes: pre-fill a local status vector with the marker `0xFF`, call `fn.readRegs` for the block; a
  negative result is returned at once; afterwards `confirmed[i] = (status[i] == 0)`. The return value is the sum
  of confirmed entries. The "not evaluated (pylink: unreliable)" comment is replaced by a reference to ADR-0371.
- `jlink_abi.h`: the `ReadRegsFn` comment notes the per-call limit and that statuses are evaluated against a
  marker (ADR-0371). The declaration itself is unchanged.
- Bounds: blocks ≤ 32, total ≤ `maxRegisterListItems` (256, already enforced by `registerList()`); the status
  vector is per block (≤ 32 bytes).

### `JLinkProgrammer::readRegisters` (`programmer.cpp`)

- Allocates `confirmed` next to `values`, calls the new signature.
- Builds `RegisterValue`s only for `confirmed[i] != 0`, in the library's order.
- If any register is not confirmed: one `qCInfo(log::lcJLink)` line with the count and up to 8 names
  (bounded), e.g. `Registers not confirmed by the library: 3 (FOO, BAR, #77)`. No user-visible error.
- If **no** register is confirmed while the list was non-empty: `ErrorCode::TargetConnectFailed` with
  `libraryDetail("JLINKARM_ReadRegs confirmed no register")` — same code as today's `ReadRegs` failure.
- Thread: unchanged (session worker; vendor calls serialized there).

### Test doubles

- `libs/hw/jlink/tests/mock_jlink_api.h`: `MOCK_METHOD` updated to the new signature.
- `libs/device/tests/scripted_api.h`: new signature; fills `confirmed` with 1; honours an optional per-chip
  `readRegsLimit` like the fake library (below).
- `tests/fake_jlink/fake_jlink.cpp` `JLINKARM_ReadRegs`: new optional scenario field `chip.readRegsLimit`
  (integer, default 0 = unlimited). When set, only the first `readRegsLimit` entries of a call get their value and
  status 0; the rest get value `0x00AD9390` and their status byte is not touched — exactly the V9.84 behaviour.
  Document the field in the scenario comment block at the top of the file.

### Errors

No new `hwio::Error` codes. Existing: `target.not_halted`, `TargetConnectFailed` (vendor error or nothing
confirmed), missing optional function (`require`).

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Where the workaround lives | J-Link adapter / JLinkProgrammer | **J-Link adapter** (2026-11-13). Consequence: `JLinkApi::readRegisters` gains the `confirmed` span so the programmer can leave registers out | ADR-0371 |
| How registers are read | `ReadRegs` in blocks of 32 / `ReadReg` one by one | **Blocks of 32** (2026-11-13) | ADR-0371 |
| Register the library did not confirm | Omit and log / fail the call / value null | **Omit and log** (2026-11-13) | ADR-0371 |
| Registers the core does not have | Keep (out of scope) / filter by core | **Keep, out of scope** (2026-11-13) | ADR-0371 |

## Implementation steps

- [x] `jlink_api.h`: `maxRegistersPerRead`, new `readRegisters` signature and comment.
- [x] `jlink_library.cpp` / `.h`: block loop with marker statuses; `jlink_abi.h` comment.
- [x] `programmer.cpp`: `confirmed` handling, bounded log line, "nothing confirmed" error.
- [x] `mock_jlink_api.h`, `scripted_api.h`: new signature (+ `readRegsLimit` in the scripted API).
- [x] `tests/fake_jlink/fake_jlink.cpp`: `chip.readRegsLimit` scenario field, documented.
- [x] Unit tests (below); update `RegistersAreCappedAt128` and the existing register tests to the new signature.
- [x] CLI e2e test with the fake library and `readRegsLimit: 45`, > 45 registers.
- [x] Build, ctest, tidy, format (all at low priority: `nice -n 19 ionice -c 3`).
- [x] HIL check on CHESTER (below); plan status → Done.

## Tests and verification

Unit (`libs/hw/jlink/tests`):
- `JLinkLibrary` against the fake library: 115 registers with `readRegsLimit: 45` → all 115 confirmed with the
  scenario values; `fn.readRegs` called in blocks of ≤ 32 (4 calls).
- `JLinkLibrary`: a block whose call returns < 0 → that code is returned.
- `JLinkLibrary`: span size mismatch → -1, no vendor call.
- `JLinkProgrammer` (mock): some `confirmed` = 0 → those registers are absent, the others keep order and values.
- `JLinkProgrammer` (mock): all `confirmed` = 0 → `TargetConnectFailed`.
- `RegistersAreCappedAt128` still holds (the cap is applied before reading).

Device / MCP / CLI:
- `libs/device` target-session test with the scripted API and `readRegsLimit` → full, correct list.
- `libs/mcp_tools/tests/jlink_tools_test.cpp`: `read_registers` with > 45 registers returns them all.
- `app/cli/tests/target_e2e_test.cpp`: fake library scenario with 60 registers and `readRegsLimit: 45` → every
  value matches the scenario. This test must fail on the current code (verify once before the fix).

Sanitizers / static: `linux-asan`, `linux-tsan` ctest; `linux-tidy` zero findings; clang-format clean.

Manual / HIL (CHESTER-M, J-Link V9.84): MCP `halt` → `read_registers` → `go`. Expect 115 registers, FPS14–FPS31
and `CycleCnt` no longer `0x00AD9390`/text-like, R0–R15 equal to `JLINKARM_ReadReg` (spot-check as in the
measurement script), `go` resumes and the console keeps running.

## Risks and open points

- The limit is undocumented and may differ between J-Link versions or cores; 32 leaves a margin below the 45 we
  measured. If a library confirms fewer than requested, the omit-and-log path keeps the output correct and the
  log line shows it.
- pylink calls the statuses unreliable. On V9.84 every written status was 0. If a library writes non-zero for
  good reads, registers would be omitted (visible in the log and in the HIL check), never shown with a wrong value.
- Registers the core does not have stay in the output (non-goal); worth a follow-up plan that filters by CPUID.
- rttt / pylink have the same bug; worth reporting upstream (not part of this plan).
