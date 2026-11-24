# ADR-0422 — The CLI and the stdio MCP server run the J-Link library without a display (Linux)

- **Status:** Rejected
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24: "do not do this. Ignore this comment. We will not use EDU in production" (plan [0060](../features/0060-test-feedback-2026-11.md), D1)

## Context

With a J-Link EDU / EDU Mini, the J-Link library (9.82 seen) opens SEGGER's "Terms of use" window at every
`JLINKARM_Open` when a display is available. The command strings Monitor sends before and after Open
(`SetBatchMode = 1`, `SuppressControlPanel`, `HideDeviceSelection = 1`, `DisableInfoWinFlashDL`,
`DisableAutoUpdateFW`; `jlink_probe.cpp`) do not suppress it, and the library's declarations Monitor uses offer
no hook for it (only `JLINK_SetHookUnsecureDialog`). `hwio` and `hwio mcp` then block with no time limit until
someone clicks. Without `DISPLAY` / `WAYLAND_DISPLAY`, the same probe opens in 0.3 s (tester, 2026-11-20/23).

## Options considered

1. **CLI (`hwio`, including `hwio mcp`) unsets `DISPLAY` and `WAYLAND_DISPLAY` on Linux as the first statement of
   `main()` (before any thread); `HWIO_JLINK_DIALOGS=1` keeps them. Later, all OSes: a one-time stderr notice when a
   probe open takes longer than 5 s ("a SEGGER window may be waiting"; needs a watchdog thread, because some
   commands open the probe on the main thread).**
2. Only the notice.
3. Only documentation.

## Decision

Proposed: option 1. The GUI and the GUI-hosted MCP server are unchanged (the window is visible to the user there).
macOS and Windows have no such switch; only the notice applies.

## Consequences

- Scripts, CI and AI clients on Linux no longer hang on the EDU notice. The CLI does not show SEGGER's licence
  notice for EDU probes; whether that is acceptable is a HARDWARIO decision (recorded here once decided).
- `unsetenv` is not thread-safe: it must stay before `QCoreApplication` and any thread; the CLI never uses QtGui
  (ADR-0004); its only child process is nRF Util, a console tool, so nothing else needs a display.
- The fake J-Link library records the environment at Open for tests.
