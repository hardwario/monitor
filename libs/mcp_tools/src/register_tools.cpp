#include "hwio/mcp/tools/register_tools.h"

#include "hwio/core/result.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/tools/tools_context.h"
#include "remote_support.h"
#include "resources.h"
#include "tools/tools.h"

#include <utility>

namespace hwio::mcp::tools {

hwio::Status registerTools(hwio::mcp::Registry& registry, ToolsContext& context) {
    // Plan 0062: every tool's start passes the remote gate (detail::remoteGate()); the tools are
    // built in a scratch registry first and added with the wrapped start.
    hwio::mcp::Registry tools;
    for (const auto& add : {&detail::registerDiscoveryTools, &detail::registerConsoleTools,
                            &detail::registerSessionTools, &detail::registerTargetTools,
                            &detail::registerFirmwareTools, &detail::registerStm32Tools}) {
        if (auto added = (*add)(tools, context); !added) {
            return added;
        }
    }
    for (hwio::mcp::ToolSpec spec : tools.tools()) {
        spec.start = detail::remoteGate(context, spec.name, std::move(spec.start));
        if (auto added = registry.addTool(std::move(spec)); !added) {
            return added;
        }
    }
    if (auto added = detail::registerResources(registry, context); !added) {
        return added;
    }
    return detail::registerPrompts(registry);
}

} // namespace hwio::mcp::tools
