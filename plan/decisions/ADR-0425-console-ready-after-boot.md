# ADR-0425 — The console is Connected only once RTT output has gone quiet after an attach

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, as recommended) (plan [0060](../features/0060-test-feedback-2026-11.md), D5)

## Context

After `reset` (GUI, CLI, MCP) the session worker re-attaches RTT inside the operation and reports Connected before
the operation finishes. The attach probably succeeds against a control block that is valid in RAM while
the bootloader and the application are still starting. MCP `wait_for_connection` then returns at once, and the
first `send_command`, `chester app command` or the GUI's silent `info show` gets no answer, with no error.
CHESTER's shell prompt is not known (open question 25), so "no answer" and "no output" look the same. The
same happens to a new process that attaches while the device reboots (`chester app command`, a HIL test after a
rebooting test). The exact cause is not proven on hardware (plan 0060, B0).

## Options considered

1. **Every RTT attach of the console (at open, after a reset / flash / recover with the CPU running, after an
   automatic reconnect) reports Connected once no line has arrived for 300 ms, or after 3 s at most while lines keep
   coming; lines sent meanwhile are queued. A command that got no output at all is marked (`no_output` in MCP,
   `noOutput` and a stderr warning in the CLI); exit codes unchanged.**
1a. The same settle only after Monitor's own reset / flash; the attach at open is unchanged.
2. Option 1, and `chester app command` without output exits 9 (timeout).
3. Only the marking.

## Decision

Option 1 (accepted 2026-11-24); the two numbers are confirmed on hardware first.

## Consequences

- Every attach is Connected 300 ms later (an idle device), a booting device up to 3 s later; tests that wait for
  Connected may need longer timeouts.
- With option 1a, a device rebooted by something else can still give one empty answer; the marking makes it
  visible.
- New optional result fields in MCP and CLI `--json` (additive).
