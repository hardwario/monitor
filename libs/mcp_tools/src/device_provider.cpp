#include "hwio/mcp/tools/device_provider.h"

#include <string_view>

namespace hwio::mcp::tools {

std::string_view remoteJoinId(RemoteJoin join) noexcept {
    switch (join) {
    case RemoteJoin::Connecting:
        return "connecting";
    case RemoteJoin::Waiting:
        return "waiting";
    case RemoteJoin::Allowed:
        return "allowed";
    case RemoteJoin::Ended:
        return "ended";
    }
    return "ended";
}

std::string_view remoteHostId(RemoteHost host) noexcept {
    switch (host) {
    case RemoteHost::Monitor:
        return "monitor";
    case RemoteHost::Other:
        return "other";
    }
    return "other";
}

std::string_view remoteOperationKindId(RemoteOperationKind kind) noexcept {
    switch (kind) {
    case RemoteOperationKind::Flash:
        return "flash";
    case RemoteOperationKind::Reset:
        return "reset";
    case RemoteOperationKind::LteUpdate:
        return "lteUpdate";
    }
    return "reset";
}

} // namespace hwio::mcp::tools
