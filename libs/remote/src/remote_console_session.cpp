#include "hwio/remote/remote_console_session.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/core/text_sanitizer.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/line_assembler.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/remote/relay_client.h"
#include "hwio/remote/transcript.h"
#include "hwio/remote/tunnel.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtLogging>

#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace hwio::remote {

namespace {

QString trRemote(const char* text) { return QCoreApplication::translate("hwio::remote", text); }

} // namespace

RemoteConsoleSession::RemoteConsoleSession(JoinOptions options)
    : m_options(std::move(options)),
      m_log(m_options.consoleLog ? m_options.consoleLog : std::make_shared<device::ConsoleLog>()) {
    m_connection.transport = device::Transport::Remote;
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (m_state != JoinState::Ended) {
            emit idleNotice();
        }
    });
    m_relay = std::make_unique<RelayClient>(RelayConfig{
        .relay = m_options.relay, .code = m_options.code, .limits = m_options.relayLimits});
    connect(m_relay.get(), &RelayClient::connected, this, [this](bool reconnected) {
        if (reconnected) {
            // The host sees a new join: it asks the customer again (ADR-0335).
            setState(JoinState::Waiting);
            setConnection(device::ConnectionState::Connecting);
            (void)recordRemoteEvent(
                trRemote("Connection restored; waiting for the customer to allow you again.")
                    .toStdString());
        } else {
            setState(JoinState::Waiting);
        }
        hostActivity();
    });
    connect(m_relay.get(), &RelayClient::connectionLost, this, [this](const Error& error) {
        setConnection(device::ConnectionState::Disconnected, error);
    });
    connect(m_relay.get(), &RelayClient::failed, this, [this](const Error& error) { end(error); });
    connect(m_relay.get(), &RelayClient::atReceived, this, &RemoteConsoleSession::onAt);
    connect(m_relay.get(), &RelayClient::syncReceived, this, &RemoteConsoleSession::onSync);
    connect(m_relay.get(), &RelayClient::writeReceived, this, [this](const QJsonValue& payload) {
        // Another participant's line (the host shows its echo as at/write); tunnel objects of
        // other Monitor joiners are not for us.
        if (payload.isString()) {
            hostActivity();
        }
    });
    connect(m_relay.get(), &RelayClient::newClient, this, [this] { hostActivity(); });
    m_relay->start();
}

RemoteConsoleSession::~RemoteConsoleSession() { closeSession(); }

void RemoteConsoleSession::closeSession() {
    if (m_relay) {
        m_relay->stop();
    }
    m_idleTimer.stop();
    if (m_state != JoinState::Ended) {
        m_state = JoinState::Ended;
    }
}

Status RemoteConsoleSession::sendLine(std::string line) {
    if (m_state == JoinState::Ended) {
        return fail(ErrorCode::RemoteHostEnded);
    }
    if (line.size() > maxLineBytes) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, std::to_string(line.size()));
        error.message = trRemote("A command line must not be longer than %1 bytes.")
                            .arg(maxLineBytes)
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    for (const char c : line) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte < 0x20 && byte != '\t') || byte == 0x7F) {
            return std::unexpected(
                makeError(ErrorCode::UsageInvalidArgument, "control character in the command"));
        }
    }
    if (!m_relay || !m_relay->isConnected()) {
        return std::unexpected(
            makeError(ErrorCode::TargetNotConnected, "not connected to the remote session"));
    }
    if (Status held; holdLine(line, held)) {
        return held; // a silent shell query runs (plan 0021)
    }
    m_relay->emitEvent(QStringLiteral("write"), toQString(line), false);
    lineQueued();
    return {};
}

Status RemoteConsoleSession::reconnect() { return {}; }

Status RemoteConsoleSession::recordRemoteEvent(std::string text) {
    device::ConsoleLine marker;
    marker.channel = device::Channel::Shell;
    marker.dir = device::Direction::Marker;
    marker.time = std::chrono::system_clock::now();
    marker.text = boundedRemoteEvent(std::move(text));
    marker.source = device::LineSource::Remote;
    appendLines({std::move(marker)});
    return {};
}

device::SessionIdentity RemoteConsoleSession::identity() const {
    return device::SessionIdentity{.transport = device::Transport::Remote,
                                   .id = m_options.code.text(),
                                   .displayName = toStdString(title())};
}

