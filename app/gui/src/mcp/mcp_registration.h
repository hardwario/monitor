#pragma once

// MCP in the GUI as a registered extension (plan/features/0014, ADR-0243): registerMcp() runs from
// registerBuiltInPages() after the Flash/Settings registration. It registers the setting keys
// (mcp/enabled, mcp/port, mcp/consent/<category>), creates the McpController (a Qt child of the
// AppViewModel, the QML singleton McpController) and adds the Settings section "AI assistants
// (MCP)" (id "mcp", order 50: after nRF Util, before Advanced and About).

#include "mcp/mcp_controller.h"

namespace hwio::gui {

class AppViewModel;

// Returns the controller (owned by `app`). `options`: tests (their own token file, a short consent
// timeout).
McpController& registerMcp(AppViewModel& app, McpController::Options options = {});

} // namespace hwio::gui
