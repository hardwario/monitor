#pragma once

// Test doubles for the MCP device tools (plan/features/0013 A4, C1-C3):
//   FakeConsoleSession - an in-process ConsoleSession driven by the test (lines, prompt,
//                        connection), no threads;
//   MockDeviceProvider - GMock IDeviceProvider;
//   FakeRemoteDevice   - plan 0062: an IRemoteDevice over a FakeConsoleSession (the remote
//                        tab's log): records lines and operation requests, echoes or refuses;
//   FakeProvider       - an IDeviceProvider over FakeConsoleSessions that records markers, busy
//                        state and command hints (and remote tabs "remote:<n>");
//   Harness            - ToolsContext + registerTools + McpServer, calls tools over the modern
//                        dialect (stdio transport) and pumps the event loop.
// Production code never runs nested loops (ADR-0097); tests pump with runUntil().

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/mcp/json_rpc.h"
#include "hwio/mcp/limits.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/server.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/register_tools.h"
#include "hwio/mcp/tools/remote_hidden_view.h"
#include "hwio/mcp/tools/tools_context.h"
#include "mcp_test_support.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPointer>
#include <QString>

#include <gmock/gmock.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::mcp::tools::test {

using hwio::mcp::test::runFor;
using hwio::mcp::test::runUntil;

// A CHESTER copy whose app target has an RTT shell prompt (ADR-0104; CHESTER's is empty).
inline const hwio::device::DeviceProfile& promptProfile() {
    static const std::array<hwio::device::TargetProfile, 2> targets = [] {
        const auto* chester = hwio::device::ProfileRegistry::find("chester");
        std::array<hwio::device::TargetProfile, 2> copy{chester->targets[0], chester->targets[1]};
        copy[0].shellPrompt = "uart:~$ ";
        return copy;
    }();
    static const hwio::device::DeviceProfile profile = [] {
        hwio::device::DeviceProfile copy = *hwio::device::ProfileRegistry::find("chester");
        copy.targets = targets;
        return copy;
    }();
    return profile;
}

inline const hwio::device::DeviceProfile& chesterProfile() {
    return *hwio::device::ProfileRegistry::find("chester");
}

class FakeConsoleSession final : public hwio::device::ConsoleSession {
public:
    explicit FakeConsoleSession(
        const hwio::device::DeviceProfile& profile = chesterProfile(),
        std::size_t logCapacity = hwio::device::ConsoleLog::defaultCapacityPerChannel,
        hwio::device::Transport transport = hwio::device::Transport::JLink)
        : m_profile(&profile), m_log(std::make_shared<hwio::device::ConsoleLog>(logCapacity)),
          m_transport(transport) {
        m_connection.state = hwio::device::ConnectionState::Connected;
        m_connection.transport = transport;
    }

    // The lines sent, in order.
    [[nodiscard]] const std::vector<std::string>& sent() const { return m_sent; }
    [[nodiscard]] int reconnects() const { return m_reconnects; }

    hwio::Status sendLine(std::string line) override {
        if (hwio::Status held; holdLine(line, held)) {
            return held; // plan 0021: a silent shell query runs
        }
        m_sent.push_back(line);
        lineQueued();
        if (m_echo) { // like the real sessions: the line is echoed (dir In) once written
            emitLines({{hwio::device::Channel::Shell, line}}, hwio::device::Direction::In,
                      sendingOrigin());
        }
        return {};
    }
    hwio::Status reconnect() override {
        ++m_reconnects;
        return {};
    }
    [[nodiscard]] const hwio::device::ConsoleLog& log() const override { return *m_log; }
    [[nodiscard]] hwio::device::ConsoleLog& mutableLog() { return *m_log; }
    [[nodiscard]] std::shared_ptr<hwio::device::ConsoleLog> sharedLog() { return m_log; }
    [[nodiscard]] const hwio::device::ConnectionEvent& connection() const override {
        return m_connection;
    }
    [[nodiscard]] hwio::device::SessionIdentity identity() const override {
        return {.transport = m_transport, .id = "1", .displayName = "Fake 1"};
    }
    [[nodiscard]] hwio::device::Capabilities capabilities() const override { return {}; }
    [[nodiscard]] const hwio::device::DeviceProfile& profile() const override { return *m_profile; }
    [[nodiscard]] std::string_view consoleTargetId() const override { return "app"; }
    [[nodiscard]] std::filesystem::path consoleFile() const override { return {}; }
    [[nodiscard]] hwio::Status recordRemoteEvent(std::string /*text*/) override { return {}; }
    [[nodiscard]] hwio::Status
    setFirmwareIdentity(std::optional<hwio::device::FirmwareIdentity> /*identity*/,
                        std::uint64_t /*attach*/) override {
        return {};
    }

