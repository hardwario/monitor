#pragma once

// Test doubles of the host side (libs/remote tests): FakeConsole (a device::ConsoleSession that
// records the lines sent to it and echoes them, records the remote audit events, and delivers
// device lines on demand), ScriptedConsent (answers or keeps questions pending), FakeOperations (a
// HostOperations whose lookups, progress and results the test drives).

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/remote/consent.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/tunnel.h"

#include <QList>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::remote::testing {

class FakeConsole final : public device::ConsoleSession {
public:
    explicit FakeConsole(device::Transport transport = device::Transport::JLink)
        : m_log(std::make_shared<device::ConsoleLog>()), m_transport(transport) {
        m_connection.state = device::ConnectionState::Connected;
        m_connection.transport = transport;
    }

    struct Sent {
        std::string line;
        device::LineSource source;
    };
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::vector<Sent> sent;
    std::vector<std::string> events; // recordRemoteEvent()
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

    Status sendLine(std::string line) override {
        sent.push_back(Sent{.line = line, .source = sendingSource()});
        lineQueued();
        device::ConsoleLine echo = make(device::Channel::Shell, device::Direction::In, line);
        echo.source = sendingSource();
        deliver(std::move(echo));
        return {};
    }
    Status reconnect() override { return {}; }
    Status recordRemoteEvent(std::string text) override {
        events.push_back(text);
        device::ConsoleLine marker = make(device::Channel::Shell, device::Direction::Marker, text);
        marker.source = device::LineSource::Remote;
        deliver(std::move(marker));
        return {};
    }
    [[nodiscard]] const device::ConsoleLog& log() const override { return *m_log; }
    [[nodiscard]] const device::ConnectionEvent& connection() const override {
        return m_connection;
    }
    [[nodiscard]] device::SessionIdentity identity() const override {
        return {.transport = m_transport, .id = "801000001", .displayName = "J-Link 801000001"};
    }
    [[nodiscard]] device::Capabilities capabilities() const override { return {}; }
    [[nodiscard]] const device::DeviceProfile& profile() const override {
        return *device::ProfileRegistry::find("chester");
    }
    [[nodiscard]] std::string_view consoleTargetId() const override { return "app"; }
    [[nodiscard]] std::filesystem::path consoleFile() const override { return {}; }
    Status setFirmwareIdentity(std::optional<FirmwareIdentity> /*identity*/,
                               std::uint64_t /*attach*/) override {
        return {};
    }

    void shell(const std::string& text) {
        deliver(make(device::Channel::Shell, device::Direction::Out, text));
    }
    void logLine(const std::string& text) {
        device::ConsoleLine line = make(device::Channel::Log, device::Direction::Out, text);
        line.level = device::LogLevel::Info;
        deliver(std::move(line));
    }
    void setState(device::ConnectionState state) {
        m_connection.state = state;
        emit connectionChanged(m_connection);
    }
    void adopt(FirmwareIdentity identity) { setAdoptedIdentity(std::move(identity)); }

    [[nodiscard]] std::vector<std::string> sentLines() const {
        std::vector<std::string> out;
        out.reserve(sent.size());
        for (const Sent& s : sent) {
            out.push_back(s.line);
        }
        return out;
    }
    [[nodiscard]] bool hasEvent(std::string_view part) const {
        return std::ranges::any_of(
            events, [part](const std::string& e) { return e.find(part) != std::string::npos; });
    }

protected:
    void closeSession() override {}

private:
    static device::ConsoleLine make(device::Channel channel, device::Direction dir,
                                    const std::string& text) {
        device::ConsoleLine line;
        line.channel = channel;
        line.dir = dir;
        line.time = std::chrono::system_clock::now();
        line.text = text;
        return line;
    }
    void deliver(device::ConsoleLine line) {
        line.seq = m_log->append(line);
        emit linesReceived(QList<device::ConsoleLine>{line});
    }

    std::shared_ptr<device::ConsoleLog> m_log;
    device::Transport m_transport;
    device::ConnectionEvent m_connection;
};

// Answers every question with `allowJoins` / `allowOperations`, or keeps it pending (Manual).
class ScriptedConsent final : public ConsentProvider {
public:
    enum class Mode : std::uint8_t { Allow, Deny, Manual };
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    Mode joins = Mode::Allow;
    Mode operations = Mode::Allow;
    int joinQuestions = 0;
    int withdrawn = 0;
    std::vector<OperationQuestion> operationQuestions;
    Answer pendingJoin;
    Answer pendingOperation;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

    Withdraw askJoin(const JoinQuestion& /*question*/, Answer answer) override {
        ++joinQuestions;
        return ask(joins, std::move(answer), pendingJoin);
    }
    Withdraw askOperation(const OperationQuestion& question, Answer answer) override {
        operationQuestions.push_back(question);
        return ask(operations, std::move(answer), pendingOperation);
    }

private:
    Withdraw ask(Mode mode, Answer answer, Answer& pending) {
        if (mode == Mode::Manual) {
            pending = std::move(answer);
            return [this, &pending] {
                ++withdrawn;
                pending = {};
            };
        }
        answer(mode == Mode::Allow);
        return {};
    }
};

class FakeOperations final : public HostOperations {
public:
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    bool catalogKnows = true;
    bool lteUpdate = true; // ADR-0363: lteUpdate in kinds()
    std::vector<tunnel::OpRequest> started;
    Progress progress;
    Finished finished;
    AskSupport ask; // ADR-0368: set by start() when the joiner answers questions
    int abandoned = 0;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

    [[nodiscard]] std::vector<tunnel::OpKind> kinds() const override {
        std::vector<tunnel::OpKind> kinds{tunnel::OpKind::Flash, tunnel::OpKind::Erase,
                                          tunnel::OpKind::Recover, tunnel::OpKind::Reset};
        if (lteUpdate) {
            kinds.push_back(tunnel::OpKind::LteUpdate);
        }
        return kinds;
    }
    [[nodiscard]] std::pair<std::string, std::string> lteUpdateFirmware() const override {
        if (!lteUpdate) {
            return {};
        }
        return {"HARDWARIO LTE firmware", "1.7.0"};
    }
    [[nodiscard]] Status validate(const tunnel::OpRequest& request) const override {
        if (request.kind == tunnel::OpKind::Flash && request.target != tunnel::OpTarget::App) {
            return std::unexpected(makeError(ErrorCode::UsageInvalidArgument, "flash to lte"));
        }
        return {};
    }
    void lookup(const std::string& firmwareId, LookupDone done) override {
        if (!catalogKnows) {
            done(std::unexpected(makeError(ErrorCode::RemoteFirmwareNotInCatalog, firmwareId)));
            return;
        }
        done(firmware::CatalogInfo{
            .name = "CHESTER-Clime", .version = "v3.2.1", .uploaded = {}, .sha256 = std::nullopt});
    }
    [[nodiscard]] Status start(const tunnel::OpRequest& request, Progress p, Finished f,
                               AskSupport a) override {
        started.push_back(request);
        progress = std::move(p);
        finished = std::move(f);
        ask = std::move(a);
        return {};
    }
    void abandon() override {
        ++abandoned;
        progress = {};
        finished = {};
        ask = {};
    }
};

} // namespace hwio::remote::testing