device::Capabilities RemoteConsoleSession::capabilities() const {
    device::Capabilities capabilities;
    if (m_hostHello && m_hostHello->ops) {
        capabilities |= device::Capability::Operations;
    }
    return capabilities;
}

const device::DeviceProfile& RemoteConsoleSession::profile() const {
    // The remote device is a CHESTER (the only profile; its console rules apply to the view).
    return *device::ProfileRegistry::find("chester");
}

Status RemoteConsoleSession::setFirmwareIdentity(std::optional<FirmwareIdentity> /*identity*/,
                                                 std::uint64_t /*attach*/) {
    return {}; // no console file of its own
}

QString RemoteConsoleSession::title() const {
    const QString session = trRemote("Session %1").arg(toQString(m_options.code.grouped()));
    if (m_hostHello) {
        QString device = toQString(m_hostHello->device.name);
        if (!m_hostHello->device.serial.empty()) {
            device += QLatin1Char(' ') + toQString(m_hostHello->device.serial);
        }
        return trRemote("%1 — remote").arg(device.isEmpty() ? session : device);
    }
    if (m_syncSeen) {
        return trRemote("%1 — web Terminal").arg(session);
    }
    return session;
}

Status RemoteConsoleSession::sendTunnel(const tunnel::JoinerMessage& message) {
    if (m_state == JoinState::Ended) {
        return fail(ErrorCode::RemoteHostEnded);
    }
    if (!m_hostHello) {
        return std::unexpected(
            makeError(ErrorCode::RemoteOperationUnsupportedPeer, "no host hello"));
    }
    if (std::holds_alternative<tunnel::OpRequest>(message) && !m_hostHello->ops) {
        return std::unexpected(
            makeError(ErrorCode::RemoteOperationUnsupportedPeer, "the host has no operations"));
    }
    m_relay->emitEvent(QStringLiteral("write"), tunnel::encodeJoiner(message), false);
    return {};
}

device::ConsoleLine RemoteConsoleSession::makeLine(device::Direction dir, const QString& text) {
    device::ConsoleLine line;
    line.dir = dir;
    line.time = std::chrono::system_clock::now();
    line.text = sanitizeDeviceText(toStdString(text));
    line.source = device::LineSource::Remote;
    if (dir == device::Direction::Out) {
        line.level = device::parseZephyrLevel(line.text);
        line.channel =
            line.level != device::LogLevel::None ? device::Channel::Log : device::Channel::Shell;
    }
    return line;
}

void RemoteConsoleSession::onAt(const QJsonValue& payload) {
    if (m_state == JoinState::Ended) {
        return;
    }
    hostActivity();
    if (tunnel::isTunnelAt(payload)) {
        auto message = tunnel::decodeHost(payload);
        if (!message) {
            emit warning(message.error());
            return;
        }
        if (const auto* hello = std::get_if<tunnel::HostHello>(&*message)) {
            const bool first = !m_hostHello;
            m_hostHello = *hello;
            // ADR-0336: our tunnel objects only after the host's hello.
            m_relay->emitEvent(
                QStringLiteral("write"),
                tunnel::encodeJoiner(tunnel::JoinerHello{.app = std::string(tunnel::appId),
                                                         .version = m_options.appVersion}),
                false);
            if (first) {
                (void)recordRemoteEvent(
                    trRemote("Connected to HARDWARIO Monitor %1 of the customer.")
                        .arg(toQString(hello->version))
                        .toStdString());
            }
            emit hostHelloReceived();
            return;
        }
        emit hostMessage(*message);
        return;
    }
    const QJsonObject at = payload.toObject();
    const QString event = at.value(QStringLiteral("event")).toString();
    const QJsonValue data = at.value(QStringLiteral("data"));
    if (event == QStringLiteral("state")) {
        const QString state = data.toString();
        if (state == QStringLiteral("connected")) {
            setConnection(device::ConnectionState::Connected);
        } else if (state == QStringLiteral("connection")) {
            setConnection(device::ConnectionState::Connecting);
        } else if (state == QStringLiteral("disconnected")) {
            Error error =
                makeError(ErrorCode::TargetNotConnected, "the host's device disconnected");
            error.message = trRemote("The customer's device is disconnected.").toStdString();
            setConnection(device::ConnectionState::Disconnected, std::move(error));
        }
        return;
    }
    if (!data.isString()) {
        return; // the web Terminal ignores other events too
    }
    if (event == QStringLiteral("write")) {
        appendLines({makeLine(device::Direction::In, data.toString())});
    } else if (event == QStringLiteral("read")) {
        if (m_connection.state != device::ConnectionState::Connected) {
            setConnection(device::ConnectionState::Connected); // output proves the link works
        }
        appendLines({makeLine(device::Direction::Out, data.toString())});
    }
}

