# 0049 — `regs --halt`: read the registers of a running core in one process

- **Status:** In progress (approved 2026-11-13, Matej Strnad; implemented 2026-11-19, hardware checks open)
- **Milestone:** 2.0.0-rc.5
- **Author:** Claude (with Matej Strnad)
- **Related ADRs:** ADR-0372 (registers of a running core: an explicit halt option, prior state restored),
  ADR-0371 (registers read in blocks), ADR-0099 (MCP tool set, `read_registers`), ADR-0157 (halt / go on the
  console target), ADR-0034 (no auto-reconnect while halted), ADR-0007 (errors)
- **Depends on:** plan 0048 (read registers in blocks; same code paths), plan 0013 (M6 MCP core, target operations)

## Context

Found on 2026-11-13 during the hardware check of plan 0048 (CHESTER-M nRF52840, J-Link V9.84):

```
hwio chester app halt   # → "CPU halted", exit 0
hwio chester app regs   # → exit 6, "The CPU is running." (target.not_halted)
```

Every CLI command is its own process: it opens the probe, connects (`JLINKARM_Connect`), runs one light
operation and closes (`JLinkProbe::close()` → `JLINKARM_Close`). After the next process connects, the core is
running again. Our code never calls `JLINKARM_Go` on close (`libs/hw/jlink/src/jlink_probe.cpp` `close()`), so
the halt is lost either inside `JLINKARM_Close` or inside the next `JLINKARM_Connect`; which one is not known yet
(phase 0 below measures it). The fake J-Link library already models "the target runs after every connect"
(`app/cli/tests/target_e2e_test.cpp`, `RegsOnARunningCore` expects exit 6).

Consequence: `hwio chester app regs` (same for `lte`) is unusable on hardware from the CLI. It only works inside
one process: MCP `halt` → `read_registers` → `go`.

Reference: py-hardwario has no `halt` / `go` / `regs` commands (they are new in v2, ADR-0099); it only has
`reset --halt` and `flash --halt` (`hardwario/chester/cli/app.py`), which end with the core halted at process exit.
If phase 0 shows that `JLINKARM_Close` resumes the core, those options are affected too — that is out of scope
here and becomes a separate plan.

## Goals / Non-goals

Goals:
- `hwio chester app regs --halt` (and `lte regs --halt`) works on hardware in one process: halt if running, read,
  restore the prior run state.
- MCP `read_registers` gains the same optional `halt` argument, so an AI client needs one call instead of three.
- The root cause (Close vs. Connect) is measured on hardware and written into this plan.
- `regs` without `--halt` stays as it is (strict: exit 6 on a running core); its error suggestion points to
  `--halt` in the CLI.

Non-goals:
- No attempt to keep a halt across CLI processes (ADR-0372, option rejected).
- No change of `halt`, `go`, `status`, `reset --halt`, `flash --halt`. If phase 0 shows that `--halt` of
  reset/flash does not survive the process end, a new Draft plan is written instead.
- No GUI change (there is no register view).
- No change of the register output schema (ADR-0371).

## User experience

GUI: none.

CLI:
```
$ hwio chester app regs
Error: The CPU is running.
Cause: Registers can be read only while the core is halted.
Suggestion: Use `regs --halt` to halt the CPU for the read and let it run again.   (new CLI-only text, see Errors)
$ hwio chester app regs --halt
R0   0x00000000
...
```
The CPU is stopped only for the duration of the read (a few ms plus 4 `ReadRegs` calls, ADR-0371). If it was
already halted (e.g. by a debugger in the same process — in practice only via MCP), it stays halted.

MCP: `read_registers {"halt": true}`; the description says what it does.

## CLI

| Command | Change |
|---|---|
| `hwio chester app regs [--halt]`, `hwio chester lte regs [--halt]` | new flag `--halt`: "Halt the CPU for the read and let it run again if it was running" |

- Human and `--json` output unchanged (`registersJson` / `registersText`).
- Exit codes unchanged: 0 success, 6 `target.not_halted` without `--halt` on a running core; with `--halt`
  the existing codes of halt / read errors (`TargetConnectFailed` etc.).
- Option name `--halt` matches the existing py-hardwario-style `--halt` of `reset` / `flash` (in those it means
  "stay halted afterwards"; here the help text says "for the read"). See open points.

## Design

### Phase 0 — diagnosis on hardware (only with the user's OK at the time)