    // Appends lines to the log and emits them as one batch (like the real sessions).
    // A silent shell capture (plan 0021) takes its lines first, like in the real sessions.
    void emitLines(const std::vector<std::pair<hwio::device::Channel, std::string>>& lines,
                   hwio::device::Direction dir = hwio::device::Direction::Out,
                   std::uint64_t origin = 0) {
        QList<hwio::device::ConsoleLine> batch;
        for (const auto& [channel, text] : lines) {
            hwio::device::ConsoleLine line;
            line.channel = channel;
            line.dir = dir;
            line.time = std::chrono::system_clock::now();
            line.level = channel == hwio::device::Channel::Log ? hwio::device::LogLevel::Info
                                                               : hwio::device::LogLevel::None;
            line.text = text;
            line.origin = origin;
            if (captureLine(line)) {
                continue;
            }
            line.seq = m_log->append(line);
            batch.push_back(line);
        }
        if (!batch.isEmpty()) {
            emit linesReceived(batch);
        }
    }
    void emitLine(hwio::device::Channel channel, hwio::device::Direction dir,
                  const std::string& text) {
        emitLines({{channel, text}}, dir);
    }
    void shell(const std::string& text) {
        emitLine(hwio::device::Channel::Shell, hwio::device::Direction::Out, text);
    }
    void logLine(const std::string& text) {
        emitLine(hwio::device::Channel::Log, hwio::device::Direction::Out, text);
    }
    void prompt() { emit promptSeen(); }
    void setConnection(hwio::device::ConnectionState state,
                       std::optional<hwio::Error> error = std::nullopt) {
        m_connection.state = state;
        m_connection.error = std::move(error);
        emit connectionChanged(m_connection);
    }

protected:
    void closeSession() override {}

private:
    const hwio::device::DeviceProfile* m_profile;
    std::shared_ptr<hwio::device::ConsoleLog> m_log;
    hwio::device::Transport m_transport;
    hwio::device::ConnectionEvent m_connection;
    std::vector<std::string> m_sent;
    int m_reconnects = 0;
    bool m_echo = true;
};

// Plan 0062: a joined remote tab. sendLine() records the line and, like a Monitor host, echoes it
// on the session (dir In); with `echo` false nothing comes back (a dropped line), with `refuse`
// the host answers line-refused. Operations wait for the test (finishOperation()).
class FakeRemoteDevice final : public IRemoteDevice {
public:
    FakeRemoteDevice(FakeConsoleSession& session, std::string_view code)
        : m_session(&session), m_view(hwio::device::ConsoleRedaction("chester", "app"), code) {
        remoteInfo.host = RemoteHost::Monitor;
        remoteInfo.join = RemoteJoin::Allowed;
        remoteInfo.operations = {
            RemoteTargetOffer{.target = "app",
                              .kinds = {RemoteOperationKind::Flash, RemoteOperationKind::Reset},
                              .catalog = true,
                              .prepared = {RemotePreparedFirmware{
                                  .id = "release", .name = "STICKER", .version = "2.0.0"}}},
            RemoteTargetOffer{.target = "", .kinds = {RemoteOperationKind::LteUpdate}}};
    }

