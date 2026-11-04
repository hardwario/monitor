# ADR-0243 — MCP in the GUI as a registered extension; J-Link tabs first, BLE later

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0014 revision session)

## Context

Plan 0014 (draft of 2026-10-16) let `AppViewModel` own an `McpController` and gain an `openTab` overload for BLE
and J-Link. Plan 0012 (Bluetooth in the GUI) is implemented at the same time on another branch and changes
`AppViewModel`, `DeviceTabViewModel`, `ConnectionViewModel`, `ConsoleViewModel`, `DeviceBar.qml`, `NavRail.qml`
and `DeviceTabBar.qml` heavily. Both branches are merged into `main` one after the other.

## Options considered

1. **A registered extension (like the Console and Flash pages) with small additive changes to the shell; J-Link
   tabs now, Bluetooth tabs as a later step after 0012 is merged (chosen).**
2. `AppViewModel` owns the controller (draft design); more changes in the files 0012 changes.
3. Wait for 0012 and do J-Link and Bluetooth together.

## Decision

Option 1.
- `registerMcp(AppViewModel&)` runs from `registerBuiltInPages` (the `registerExtensions` hook). It creates the
  `McpController` (Qt child of `AppViewModel`, QML singleton `McpController`), registers the settings keys and the
  Settings section "AI assistants (MCP)". The controller stops the server on `QCoreApplication::aboutToQuit` and in
  its destructor.
- The shell gets only small additive changes: `DeviceTabViewModel::openedBy` and `aiCommandActive` properties,
  `AppViewModel::appendTab()` (adds a tab without switching to it), `tabsJson` skips AI-opened tabs, and three QML
  additions (rail indicator in `NavRail.qml`, "AI" badge in `DeviceTabBar.qml`, Cancel in `DeviceBar.qml`).
- `GuiDeviceProvider` lists and opens J-Link tabs (`jlink:<SN>`). Bluetooth tabs (`ble:<id>`) are step D1 of plan
  0014, done after plan 0012 is in `main`: the provider then uses `consoleSession()` and the BLE scan cache.

## Consequences

- Merging the second branch needs a small manual merge in the listed files only.
- Until D1, `ble:` ids from the AI give `mcp.device_not_open` in the GUI and Bluetooth tabs are not listed.