On CHESTER-M + J-Link V9.84, record in this plan:
1. `hwio chester app halt`, then `hwio chester app status` → halted or running?
2. J-Link Commander: `connect`, `h`, `q`; then `connect`, `IsHalted` (`regs` shows "running"?).
3. A small measurement program / Commander script: `Connect`, `Halt`, `IsHalted` → `Close`, `Open`, (no
   `Connect`) read DHCSR (`0xE000EDF0`) via `JLINKARM_ReadMemU32` if possible → `Connect` → `IsHalted`.
4. `hwio chester app reset --halt`, then `status`.

Result: one paragraph "Root cause" plus whether `reset --halt` / `flash --halt` are affected (→ new Draft plan).
Phase 0 does not block phases 1–3; they do not depend on the root cause.

### `libs/device` (backend, ADR-0372)

```cpp
// DeviceSession (device_session.h)
struct ReadRegistersOptions {
    // Halt the core if it runs, read, then let it run again; an already halted core stays halted
    // (ADR-0372). Without it a running core gives target.not_halted.
    bool haltIfRunning = false;
};
[[nodiscard]] Result<OperationId> readRegisters(std::string_view targetId,
                                                ReadRegistersOptions options = {});
```

- `detail::OperationCommand` gets `bool haltIfRunning` (or the options struct), set only for `ReadRegisters`.
- `runReadRegisters(WorkflowContext&, bool haltIfRunning)` (`session_worker_target.cpp`), on the session worker
  thread (vendor calls serialized there, unchanged):
  1. `identified(context)` as today.
  2. If `haltIfRunning`: `programmer->isHalted()`; if running → `programmer->halt()`, remember `weHalted = true`.
  3. `programmer->readRegisters()`.
  4. If `weHalted`: `programmer->go()` — **always**, also when step 3 failed. If `go()` fails, the result is the
     `go` error when the read succeeded, else the read error (the first error wins), and the log line notes that
     the CPU may remain halted.
  5. Log lines (`log(context, …)`): "Halting the CPU to read the registers" / "Letting the CPU run again".
- Halt state bookkeeping (`noteHaltState`, `m_halted`, ADR-0157 / ADR-0034): a `ReadRegisters` with
  `haltIfRunning` does not change `m_halted`, because it restores the state it found. The console (RTT) stays
  attached; auto-reconnect is not triggered (the read is a light operation, as today).
- Cancellation: the operation is light and not cancellable between halt and go (bounded: one halt, ≤ 8 ReadRegs
  calls for ≤ 256 registers, one go). A stop request is honoured after `go`.

### `libs/hw/jlink`

No change. `isHalted`, `halt`, `go`, `readRegisters` exist (`JLinkProgrammer`, plan 0048).

### CLI (`app/cli/src/target_commands.cpp`)

- `regs` gets a `--halt` flag (CLI11 flag on the `regs` subcommand of both `app` and `lte` groups;
  `addSimpleCommand` is replaced for `regs` by a small dedicated registration like other commands with options).
- `runQuiet(..., [&](DeviceSession& s){ return s.readRegisters(targetId, {.haltIfRunning = halt}); })`.
- No logic beyond passing the flag (CLAUDE.md).
- On `target.not_halted` without `--halt`, the CLI replaces the suggestion with a CLI-specific, English-only text
  ("Use `regs --halt` …"; CLI is English only, ADR-0027). If replacing a suggestion has no existing helper, the
  backend suggestion "Halt the core first." stays and the help text of `regs` mentions `--halt` (implementation
  agent checks and picks the variant that needs no new mechanism; if neither fits, stop and ask).

### MCP (`libs/mcp_tools/src/tools/target_tools.cpp`)

- `read_registers` input schema: `deviceTargetSchema()` plus optional boolean `halt` (default false).
- `.start = [id, halt](DeviceSession& s){ return s.readRegisters(id, {.haltIfRunning = halt}); }`.
- Description: "… The CPU must be halted first (halt), else target.not_halted — or pass halt=true to halt it for
  the read and let it run again if it was running. …"
- Annotations stay `readOnly`, no consent (the run state is restored; see open points).
- `prompts.cpp`: where the halt → read_registers → go sequence is described, mention `halt: true`.

### Test doubles

- Fake J-Link library: no change needed (it already runs after every connect; `chip.halted` scenario key exists).
  Check that `JLINKARM_Halt` / `JLINKARM_Go` / `JLINKARM_IsHalted` keep state within one process; if not, add it.
- `scripted_api.h` / `MockJLinkApi`: expectations for halt → readRegs → go ordering.

### Errors

No new `hwio::Error` code and no new translated backend string. Existing: `target.not_halted` (without the
option), `TargetConnectFailed` (halt / read / go vendor errors), `DependencyJLinkFunctionMissing` (`IsHalted`
missing). One new English-only CLI hint (if the variant above is used).