    struct Operation {
        RemoteOperationRequest request;
        std::string client;
        RemoteOperationCallbacks callbacks;
        bool detached = false;
    };

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes): test knobs
    RemoteInfo remoteInfo;
    bool echo = true;
    bool refuse = false;
    std::optional<hwio::Error> sendError;
    std::optional<hwio::Error> operationError;
    std::vector<std::string> lines;
    std::vector<Operation> operations;
    std::set<std::string> allowed;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

    [[nodiscard]] RemoteInfo info() const override { return remoteInfo; }
    hwio::Status sendLine(std::string line, const hwio::mcp::RequestContext& /*who*/) override {
        if (sendError) {
            return std::unexpected(*sendError);
        }
        lines.push_back(line);
        if (refuse) {
            emit lineRefused(QStringLiteral("remote.command_refused"));
        } else if (echo) {
            m_session->emitLines({{hwio::device::Channel::Shell, line}},
                                 hwio::device::Direction::In);
        }
        return {};
    }
    hwio::Result<RemoteOperationId> requestOperation(const RemoteOperationRequest& request,
                                                     const hwio::mcp::RequestContext& who,
                                                     RemoteOperationCallbacks callbacks) override {
        if (operationError) {
            return std::unexpected(*operationError);
        }
        operations.push_back(Operation{.request = request,
                                       .client = who.clientKey,
                                       .callbacks = std::move(callbacks),
                                       .detached = false});
        return operations.size();
    }
    void detachOperation(RemoteOperationId id) override {
        if (id >= 1 && id <= operations.size()) {
            operations.at(id - 1).detached = true;
        }
    }
    [[nodiscard]] hwio::device::ConsoleRedaction redaction() const override {
        return {"chester", "app"};
    }
    [[nodiscard]] std::string hiddenText(const hwio::device::ConsoleLine& line) const override {
        return m_view.text(line);
    }
    [[nodiscard]] bool aiAllowed(std::string_view clientKey) const override {
        return allowed.contains(std::string(clientKey));
    }
    void allowAi(const hwio::mcp::RequestContext& who) override { allowed.insert(who.clientKey); }

    // Stop AI.
    void revoke(const std::string& clientKey) {
        allowed.erase(clientKey);
        emit aiRevoked(QString::fromStdString(clientKey));
    }
    void setJoin(RemoteJoin join) {
        remoteInfo.join = join;
        emit infoChanged();
    }
    // Progress or the end of operation `index` (0-based), unless detached.
    void progressOperation(std::size_t index, const RemoteOperationProgress& progress) {
        Operation& operation = operations.at(index);
        if (!operation.detached && operation.callbacks.progress) {
            operation.callbacks.progress(progress);
        }
    }
    void finishOperation(std::size_t index, hwio::Status status) {
        Operation& operation = operations.at(index);
        if (!operation.detached && operation.callbacks.done) {
            operation.detached = true;
            operation.callbacks.done(std::move(status));
        }
    }

private:
    FakeConsoleSession* m_session; // non-owning, the provider keeps it alive longer
    RemoteHiddenView m_view;
};

class MockDeviceProvider : public IDeviceProvider {
public:
    MOCK_METHOD(std::vector<DeviceEntry>, list, (), (override));
    MOCK_METHOD(void, refresh, (std::function<void(hwio::Status)> done), (override));
    MOCK_METHOD(bool, firstScanPending, (), (const, override));
    MOCK_METHOD(void, open,
                (const OpenRequest& request, std::function<void(hwio::Result<DeviceRef>)> done),
                (override));
    MOCK_METHOD(hwio::Status, close,
                (std::string_view id, const hwio::mcp::RequestContext& context), (override));
    MOCK_METHOD(std::optional<DeviceRef>, find, (std::string_view id), (override));
    MOCK_METHOD(hwio::Status, beginBusy,
                (std::string_view id, QString text, std::function<void()> cancel), (override));
    MOCK_METHOD(void, endBusy, (std::string_view id), (override));
    MOCK_METHOD(void, marker, (std::string_view id, QString text), (override));
    MOCK_METHOD(void, setAiCommandHint, (std::string_view id, bool active), (override));
    DeviceProviderNotifier& notifier() override { return m_notifier; }

private:
    DeviceProviderNotifier m_notifier;
};

