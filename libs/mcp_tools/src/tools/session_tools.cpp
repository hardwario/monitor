// Session control tools (plan/features/0013 C1, ADR-0099; rttt reconnect / stop / start /
// jlink_close / jlink_open): reconnect, stop, start, release_probe, reacquire_probe. They queue a
// command for the session and return at once (reacquire_probe waits for the console, ADR-0375);
// wait_for_connection waits for the outcome.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_session.h"
#include "hwio/device/session_events.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/schema.h"
#include "hwio/mcp/tool_call.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/tools_context.h"
#include "tool_support.h"
#include "tools/tools.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace hwio::mcp::tools::detail {

namespace {

using hwio::mcp::Schema;

constexpr const char* reconnectDescription =
    "Restart the console connection without resetting the device: re-attach RTT (J-Link) or "
    "reconnect Bluetooth. Use it when no output arrives, commands fail, or the device was reset "
    "or reflashed outside this server. rtt_address (J-Link only) sets the RTT control block "
    "address, e.g. \"0x20004000\" (the _SEGGER_RTT symbol from the firmware ELF: "
    "`nm zephyr.elf | grep _SEGGER_RTT`), when the automatic search attaches to a stale block; "
    "it is kept for later connections, \"auto\" returns to the search. Returns at once; call "
    "wait_for_connection to wait for the result.";

constexpr const char* stopDescription =
    "Stop reading the device's RTT console; the probe stays held, so memory, registers and "
    "halt/go keep working, and the firmware keeps running. No output arrives while stopped. "
    "Call start to read again, or release_probe to also give the probe up. J-Link only.";

constexpr const char* startDescription =
    "Start reading the RTT console again after stop. Use reconnect instead when the session is "
    "running but stuck. J-Link only.";

constexpr const char* releaseProbeDescription =
    "Release the J-Link probe so that another program (J-Link Commander, nrfjprog, west flash) "
    "can use it (rttt: jlink_close). Stops RTT; the firmware keeps running. HARDWARIO programs "
    "still see the probe as used by this server. Call reacquire_probe to take it back. J-Link "
    "only.";

constexpr const char* reacquireProbeDescription =
    "Take the J-Link probe back after release_probe and start reading RTT again (rttt: "
    "jlink_open). Waits up to 10 s until the console is connected or the attempt stopped, and "
    "returns `connection` and `waited` (seconds). connection.state stopped with "
    "probe.in_use_by_other_process: another program still holds the probe. A state other than "
    "connected or stopped after the wait is not an error: call wait_for_connection to wait "
    "longer. J-Link only.";

QJsonObject connectionResult(const DeviceRef& ref) {
    QJsonObject result;
    result.insert(QStringLiteral("connection"), connectionJson(ref.console->connection()));
    return result;
}

using SessionAction =
    std::function<hwio::Result<QJsonObject>(const ResolvedDevice& device, const QJsonObject& args)>;

// A synchronous control tool: device resolution (J-Link only when `jlinkOnly`), the start marker
// (ADR-0105), then `action`.
hwio::mcp::ToolStart controlStart(ToolsContext& context, const char* tool, bool jlinkOnly,
                                  SessionAction action) {
    return [&context, tool, jlinkOnly, action = std::move(action)](
               const hwio::mcp::RequestContext& rc, const QJsonObject& args,
               hwio::mcp::ToolEnv&) -> std::unique_ptr<hwio::mcp::ToolCall> {
        auto device = jlinkOnly ? resolveJLinkDevice(context.provider(), args)
                                : resolveDevice(context.provider(), args);
        if (!device) {
            return failed(std::move(device).error());
        }
        context.provider().marker(device->id, markerStart(rc, QString::fromLatin1(tool)));
        auto result = action(*device, args);
        auto call = done(std::move(result));
        return call;
    };
}

// reacquire_probe (ADR-0375): queues the reacquire, then waits (owner thread not blocked) until
// the console is connected or the attempt stopped, at most ToolLimits::reacquireWait. A timeout is
// not an error (the current state is returned). Cancelling stops the wait; the reacquire stays.
class ReacquireProbeCall final : public DeviceCall {
public:
    ReacquireProbeCall(ToolsContext& tools, const hwio::mcp::RequestContext& context,
                       hwio::mcp::ToolEnv& env, ResolvedDevice device)
        : DeviceCall(tools, context, env, std::move(device)) {
        m_elapsed.start();
        connect(ref().console.data(), &hwio::device::ConsoleSession::connectionChanged, this,
                [this](const hwio::device::ConnectionEvent& event) {
                    switch (event.state) {
                    case hwio::device::ConnectionState::Connecting:
                        m_attempting = true;
                        break;
                    case hwio::device::ConnectionState::Connected:
                        finishNow();
                        break;
                    case hwio::device::ConnectionState::Stopped:
                        // Only the Stopped of this attempt (it starts with Connecting), not a
                        // late one of the release before it.
                        if (m_attempting) {
                            finishNow();
                        }
                        break;
                    case hwio::device::ConnectionState::Disconnected:
                    case hwio::device::ConnectionState::ProbeOpen:
                        break;
                    }
                });
        if (auto reacquired = ref().jlink->reacquireProbe(); !reacquired) {
            completeError(std::move(reacquired).error());
            return;
        }
        if (ref().console->connection().state == hwio::device::ConnectionState::Connected) {
            finishNow(); // not released: nothing to wait for
            return;
        }
        const std::chrono::milliseconds wait = limits().reacquireWait;
        m_deadline.setSingleShot(true);
        m_deadline.setInterval(wait);
        connect(&m_deadline, &QTimer::timeout, this, [this] { finishNow(); });
        m_progress.setInterval(std::chrono::seconds(1));
        connect(&m_progress, &QTimer::timeout, this, [this, wait] {
            reportProgress(seconds(std::chrono::milliseconds(m_elapsed.elapsed())), seconds(wait),
                           QStringLiteral("waiting"));
        });
        m_deadline.start();
        m_progress.start();
    }

protected:
    void onCancel() override {
        m_deadline.stop();
        m_progress.stop();
        ToolCall::onCancel();
    }
    void deviceGone() override {
        m_deadline.stop();
        m_progress.stop();
    }

private:
    void finishNow() {
        if (isFinished()) {
            return;
        }
        m_deadline.stop();
        m_progress.stop();
        if (!alive()) {
            completeError(deviceNotOpen(id()));
            return;
        }
        QJsonObject result = connectionResult(ref());
        // Seconds with 0.1 s resolution.
        const auto tenths = (m_elapsed.elapsed() + 50) / 100;
        result.insert(QStringLiteral("waited"), static_cast<double>(tenths) / 10.0);
        complete(result);
    }

