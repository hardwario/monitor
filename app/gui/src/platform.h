#pragma once

// Per-OS application identity of the GUI (ADR-0018, plan/features/0015 A3).

namespace hwio::gui {

// Windows: SetCurrentProcessExplicitAppUserModelID(L"com.hardwario.monitor") (taskbar grouping,
// the Start-menu shortcut has the same id). Call before the first window. No-op elsewhere.
void applyPlatformIdentity();

// Plan 0060 A3: before `--version` / `--help` print on stdout. Windows (a GUI-subsystem program
// has no console): when stdout is not redirected, attach to the console of the parent process
// (the terminal hwio-monitor was started from) and send stdout there; without a parent console
// nothing is shown. No-op elsewhere.
void attachParentConsole();

} // namespace hwio::gui
