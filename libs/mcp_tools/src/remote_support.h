#pragma once

// The MCP tools on a joined remote support tab (plan/features/0062, ADR-0433..0438): the tool
// matrix (ADR-0434), support's session permission (ADR-0436), the hidden view of every read path
// (ADR-0437), send_command over the remote link and its limits (ADR-0438) and the remote
// operations flash / flash_lte / reset (D8, D12, D13).
//
// registerTools() wraps every tool's ToolSpec::start with remoteGate(): a call that resolves to a
// remote device is answered by the matrix (mcp.tool_not_allowed / mcp.capability_missing), or
// asks support's permission (ConsentCategory::RemoteDevice, once per client and remote device;
// IRemoteDevice keeps the grant) and then runs the tool's remote start (send_command, flash,
// flash_lte, reset) or its normal start (reads, waits, status), which apply the hidden view
// themselves (tool_support.h visibleLine()). The gate records the redacted arguments for the
// activity entry, adds `remote: true` to a successful result and cancels the call when support
// stops the AI client (IRemoteDevice::aiRevoked).
//
// Thread safety: owner thread only.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/mcp/limits.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/tools_context.h"

#include <QJsonObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace hwio::mcp::tools::detail {

// The text list_devices and status carry for a remote device (plan 0062 § Device entry).
inline constexpr std::string_view remoteNote =
    "This is a customer's device in a support session. Commands and operations run on the "
    "customer's device.";
// The name of a remote device before support allowed the client (ADR-0436).
inline constexpr std::string_view remoteNamelessName = "Remote support session";

// How a tool answers on a remote device (ADR-0434).
enum class RemotePolicy : std::uint8_t {
    Allowed,           // after support's permission
    NotAllowed,        // mcp.tool_not_allowed: join and leave stay human
    CapabilityMissing, // mcp.capability_missing: probe, memory, register, PIB / UICR, erase, ...
};
[[nodiscard]] RemotePolicy remotePolicy(std::string_view tool) noexcept;

// mcp.consent_denied for a remote device the client has no grant for (resources, without a
// prompt).
[[nodiscard]] hwio::Error remoteNotAllowedYet(const std::string& id);

// One AI command at a time and at most ToolLimits::remoteCommandsPerWindow per window and remote
// device (ADR-0438). Bounded: one entry per listed remote device (forget() on device changes),
// at most remoteCommandsPerWindow timestamps per entry.
class RemoteCommands {
public:
    // A send_command: internal.busy while another one runs on `id`, mcp.too_many_requests above
    // the rate; marks `id` busy until end().
    [[nodiscard]] hwio::Status begin(const std::string& id, const hwio::mcp::ToolLimits& limits);
    void end(const std::string& id);
    // A polling command of wait_for_state: the rate only.
    [[nodiscard]] hwio::Status take(const std::string& id, const hwio::mcp::ToolLimits& limits);
    // Drops the entries of devices not in `alive`.
    void keepOnly(const std::set<std::string>& alive);

private:
    struct State {
        bool busy = false;
        std::deque<std::chrono::steady_clock::time_point> sent; // <= remoteCommandsPerWindow
    };
    std::map<std::string, State> m_states;
};

// The pre-checks of an AI command line on a remote device (ADR-0437, ADR-0438): longer than
// remoteLineMaxBytes or a control character (usage.invalid_argument), a secret-setting command
// (remote.command_refused, never sent), the session ended (remote.host_ended), not allowed yet
// (remote.waiting_for_customer), an operations-only session (mcp.capability_missing).
[[nodiscard]] hwio::Status checkRemoteCommand(const std::string& id, IRemoteDevice& remote,
                                              std::string_view line,
                                              const hwio::mcp::ToolLimits& limits);

// `arguments` with every string as the hidden view shows it (`command` as a sent line) - for the
// activity entry (ADR-0437).
[[nodiscard]] QJsonObject visibleArguments(const DeviceRef& ref, const QJsonObject& arguments);

// Wraps `spec.start` (registerTools()).
[[nodiscard]] hwio::mcp::ToolStart remoteGate(ToolsContext& context, std::string_view tool,
                                              hwio::mcp::ToolStart start);

// The remote starts (called by the gate after the permission).
[[nodiscard]] hwio::mcp::ToolStart remoteSendCommandStart(ToolsContext& context);
[[nodiscard]] hwio::mcp::ToolStart remoteOperationStart(ToolsContext& context,
                                                        std::string_view tool);

} // namespace hwio::mcp::tools::detail