void RemoteConsoleSession::onSync(const QJsonValue& payload) {
    if (m_state == JoinState::Ended) {
        return;
    }
    hostActivity();
    const QJsonObject sync = payload.toObject();
    auto transcript =
        Transcript::fromJson(sync.value(QStringLiteral("commands")), m_options.transcript);
    if (!transcript) {
        emit warning(transcript.error());
        return;
    }
    m_syncSeen = true;
    m_log->clear(device::ChannelMask::All);
    QList<device::ConsoleLine> lines;
    for (const TranscriptEntry& entry : transcript->entries()) {
        if (entry.command != QString::fromLatin1(deviceOutputCommand) || entry.index != 0) {
            lines.append(makeLine(device::Direction::In, entry.command));
        }
        for (const QString& output : entry.outputs) {
            lines.append(makeLine(device::Direction::Out, output));
        }
    }
    for (device::ConsoleLine& line : lines) {
        line.seq = m_log->append(line);
    }
    if (transcript->truncated()) {
        device::ConsoleLine marker;
        marker.dir = device::Direction::Marker;
        marker.time = std::chrono::system_clock::now();
        marker.text = trRemote("Older history of the session was left out.").toStdString();
        marker.source = device::LineSource::Remote;
        marker.seq = m_log->append(marker);
    }
    const QString state = sync.value(QStringLiteral("state")).toString();
    if (state == QStringLiteral("connected")) {
        setConnection(device::ConnectionState::Connected);
    } else if (state == QStringLiteral("connection")) {
        setConnection(device::ConnectionState::Connecting);
    } else if (state == QStringLiteral("disconnected")) {
        setConnection(device::ConnectionState::Disconnected);
    }
    setState(JoinState::Allowed);
    emit transcriptReplaced();
}

void RemoteConsoleSession::appendLines(QList<device::ConsoleLine> lines) {
    QList<device::ConsoleLine> batch;
    for (device::ConsoleLine& line : lines) {
        if (captureLine(line)) {
            continue; // a silent shell query took it
        }
        line.seq = m_log->append(line);
        batch.append(std::move(line));
    }
    if (!batch.isEmpty()) {
        emit linesReceived(batch);
    }
}

void RemoteConsoleSession::setConnection(device::ConnectionState state,
                                         std::optional<Error> error) {
    if (m_connection.state == state && !error) {
        return;
    }
    if (state == device::ConnectionState::Connected &&
        m_connection.state != device::ConnectionState::Connected) {
        ++m_attach;
    }
    m_connection.state = state;
    m_connection.error = std::move(error);
    m_connection.attach = m_attach;
    emit connectionChanged(m_connection);
}

void RemoteConsoleSession::setState(JoinState state) {
    if (m_state == state || m_state == JoinState::Ended) {
        return;
    }
    m_state = state;
    emit joinStateChanged();
}

void RemoteConsoleSession::hostActivity() {
    if (m_state != JoinState::Ended) {
        m_idleTimer.start(m_options.idleNoticeAfter);
    }
}

void RemoteConsoleSession::end(std::optional<Error> error) {
    if (m_state == JoinState::Ended) {
        return;
    }
    m_endError = error;
    m_idleTimer.stop();
    const bool wasInSession = m_state != JoinState::Connecting;
    m_state = JoinState::Ended;
    Error stopped = error.value_or(makeError(ErrorCode::RemoteHostEnded));
    if (stopped.code == ErrorCode::RemoteRelayUnreachable && wasInSession) {
        // The relay connection is gone for good: for the joiner the session has ended.
        Error ended = makeError(ErrorCode::RemoteHostEnded, stopped.detail);
        stopped = std::move(ended);
    }
    setConnection(device::ConnectionState::Stopped, stopped);
    emit joinStateChanged();
}

} // namespace hwio::remote
