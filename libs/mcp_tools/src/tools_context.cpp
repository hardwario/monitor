#include "hwio/mcp/tools/tools_context.h"

#include "hwio/device/console_line.h"
#include "hwio/device/console_session.h"
#include "hwio/device/session_events.h"
#include "hwio/mcp/server.h"
#include "hwio/mcp/tools/device_provider.h"
#include "remote_support.h"
#include "resources.h"
#include "scan_scheduler.h"
#include "tool_support.h"

#include <QList>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QStringLiteral>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::mcp::tools {

namespace detail {

// The session connections for resources/updated; destroying the context object disconnects them.
struct ResourceWatch {
    std::unique_ptr<QObject> connections = std::make_unique<QObject>();
};

} // namespace detail

ToolsContext::ToolsContext(IDeviceProvider& provider, hwio::firmware::FirmwareCatalog* catalog,
                           ToolsOptions options, QObject* parent)
    : QObject(parent), m_provider(&provider), m_catalog(catalog), m_options(std::move(options)),
      m_scheduler(std::make_unique<detail::ScanScheduler>()),
      m_remoteCommands(std::make_unique<detail::RemoteCommands>()) {
    connect(&provider.notifier(), &DeviceProviderNotifier::devicesChanged, this,
            &ToolsContext::devicesChanged);
}

ToolsContext::~ToolsContext() = default;

std::filesystem::path ToolsContext::memoryDumpDirectory() const {
    if (!m_options.memoryDumpDirectory.empty()) {
        return m_options.memoryDumpDirectory;
    }
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return std::filesystem::path(cache.toStdU16String()) / "mcp";
}

void ToolsContext::attachServer(hwio::mcp::McpServer& server) {
    m_server = &server;
    rewatch();
}

void ToolsContext::detachServer() {
    m_server.clear();
    m_watch.reset();
}

void ToolsContext::devicesChanged() {
    std::set<std::string> remote;
    for (const DeviceEntry& entry : m_provider->list()) {
        if (entry.kind == "remote") {
            remote.insert(entry.id);
        }
    }
    m_remoteCommands->keepOnly(remote);
    if (m_server.isNull()) {
        return;
    }
    m_server->notifyResourceListChanged();
    m_server->notifyResourceUpdated(detail::devicesUri());
    rewatch();
}

void ToolsContext::rewatch() {
    m_watch = std::make_unique<detail::ResourceWatch>();
    if (m_server.isNull()) {
        return;
    }
    for (const DeviceEntry& entry : m_provider->list()) {
        if (entry.state != DeviceEntry::State::Open) {
            continue;
        }
        if (const auto ref = m_provider->find(entry.id); ref && ref->alive()) {
            watch(entry.id, *ref);
        }
    }
}

void ToolsContext::watch(const std::string& id, const DeviceRef& ref) {
    QObject* context = m_watch->connections.get();
    connect(ref.console.data(), &hwio::device::ConsoleSession::linesReceived, context,
            [this, id, ref](const QList<hwio::device::ConsoleLine>& lines) {
                linesArrived(id, ref, lines);
            });
    connect(ref.console.data(), &hwio::device::ConsoleSession::connectionChanged, context,
            [this, id, ref](const hwio::device::ConnectionEvent&) {
                if (m_server.isNull()) {
                    return;
                }
                notifyDevice(ref, detail::deviceUri(id, QStringLiteral("status")));
                m_server->notifyResourceUpdated(detail::devicesUri());
            });
    if (ref.isRemote && !ref.remote.isNull()) {
        // The join state or the host's hello changed the devices entry.
        connect(ref.remote.data(), &IRemoteDevice::infoChanged, context, [this] {
            if (!m_server.isNull()) {
                m_server->notifyResourceUpdated(detail::devicesUri());
            }
        });
    }
}

void ToolsContext::notifyDevice(const DeviceRef& ref, const QString& uri) {
    if (m_server.isNull()) {
        return;
    }
    if (!ref.isRemote) {
        m_server->notifyResourceUpdated(uri);
        return;
    }
    // ADR-0436: nothing of a customer's device reaches a client before support's Allow.
    m_server->notifyResourceUpdated(
        uri, [ref](std::string_view clientKey) { return detail::visibleTo(ref, clientKey); });
}

void ToolsContext::linesArrived(const std::string& id, const DeviceRef& ref,
                                const QList<hwio::device::ConsoleLine>& lines) {
    if (m_server.isNull()) {
        return;
    }
    const bool shell = std::ranges::any_of(lines, [](const hwio::device::ConsoleLine& line) {
        return line.channel == hwio::device::Channel::Shell;
    });
    const bool log = std::ranges::any_of(lines, [](const hwio::device::ConsoleLine& line) {
        return line.channel == hwio::device::Channel::Log;
    });
    if (shell) {
        notifyDevice(ref, detail::deviceUri(id, QStringLiteral("terminal")));
    }
    if (log) {
        notifyDevice(ref, detail::deviceUri(id, QStringLiteral("log")));
    }
}

} // namespace hwio::mcp::tools