    QElapsedTimer m_elapsed;
    QTimer m_deadline;
    QTimer m_progress;
    bool m_attempting = false;
};

QJsonObject deviceOnly() {
    return Schema::object().property(QStringLiteral("device"), deviceArgument()).json();
}

hwio::Status addReconnect(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "reconnect",
            .title = "Reconnect the console",
            .description = reconnectDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(
                        QStringLiteral("rtt_address"),
                        Schema::string(QStringLiteral("\"auto\" or the RTT control block address "
                                                      "(J-Link only; sticky)."))
                            .pattern(QStringLiteral("^(auto|0x[0-9a-fA-F]{1,8}|[0-9]{1,10})$")))
                    .json(),
            .outputSchema =
                objectOf({{"connection", connectionSchema()}}, {QStringLiteral("connection")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = [&context](const hwio::mcp::RequestContext& rc, const QJsonObject& args,
                                hwio::mcp::ToolEnv&) -> std::unique_ptr<hwio::mcp::ToolCall> {
                auto device = resolveDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                const QString address = args.value(QStringLiteral("rtt_address")).toString();
                if (!address.isEmpty() && device->ref.jlink.isNull()) {
                    return failed(capabilityMissing(device->id));
                }
                context.provider().marker(device->id, markerStart(rc, QStringLiteral("reconnect")));
                if (!address.isEmpty()) {
                    std::optional<std::uint32_t> value;
                    if (address != QStringLiteral("auto")) {
                        value = parseAddress(address);
                        if (!value) {
                            return failed(invalidArgument(
                                QStringLiteral("The address %1 is not valid.").arg(address),
                                address.toStdString()));
                        }
                    }
                    if (auto set = device->ref.jlink->setRttAddress(value); !set) {
                        return failed(std::move(set).error());
                    }
                }
                if (auto reconnected = device->ref.console->reconnect(); !reconnected) {
                    return failed(std::move(reconnected).error());
                }
                return done(connectionResult(device->ref));
            },
            .validate = {}});
}

hwio::Status addStop(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "stop",
            .title = "Stop reading RTT",
            .description = stopDescription,
            .inputSchema = deviceOnly(),
            .outputSchema =
                objectOf({{"was_reading", typed("boolean")}}, {QStringLiteral("was_reading")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = controlStart(
                context, "stop", true,
                [](const ResolvedDevice& device, const QJsonObject&) -> hwio::Result<QJsonObject> {
                    const auto state = device.ref.console->connection().state;
                    const bool wasReading = state == hwio::device::ConnectionState::Connected ||
                                            state == hwio::device::ConnectionState::Connecting;
                    if (auto suspended = device.ref.jlink->suspend(); !suspended) {
                        return std::unexpected(std::move(suspended).error());
                    }
                    QJsonObject result;
                    result.insert(QStringLiteral("was_reading"), wasReading);
                    return result;
                }),
            .validate = {}});
}

hwio::Status addStart(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "start",
            .title = "Start reading RTT",
            .description = startDescription,
            .inputSchema = deviceOnly(),
            .outputSchema = objectOf({{"already_reading", typed("boolean")}},
                                     {QStringLiteral("already_reading")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = controlStart(
                context, "start", true,
                [](const ResolvedDevice& device, const QJsonObject&) -> hwio::Result<QJsonObject> {
                    const bool reading = device.ref.console->connection().state ==
                                         hwio::device::ConnectionState::Connected;
                    if (!reading) {
                        if (auto resumed = device.ref.jlink->resume(); !resumed) {
                            return std::unexpected(std::move(resumed).error());
                        }
                    }
                    QJsonObject result;
                    result.insert(QStringLiteral("already_reading"), reading);
                    return result;
                }),
            .validate = {}});
}

hwio::Status addReleaseProbe(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "release_probe",
            .title = "Release the probe",
            .description = releaseProbeDescription,
            .inputSchema = deviceOnly(),
            .outputSchema =
                objectOf({{"released", typed("boolean")}}, {QStringLiteral("released")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = controlStart(
                context, "release_probe", true,
                [](const ResolvedDevice& device, const QJsonObject&) -> hwio::Result<QJsonObject> {
                    if (auto released = device.ref.jlink->releaseProbe(); !released) {
                        return std::unexpected(std::move(released).error());
                    }
                    QJsonObject result;
                    result.insert(QStringLiteral("released"), true);
                    return result;
                }),
            .validate = {}});
}

hwio::Status addReacquireProbe(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "reacquire_probe",
            .title = "Take the probe back",
            .description = reacquireProbeDescription,
            .inputSchema = deviceOnly(),
            .outputSchema =
                objectOf({{"connection", connectionSchema()},
                          {"waited", typed("number", "seconds waited for the console")}},
                         {QStringLiteral("connection"), QStringLiteral("waited")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = [&context](const hwio::mcp::RequestContext& rc, const QJsonObject& args,
                                hwio::mcp::ToolEnv& env) -> std::unique_ptr<hwio::mcp::ToolCall> {
                auto device = resolveJLinkDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                context.provider().marker(device->id,
                                          markerStart(rc, QStringLiteral("reacquire_probe")));
                return std::make_unique<ReacquireProbeCall>(context, rc, env, std::move(*device));
            },
            .validate = {}});
}

} // namespace

hwio::Status registerSessionTools(hwio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add :
         {&addReconnect, &addStop, &addStart, &addReleaseProbe, &addReacquireProbe}) {
        if (auto added = add(registry, context); !added) {
            return added;
        }
    }
    return {};
}

} // namespace hwio::mcp::tools::detail
