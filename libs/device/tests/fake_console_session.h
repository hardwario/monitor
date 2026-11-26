#pragma once

// FakeSession - an in-process ConsoleSession for the CommandCapture, ShellQuery and device
// information tests (plan 0021 phase 2, plan 0063): lines are offered to a shell capture first,
// else appended to the log and emitted; sendLine() honours the hold, echoes the written line and
// answers the commands given with answer().

#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/firmware_identity.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QTimer>
#include <QtCore/qtmetamacros.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::device::test {

// Delivers lines like the real sessions: offered to a shell capture first, else appended to the
// log and emitted; sendLine() honours the hold and echoes the written line (dir In).
class FakeSession final : public ConsoleSession {
public:
    explicit FakeSession(const hwio::device::DeviceProfile& profile =
                             *hwio::device::ProfileRegistry::find("chester"))
        : m_profile(&profile) {
        m_connection.state = hwio::device::ConnectionState::Connected;
    }

    hwio::Status sendLine(std::string line) override {
        if (hwio::Status held; holdLine(line, held)) {
            return held;
        }
        m_written.push_back(line);
        lineQueued();
        if (m_delayEcho) {
            m_pendingEchoes.emplace_back(line, sendingOrigin());
        } else if (m_echo) {
            echo(line, sendingOrigin());
        }
        return {};
    }
    // The echo of a written line (with its origin), then - later, like a device - the answer:
    // the output lines and the prompt.
    void echo(const std::string& line, std::uint64_t origin) {
        deliver(Channel::Shell, Direction::In, line, origin);
        if (const auto answer = m_answers.find(line); answer != m_answers.end()) {
            QTimer::singleShot(0, this, [this, lines = answer->second] {
                for (const std::string& text : lines) {
                    output(text);
                }
                prompt();
            });
        }
    }
    // Echoes held back by setDelayEcho(true), in order.
    void releaseEchoes() {
        while (!m_pendingEchoes.empty()) {
            const auto [line, origin] = m_pendingEchoes.front();
            m_pendingEchoes.pop_front();
            echo(line, origin);
        }
    }
    hwio::Status reconnect() override { return {}; }
    [[nodiscard]] const hwio::device::ConsoleLog& log() const override { return m_log; }
    [[nodiscard]] const hwio::device::ConnectionEvent& connection() const override {
        return m_connection;
    }
    [[nodiscard]] hwio::device::SessionIdentity identity() const override { return m_identity; }
    void setIdentity(hwio::device::SessionIdentity identity) { m_identity = std::move(identity); }
    [[nodiscard]] hwio::device::Capabilities capabilities() const override { return {}; }
    [[nodiscard]] const hwio::device::DeviceProfile& profile() const override { return *m_profile; }
    [[nodiscard]] std::string_view consoleTargetId() const override { return "app"; }
    [[nodiscard]] std::filesystem::path consoleFile() const override { return {}; }
    [[nodiscard]] hwio::Status recordRemoteEvent(std::string /*text*/) override { return {}; }
    [[nodiscard]] hwio::Status
    setFirmwareIdentity(std::optional<hwio::device::FirmwareIdentity> identity,
                        std::uint64_t attach) override {
        identities.emplace_back(std::move(identity), attach);
        return {};
    }
    // Plan 0028: what setFirmwareIdentity() received (a test recorder).
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::vector<std::pair<std::optional<hwio::device::FirmwareIdentity>, std::uint64_t>> identities;

    void deliver(Channel channel, Direction dir, const std::string& text,
                 std::uint64_t origin = 0) {
        ConsoleLine line;
        line.channel = channel;
        line.dir = dir;
        line.text = text;
        line.origin = origin;
        line.time = std::chrono::system_clock::now();
        if (captureLine(line)) {
            return;
        }
        line.seq = m_log.append(line);
        emit linesReceived(QList<ConsoleLine>{line});
    }
    void output(const std::string& text) { deliver(Channel::Shell, Direction::Out, text); }
    void prompt() { emit promptSeen(); }
    void setState(hwio::device::ConnectionState state) {
        m_connection.state = state;
        emit connectionChanged(m_connection);
    }
    // Plan 0028: a new attach (ConnectionEvent::attach) reported as Connected.
    void attachAgain(std::uint64_t attach) {
        m_connection.attach = attach;
        setState(hwio::device::ConnectionState::Connected);
    }
    void setAttach(std::uint64_t attach) { m_connection.attach = attach; }

    [[nodiscard]] const std::vector<std::string>& written() const noexcept { return m_written; }
    void clearWritten() { m_written.clear(); }
    // The device's answer to `command` (then the prompt).
    void answer(const std::string& command, std::vector<std::string> lines) {
        m_answers[command] = std::move(lines);
    }
    void setEcho(bool echo) { m_echo = echo; }
    void setDelayEcho(bool delay) { m_delayEcho = delay; }

protected:
    void closeSession() override {}

private:
    const hwio::device::DeviceProfile* m_profile;
    hwio::device::ConsoleLog m_log{1000};
    hwio::device::ConnectionEvent m_connection;
    hwio::device::SessionIdentity m_identity;
    std::vector<std::string> m_written;
    std::map<std::string, std::vector<std::string>> m_answers;
    bool m_echo = true;
    bool m_delayEcho = false;
    std::deque<std::pair<std::string, std::uint64_t>> m_pendingEchoes;
};

inline bool runUntil(const std::function<bool()>& condition,
                     std::chrono::milliseconds limit = std::chrono::seconds(5)) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < limit.count()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return condition();
}

inline void runFor(std::chrono::milliseconds duration) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < duration.count()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

} // namespace hwio::device::test