// Records what the tools tell a provider (shared by FakeProvider and the session provider).
struct ProviderRecord {
    std::vector<std::pair<std::string, QString>> markers;
    std::vector<std::pair<std::string, QString>> busy; // beginBusy
    int endBusy = 0;
    std::vector<std::pair<std::string, bool>> hints;
    std::function<void()> busyCancel; // the last beginBusy's cancel functor

    [[nodiscard]] std::vector<QString> markerTexts() const {
        std::vector<QString> texts;
        texts.reserve(markers.size());
        for (const auto& [id, text] : markers) {
            texts.push_back(text);
        }
        return texts;
    }
};

// IDeviceProvider over FakeConsoleSessions (no J-Link: DeviceRef::jlink stays null).
class FakeProvider final : public IDeviceProvider {
public:
    [[nodiscard]] ProviderRecord& record() { return m_record; }

    FakeConsoleSession&
    add(const std::string& id, const hwio::device::DeviceProfile& profile = chesterProfile(),
        std::size_t logCapacity = hwio::device::ConsoleLog::defaultCapacityPerChannel) {
        auto session = std::make_unique<FakeConsoleSession>(profile, logCapacity);
        FakeConsoleSession& ref = *session;
        m_sessions[id] = std::move(session);
        emit m_notifier.devicesChanged();
        return ref;
    }
    void remove(const std::string& id) {
        m_remotes.erase(id);
        m_sessions.erase(id);
        emit m_notifier.devicesChanged();
    }
    // Plan 0062: a joined remote tab "remote:<n>" whose session code is `code`.
    FakeRemoteDevice& addRemote(const std::string& id, std::string_view code = "482913",
                                std::string displayName = "CHESTER-M 2159018247 — remote") {
        auto session = std::make_unique<FakeConsoleSession>(
            chesterProfile(), hwio::device::ConsoleLog::defaultCapacityPerChannel,
            hwio::device::Transport::Remote);
        auto remote = std::make_unique<FakeRemoteDevice>(*session, code);
        FakeRemoteDevice& ref = *remote;
        m_sessions[id] = std::move(session);
        m_remotes[id] = RemoteTab{.device = std::move(remote), .name = std::move(displayName)};
        emit m_notifier.devicesChanged();
        return ref;
    }
    // The remote device goes away while its console session stays (the tab left the session).
    void dropRemoteDevice(const std::string& id) { m_remotes.at(id).device.reset(); }
    [[nodiscard]] FakeConsoleSession& session(const std::string& id) { return *m_sessions.at(id); }