### Bounds / threading

Unchanged from plan 0048: ≤ 256 registers, blocks of 32; all vendor calls on the session worker thread.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| How `regs` works across CLI processes | `regs --halt` option / implicit halt+resume / keep halt across processes | **`regs --halt` option** (2026-11-13) | ADR-0372 |
| State after the read | restore prior state / always resume | **Restore prior state**; resume also after a failed read (2026-11-13) | ADR-0372 |
| Also in MCP | CLI only / CLI and MCP | **CLI and MCP** (`read_registers` optional `halt`) (2026-11-13) | ADR-0372 |
| Hardware diagnosis of Close vs. Connect | phase 0 in this plan / skip | **Phase 0**, only with the user's OK at the time; reset/flash `--halt` findings go to a separate plan (2026-11-13) | ADR-0372 |

Defaults confirmed with the plan approval (2026-11-13): option name `--halt` (vs. `--auto-halt`);
MCP annotation stays `readOnly` with no consent; first-error-wins rule when `go` fails.

## Implementation steps

- [ ] Phase 0: hardware diagnosis (with the user's OK), "Root cause" paragraph added here; new Draft plan if
      `reset --halt` / `flash --halt` are affected
- [x] `ReadRegistersOptions` + `DeviceSession::readRegisters` overload, `OperationCommand` field
- [x] `runReadRegisters` halt / read / go with restore and first-error-wins; halt bookkeeping unchanged
- [x] Unit tests in `libs/device/tests/target_session_test.cpp` (scripted API); the ordering and
      go-failure cases with the GMock programmer in `libs/device/tests/workflow_test.cpp` (the real
      `JLinkProgrammer::go()` cannot fail, so the scripted API cannot reach that branch)
- [x] CLI `regs --halt` for `app` and `lte`, help text, `target.not_halted` hint
- [x] CLI e2e tests (`app/cli/tests/target_e2e_test.cpp`)
- [x] MCP `read_registers` `halt` argument, description, prompts; tests in `libs/mcp_tools/tests`
- [x] `plan/0006-cli.md` command table updated (`regs [--halt]`)
- [x] `update_translations` if any translatable string changed (none changed; `check_translations` up to date)
- [ ] Verification below; status → Done

## Tests and verification

Unit (scripted API, `libs/device/tests`):
- running core + `haltIfRunning`: calls `IsHalted` → `Halt` → `ReadRegs`… → `Go`; result has the registers.
- halted core + `haltIfRunning`: no `Halt`, no `Go`.
- running core, read fails: `Go` still called; result is the read error.
- running core, read ok, `Go` fails: result is the `go` error.
- running core without the option: `target.not_halted`, no `Halt` / `Go` (unchanged).
- `m_halted` / auto-reconnect unchanged after a `haltIfRunning` read (console target).

CLI e2e (fake library): `regs --halt` on a running fake target → exit 0, registers in human and `--json`;
afterwards `status` (same scenario) reports running; `regs` without `--halt` still exit 6 and shows the hint.

MCP tests: `read_registers {"halt": true}` on a running scripted target returns registers; schema rejects a
non-boolean `halt`.

Sanitizers / static: `linux-asan`, `linux-tsan` ctest, `linux-tidy` zero findings, clang-format clean,
`all_qmllint` unaffected. All builds with `nice -n 19 ionice -c 3`.

Manual / HIL (CHESTER-M, J-Link V9.84, only with the user's OK): `hwio chester app regs --halt` → 115 registers,
PC changes between two runs, console of the running firmware continues (RTT output keeps coming in a GUI tab or
a second `console` run afterwards); `regs` without `--halt` → exit 6 with the hint; MCP
`read_registers {"halt": true}` in the GUI → registers, console keeps running.

## Risks and open points

- `--halt` on `reset` / `flash` means "stay halted"; on `regs` it means "halt for the read". The help text makes
  the difference explicit; the user may prefer another name on approval.
- A brief halt can disturb timing-sensitive firmware (radio, watchdog). The halt lasts a few milliseconds; the
  option is explicit, so this is the user's choice.
- If `JLINKARM_Go` fails after a successful read the CPU may stay halted; the error tells the user (existing
  `TargetConnectFailed` texts) and `hwio chester app go` or `reset` fixes it.
- Root cause unknown until phase 0. If `JLINKARM_Close` resumes the core, py-hardwario-compatible
  `reset --halt` / `flash --halt` do not leave the core halted either — separate plan.
- MCP annotation `readOnly` with a brief halt: acceptable because the state is restored; the GUI's AI user modes
  (plan 0045) might classify tools by annotation — check during implementation, stop and ask if it matters.
