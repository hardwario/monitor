#pragma once

// The built-in pages (plan/features/0004-m3-gui-shell.md, B1): the console page of plan 0005
// (registerConsolePage) and the Flash page and the global Settings page of plan 0006
// (registerFlashAndSettingsPages), then MCP in the GUI of plan 0014 (registerMcp, ADR-0243).

namespace hwio::gui {

class AppViewModel;

// Registers the built-in pages. Called from AppOptions::registerExtensions.
void registerBuiltInPages(AppViewModel& app);

} // namespace hwio::gui