    std::vector<DeviceEntry> list() override {
        std::vector<DeviceEntry> entries;
        entries.reserve(m_sessions.size());
        for (const auto& [id, session] : m_sessions) {
            const auto remote = m_remotes.find(id);
            if (remote != m_remotes.end()) {
                entries.push_back(DeviceEntry{.id = id,
                                              .kind = "remote",
                                              .displayName = remote->second.name,
                                              .profileId = "chester",
                                              .state = DeviceEntry::State::Open,
                                              .owner = std::nullopt,
                                              .connection = session->connection(),
                                              .openedByMcp = false});
                continue;
            }
            entries.push_back(DeviceEntry{.id = id,
                                          .kind = "jlink",
                                          .displayName = id,
                                          .profileId = "chester",
                                          .state = DeviceEntry::State::Open,
                                          .owner = std::nullopt,
                                          .connection = session->connection(),
                                          .openedByMcp = true});
        }
        return entries;
    }
    void refresh(std::function<void(hwio::Status)> done) override { done(hwio::Status{}); }
    void open(const OpenRequest& /*request*/,
              std::function<void(hwio::Result<DeviceRef>)> done) override {
        done(std::unexpected(makeError(ErrorCode::UsageInvalidArgument)));
    }
    hwio::Status close(std::string_view id, const hwio::mcp::RequestContext& /*context*/) override {
        m_remotes.erase(std::string(id));
        if (m_sessions.erase(std::string(id)) == 0) {
            return std::unexpected(makeError(ErrorCode::McpDeviceNotOpen, std::string(id)));
        }
        emit m_notifier.devicesChanged();
        return {};
    }
    std::optional<DeviceRef> find(std::string_view id) override {
        const auto it = m_sessions.find(std::string(id));
        if (it == m_sessions.end()) {
            return std::nullopt;
        }
        const auto remote = m_remotes.find(std::string(id));
        if (remote != m_remotes.end()) {
            return DeviceRef{.console = it->second.get(),
                             .jlink = nullptr,
                             .remote = remote->second.device.get(),
                             .isRemote = true};
        }
        return DeviceRef{.console = it->second.get(), .jlink = nullptr};
    }
    hwio::Status beginBusy(std::string_view id, QString text,
                           std::function<void()> cancel) override {
        m_record.busy.emplace_back(id, std::move(text));
        m_record.busyCancel = std::move(cancel);
        return {};
    }
    void endBusy(std::string_view /*id*/) override { ++m_record.endBusy; }
    void marker(std::string_view id, QString text) override {
        if (auto it = m_sessions.find(std::string(id)); it != m_sessions.end()) {
            it->second->mutableLog().appendMarker(hwio::device::Channel::Shell, text.toStdString());
        }
        m_record.markers.emplace_back(id, std::move(text));
    }
    void setAiCommandHint(std::string_view id, bool active) override {
        m_record.hints.emplace_back(id, active);
    }
    DeviceProviderNotifier& notifier() override { return m_notifier; }

private:
    struct RemoteTab {
        std::unique_ptr<FakeRemoteDevice> device;
        std::string name;
    };
    // Declared after the sessions: the remote devices are destroyed first (they point at one).
    std::map<std::string, std::unique_ptr<FakeConsoleSession>> m_sessions;
    std::map<std::string, RemoteTab> m_remotes;
    DeviceProviderNotifier m_notifier;
    ProviderRecord m_record;
};

// ---- server harness -----------------------------------------------------------------------------

inline QJsonObject modernMeta(const QJsonObject& extra = {}) {
    QJsonObject meta{
        {QStringLiteral("io.modelcontextprotocol/protocolVersion"), QStringLiteral("2026-07-28")},
        {QStringLiteral("io.modelcontextprotocol/clientCapabilities"), QJsonObject()},
        {QStringLiteral("io.modelcontextprotocol/clientInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("tester")},
                     {QStringLiteral("version"), QStringLiteral("1")}}}};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        meta.insert(it.key(), it.value());
    }
    return meta;
}

inline QByteArray request(const QJsonValue& id, const QString& method, QJsonObject params = {},
                          const QJsonObject& metaExtra = {}) {
    params.insert(QStringLiteral("_meta"), modernMeta(metaExtra));
    QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("method"), method},
                        {QStringLiteral("params"), params}};
    if (!id.isUndefined()) {
        message.insert(QStringLiteral("id"), id);
    }
    return hwio::mcp::serialize(message);
}

// One tools/call in flight.
struct PendingCall {
    std::shared_ptr<hwio::mcp::test::RecordingReplyChannel> reply;

    [[nodiscard]] bool done() const { return reply->done(); }
    [[nodiscard]] const QJsonObject& response() const { return reply->response; }
    [[nodiscard]] QJsonObject result() const {
        return reply->response.value(QStringLiteral("result")).toObject();
    }
    [[nodiscard]] QJsonObject structured() const {
        return result().value(QStringLiteral("structuredContent")).toObject();
    }
    [[nodiscard]] bool isError() const {
        return result().value(QStringLiteral("isError")).toBool();
    }
    [[nodiscard]] QJsonObject error() const {
        return structured().value(QStringLiteral("error")).toObject();
    }
    [[nodiscard]] std::string errorCode() const {
        return error().value(QStringLiteral("code")).toString().toStdString();
    }
    [[nodiscard]] std::string errorDetail() const {
        return error().value(QStringLiteral("detail")).toString().toStdString();
    }
    [[nodiscard]] std::string errorMessage() const {
        return error().value(QStringLiteral("message")).toString().toStdString();
    }
    // JSON-RPC error number (0 when the response is a result).
    [[nodiscard]] int rpcError() const {
        return reply->response.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("code"))
            .toInt();
    }
    [[nodiscard]] QString rpcPointer() const {
        return reply->response.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("data"))
            .toObject()
            .value(QStringLiteral("pointer"))
            .toString();
    }
    [[nodiscard]] std::size_t progressCount() const { return reply->notifications.size(); }
};

class Harness {
public:
    explicit Harness(IDeviceProvider& provider, const hwio::mcp::Limits& limits = {},
                     hwio::firmware::FirmwareCatalog* catalog = nullptr, ToolsOptions options = {})
        : tools(std::make_unique<ToolsContext>(provider, catalog, std::move(options))) {
        hwio::mcp::Registry registry;
        registered = registerTools(registry, *tools);
        hwio::mcp::ServerOptions serverOptions;
        serverOptions.limits = limits;
        serverOptions.version = QStringLiteral("test");
        server = std::make_unique<hwio::mcp::McpServer>(std::move(registry), consent, activity,
                                                        std::move(serverOptions));
        tools->attachServer(*server);
    }
    ~Harness() {
        server.reset();
        tools.reset();
    }
    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;
    Harness(Harness&&) = delete;
    Harness& operator=(Harness&&) = delete;

    hwio::mcp::test::FakeConsentBroker consent;
    hwio::mcp::test::RecordingActivitySink activity;
    std::unique_ptr<ToolsContext> tools;
    std::unique_ptr<hwio::mcp::McpServer> server;
    hwio::Status registered;

    // Sends tools/call (with a progress token when `progress`) and returns at once.
    PendingCall start(const QString& tool, const QJsonObject& arguments = {},
                      bool progress = false) {
        PendingCall call{std::make_shared<hwio::mcp::test::RecordingReplyChannel>(true, false)};
        QJsonObject meta;
        if (progress) {
            meta.insert(QStringLiteral("progressToken"), nextId);
        }
        server->handle(request(nextId++, QStringLiteral("tools/call"),
                               QJsonObject{{QStringLiteral("name"), tool},
                                           {QStringLiteral("arguments"), arguments}},
                               meta),
                       call.reply, transport());
        return call;
    }

    // Calls a tool and waits for its response.
    PendingCall call(const QString& tool, const QJsonObject& arguments = {},
                     std::chrono::milliseconds timeout = std::chrono::seconds(20),
                     bool progress = false) {
        PendingCall pending = start(tool, arguments, progress);
        (void)runUntil([&pending] { return pending.done(); }, timeout);
        return pending;
    }

    // Any request; the response.
    QJsonObject rpc(const QString& method, const QJsonObject& params = {}) {
        auto reply = std::make_shared<hwio::mcp::test::RecordingReplyChannel>(true, false);
        server->handle(request(nextId++, method, params), reply, transport());
        (void)runUntil([&reply] { return reply->done(); });
        return reply->response;
    }

    // Cancels request `id` (notifications/cancelled).
    void cancel(int id) const {
        auto reply = std::make_shared<hwio::mcp::test::RecordingReplyChannel>(true, false);
        server->handle(request(QJsonValue(QJsonValue::Undefined),
                               QStringLiteral("notifications/cancelled"),
                               QJsonObject{{QStringLiteral("requestId"), id}}),
                       reply, transport());
    }

    [[nodiscard]] int lastId() const { return nextId - 1; }

    int nextId = 1; // the next JSON-RPC request id

private:
    static hwio::mcp::TransportInfo transport() {
        hwio::mcp::TransportInfo info;
        info.kind = hwio::mcp::TransportKind::Stdio;
        info.connectionId = 7;
        return info;
    }
};

inline QJsonObject args(std::initializer_list<std::pair<const char*, QJsonValue>> values) {
    QJsonObject json;
    for (const auto& [name, value] : values) {
        json.insert(QLatin1StringView(name), value);
    }
    return json;
}

} // namespace hwio::mcp::tools::test
