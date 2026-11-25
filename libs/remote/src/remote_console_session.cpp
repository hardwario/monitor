#include "hwio/remote/remote_console_session.h"

#include "host_output_reader.h"
#include "hwio/core/error.h"
#include "hwio/core/firmware_identity.h"
#include "hwio/core/json.h"
#include "hwio/core/localized_text.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/core/text_sanitizer.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/remote/protocol.h"
#include "hwio/remote/relay_client.h"
#include "hwio/remote/transcript.h"
#include "hwio/remote/tunnel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QtCore/qtmetamacros.h>
#include <QtCore/qttranslation.h>
#include <QtLogging>
#include <QtTranslation>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hwio::remote {

namespace {

// Callers pass QT_TRANSLATE_NOOP / QT_TRANSLATE_N_NOOP literals so lupdate extracts them.
QString trRemote(const char* text) { return QCoreApplication::translate("hwio::remote", text); }

std::size_t utf8Size(const QString& text) { return static_cast<std::size_t>(text.toUtf8().size()); }

// ADR-0430: what a received sync carried before sanitising (counts and lengths, never the text).
struct RawSyncStats {
    std::size_t entries = 0;
    std::size_t outputs = 0;
    std::size_t bytes = 0; // UTF-8 bytes of the commands and outputs as received
    std::size_t lineFeeds = 0;
};

RawSyncStats rawSyncStats(const QJsonValue& commands) {
    RawSyncStats stats;
    const auto count = [&stats](const QString& text) {
        stats.bytes += utf8Size(text);
        stats.lineFeeds += static_cast<std::size_t>(text.count(QLatin1Char('\n')));
    };
    const QJsonArray array = commands.toArray();
    for (const auto& value : array) {
        const QJsonObject entry = value.toObject();
        ++stats.entries;
        count(entry.value(QStringLiteral("command")).toString());
        const QJsonArray outputs = entry.value(QStringLiteral("response"))
                                       .toObject()
                                       .value(QStringLiteral("outputs"))
                                       .toArray();
        for (const auto& output : outputs) {
            if (output.isString()) {
                ++stats.outputs;
                count(output.toString());
            }
        }
    }
    return stats;
}

} // namespace

Status RemoteConsoleSession::hostOffers(const tunnel::HostHello& hello,
                                        const tunnel::OpRequest& request) {
    if (!hello.targets) {
        // An older host (2.0.0-rc.4 / rc.5, ADR-0414): its ops / opKinds, the CHESTER targets
        // app / lte, catalog firmware IDs only. Anything else would not decode there and get no
        // answer at all.
        if (!hello.ops) {
            return std::unexpected(
                makeError(ErrorCode::RemoteOperationUnsupportedPeer, "the host has no operations"));
        }
        if (!request.target.isChesterTarget() || request.prepared) {
            return std::unexpected(makeError(ErrorCode::RemoteTargetUnsupported,
                                             "the host offers the app and lte targets only"));
        }
        return {};
    }
    // A host that lists its targets: the target, its kind and its prepared firmware must be
    // listed (the host validates again).
    const tunnel::HostTarget* target = request.kind == tunnel::OpKind::LteUpdate
                                           ? nullptr
                                           : tunnel::findTarget(hello, request.target.id());
    if (request.kind == tunnel::OpKind::LteUpdate) {
        if (!tunnel::supports(hello, request.kind)) {
            return std::unexpected(makeError(ErrorCode::RemoteOperationUnsupportedPeer,
                                             "the host has no remote LTE update"));
        }
        return {};
    }
    if (target == nullptr ||
        std::ranges::find(target->kinds, request.kind) == target->kinds.end()) {
        return std::unexpected(
            makeError(ErrorCode::RemoteTargetUnsupported,
                      std::format("the host offers no {} on {}", tunnel::kindId(request.kind),
                                  request.target.id())));
    }
    if (request.prepared &&
        (!tunnel::hasCap(hello.caps, tunnel::cap::prepared) ||
         std::ranges::find(target->prepared, *request.prepared, &tunnel::PreparedFirmware::id) ==
             target->prepared.end())) {
        return std::unexpected(makeError(ErrorCode::RemoteTargetUnsupported,
                                         "the host has no such prepared firmware"));
    }
    if (request.firmwareId && !target->catalog) {
        return std::unexpected(makeError(ErrorCode::RemoteFirmwareNotInCatalog,
                                         "the target takes no catalog firmware"));
    }
    return {};
}

RemoteConsoleSession::RemoteConsoleSession(JoinOptions options)
    : m_options(std::move(options)),
      m_log(m_options.consoleLog ? m_options.consoleLog : std::make_shared<device::ConsoleLog>()),
      m_reader(std::make_unique<HostOutputReader>()) {
    m_connection.transport = device::Transport::Remote;
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (m_state != JoinState::Ended) {
            emit idleNotice();
        }
    });
    m_relay = std::make_unique<RelayClient>(
        RelayConfig{.relay = m_options.relay,
                    .code = m_options.code,
                    .limits = m_options.relayLimits,
                    .incomingEventBytes = joinerIncomingEventBytes}); // ADR-0365
    connect(m_relay.get(), &RelayClient::connected, this, [this](bool reconnected) {
        if (reconnected) {
            // The host sees a new join: it asks the customer again (ADR-0335).
            setState(JoinState::Waiting);
            setConnection(device::ConnectionState::Connecting);
            if (Status recorded = recordRemoteEvent(
                    trRemote(
                        QT_TRANSLATE_NOOP(
                            "hwio::remote",
                            "— Connection restored; waiting for the customer to allow you again —"))
                        .toStdString());
                !recorded) {
                qCWarning(log::lcRemote).noquote()
                    << "Remote event not recorded:" << toQString(recorded.error().detail);
            }
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
        setText(
            error, ErrorText::Message, "hwio::remote",
            QT_TRANSLATE_NOOP("hwio::remote", "A command line must not be longer than %1 bytes."),
            {maxLineBytes});
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
    if (m_hostHello && tunnel::offersOperations(*m_hostHello)) { // plan 0059
        capabilities |= device::Capability::Operations;
    }
    return capabilities;
}

const device::DeviceProfile&
RemoteConsoleSession::profileFor(const std::optional<tunnel::HostHello>& hello) {
    // Plan 0059 (ADR-0415, D-6): the profile the host's hello names when this build knows it,
    // else the neutral console profile. A host without `profile` in its hello (2.0.0-rc.4 /
    // rc.5) and a host without a hello (the web Terminal: Bluetooth CHESTER) share CHESTERs only:
    // their console keeps the CHESTER rules (see plan 0059 § Implementation notes, A2).
    if (!hello || hello->profile.empty()) {
        return *device::ProfileRegistry::find("chester");
    }
    const device::DeviceProfile* named = device::ProfileRegistry::find(hello->profile);
    return named != nullptr ? *named : device::genericConsoleProfile();
}

const device::DeviceProfile& RemoteConsoleSession::profile() const {
    return profileFor(m_hostHello);
}

Status RemoteConsoleSession::setFirmwareIdentity(std::optional<FirmwareIdentity> /*identity*/,
                                                 std::uint64_t /*attach*/) {
    return {}; // no console file of its own
}

QString RemoteConsoleSession::title() const {
    QString session = trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Session %1"))
                          .arg(toQString(m_options.code.grouped()));
    if (m_hostHello) {
        QString device = toQString(m_hostHello->device.name);
        if (!m_hostHello->device.serial.empty()) {
            device += QLatin1Char(' ') + toQString(m_hostHello->device.serial);
        }
        return trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1 — remote"))
            .arg(device.isEmpty() ? session : device);
    }
    if (m_syncSeen) {
        // D12 (plan 0061): a host without a hello is the web Terminal or the mobile app.
        return trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1 — web Terminal or mobile app"))
            .arg(session);
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
    if (const auto* request = std::get_if<tunnel::OpRequest>(&message)) {
        if (Status offered = hostOffers(*m_hostHello, *request); !offered) {
            return offered;
        }
    }
    m_relay->emitEvent(QStringLiteral("write"), tunnel::encodeJoiner(message), false);
    return {};
}

void RemoteConsoleSession::requestLearn(const std::vector<std::string>& path) {
    if (m_state != JoinState::Allowed || !m_hostHello || !m_commandTree || path.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (const auto asked = m_learnAsked.find(path);
        asked != m_learnAsked.end() && now - asked->second < learnRetry) {
        return; // asked recently (the host may have dropped it: asked again later)
    }
    if (!m_learnAsked.contains(path) && m_learnAsked.size() >= maxLearnPaths) {
        return;
    }
    m_learnAsked[path] = now;
    if (Status sent = sendTunnel(tunnel::Learn{.path = path}); !sent) {
        qCInfo(log::lcRemote).noquote()
            << "Learn request not sent:" << toQString(sent.error().detail);
    }
}

device::ConsoleLine RemoteConsoleSession::makeLine(device::Direction dir, const QString& text) {
    device::ConsoleLine line;
    line.dir = dir;
    line.time = std::chrono::system_clock::now();
    line.text = sanitizeDeviceText(toStdString(text));
    // The device lines of the host (LineSource::Remote marks only this session's own markers).
    if (dir == device::Direction::Out) {
        classifyHostOutput(line);
    }
    return line;
}

void RemoteConsoleSession::onTunnelAt(const QJsonValue& payload) {
    auto message = tunnel::decodeHost(payload);
    if (!message) {
        emit warning(message.error());
        return;
    }
    if (const auto* hello = std::get_if<tunnel::HostHello>(&*message)) {
        const bool first = !m_hostHello;
        m_hostHello = *hello;
        m_reader->hostHello(); // ADR-0431: a Monitor host sends lines
        // ADR-0336: our tunnel objects only after the host's hello.
        // Plan 0059 (ADR-0414): this joiner understands targets, detail codes and line-refused.
        m_relay->emitEvent(
            QStringLiteral("write"),
            tunnel::encodeJoiner(tunnel::JoinerHello{
                .app = std::string(tunnel::appId),
                .version = m_options.appVersion,
                .caps = {std::string(tunnel::cap::targets), std::string(tunnel::cap::detailCodes),
                         std::string(tunnel::cap::refusedLines)}}),
            false);
        if (first) {
            if (Status recorded = recordRemoteEvent(
                    trRemote(
                        QT_TRANSLATE_NOOP("hwio::remote",
                                          "— Connected to HARDWARIO Monitor %1 of the customer —"))
                        .arg(toQString(hello->version))
                        .toStdString());
                !recorded) {
                qCWarning(log::lcRemote).noquote()
                    << "Remote event not recorded:" << toQString(recorded.error().detail);
            }
        }
        emit hostHelloReceived();
        return;
    }
    if (!m_hostHello) {
        return; // ADR-0336: host objects count only after the host's hello
    }
    if (const auto* commands = std::get_if<tunnel::HostCommands>(&*message)) {
        // ADR-0364: validated by the codec (bounds, names, sanitised help).
        m_commandTree = commands->tree;
        emit commandTreeChanged();
        return;
    }
    if (const auto* refused = std::get_if<tunnel::LineRefused>(&*message)) {
        // Plan 0059 (ADR-0416): the customer's Monitor did not pass a line of support's.
        if (Status recorded = recordRemoteEvent(
                trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                           "— The customer's Monitor refused this command: it "
                                           "changes the device's keys or identity —"))
                    .toStdString());
            !recorded) {
            qCWarning(log::lcRemote).noquote()
                << "Remote event not recorded:" << toQString(recorded.error().detail);
        }
        // Plan 0062 (A1): for the MCP send_command of an AI client (an error with this id). A copy:
        // a slot may end this session.
        const QString code = toQString(refused->code);
        emit lineRefused(code);
        return;
    }
    if (std::holds_alternative<tunnel::HostEnd>(*message)) {
        // ADR-0354: the host ended sharing on purpose.
        end(makeError(ErrorCode::RemoteHostEnded,
                      "the host ended the session: " + std::get<tunnel::HostEnd>(*message).code));
        return;
    }
    emit hostMessage(*message);
}

void RemoteConsoleSession::onAt(const QJsonValue& payload) {
    if (m_state == JoinState::Ended) {
        return;
    }
    hostActivity();
    if (tunnel::isTunnelAt(payload)) {
        onTunnelAt(payload);
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
            setText(error, ErrorText::Message, "hwio::remote",
                    QT_TRANSLATE_NOOP("hwio::remote", "The customer's device is disconnected."));
            setConnection(device::ConnectionState::Disconnected, std::move(error));
        }
        return;
    }
    if (event == QStringLiteral("end") && !m_hostHello) {
        // ADR-0431 (D11): the HARDWARIO mobile app's `at` {event: "end", data: null} when it stops
        // sharing; like the tunnel `end` of a Monitor host (ADR-0354).
        end(makeError(ErrorCode::RemoteHostEnded, "the host ended the session: at end"));
        return;
    }
    if (!data.isString()) {
        return; // the web Terminal ignores other events too
    }
    const bool write = event == QStringLiteral("write");
    if (!write && event != QStringLiteral("read")) {
        return;
    }
    if (!write && m_connection.state != device::ConnectionState::Connected) {
        setConnection(device::ConnectionState::Connected); // output proves the link works
    }
    const QString text = data.toString();
    QList<device::ConsoleLine> lines;
    if (write) {
        lines.append(makeLine(device::Direction::In, text));
        m_reader->commandShown();
    } else {
        // ADR-0431: one line (a Monitor or web Terminal host) or a piece of the stream (the mobile
        // app): the reader assembles the lines.
        for (device::ConsoleLine& line : m_reader->read(text, std::chrono::system_clock::now())) {
            lines.append(std::move(line));
        }
    }
    std::size_t bytesAfter = 0;
    for (device::ConsoleLine& line : lines) {
        hideSecrets(line);
        bytesAfter += line.text.size();
    }
    // ADR-0430: counts and lengths of the event before and after sanitising, never the text.
    qCDebug(log::lcRemote).noquote() << QString::fromStdString(std::format(
        "Joiner {} received: {} bytes, {} line feeds; {} lines, {} bytes after sanitising{}",
        write ? "write" : "read", utf8Size(text), text.count(QLatin1Char('\n')), lines.size(),
        bytesAfter, m_reader->streamMode() ? " (stream)" : ""));
    if (!lines.isEmpty()) {
        appendLines(std::move(lines));
    }
}

void RemoteConsoleSession::hideSecrets(device::ConsoleLine& line) const {
    // ADR-0432 (D9): a host without a hello (web Terminal, mobile app) hides nothing; the console
    // file's rules apply before a line enters the log (screen, Copy, Save view, the CLI). A Monitor
    // host has hidden them already (ADR-0356, ADR-0432 D10).
    if (m_hostHello) {
        return;
    }
    if (auto redacted = m_redaction.redacted(line)) {
        line.text = std::move(*redacted);
    }
}

void RemoteConsoleSession::appendHistory(const Transcript& transcript,
                                         QList<device::ConsoleLine>& lines) {
    // ADR-0431: the log is replaced; a history in stream form (the mobile app) is read like the
    // live output: no prompt lines, no empty line right after a command or a prompt.
    m_reader->restart();
    if (transcript.streamForm()) {
        m_reader->streamSeen();
    }
    for (const TranscriptEntry& entry : transcript.entries()) {
        if (entry.command != QString::fromLatin1(deviceOutputCommand) || entry.index != 0) {
            lines.append(makeLine(device::Direction::In, entry.command));
            m_reader->commandShown();
        }
        for (const QString& output : entry.outputs) {
            device::ConsoleLine line = makeLine(device::Direction::Out, output);
            if (m_reader->keepHistoryLine(line)) {
                lines.append(std::move(line));
            }
        }
    }
    if (!transcript.unfinishedTail().isEmpty()) {
        // The newest entry's unfinished line (usually the prompt) continues in the live output.
        for (device::ConsoleLine& line :
             m_reader->read(transcript.unfinishedTail(), std::chrono::system_clock::now())) {
            lines.append(std::move(line));
        }
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
    const RawSyncStats raw = rawSyncStats(sync.value(QStringLiteral("commands")));
    m_syncSeen = true;
    m_log->clear(device::ChannelMask::All);
    QList<device::ConsoleLine> lines;
    const auto marker = [](const QString& text) {
        device::ConsoleLine line;
        line.dir = device::Direction::Marker;
        line.time = std::chrono::system_clock::now();
        line.text = toStdString(text);
        line.source = device::LineSource::Remote;
        return line;
    };
    lines.append(marker(
        trRemote(QT_TRANSLATE_NOOP("hwio::remote", "— Session history from the customer —"))));
    if (transcript->truncated()) {
        lines.append(marker(trRemote(
            QT_TRANSLATE_NOOP("hwio::remote", "— Older history of the session was left out —"))));
    }
    appendHistory(*transcript, lines);
    for (device::ConsoleLine& line : lines) {
        hideSecrets(line);
    }
    // The views reload the replaced log (transcriptReplaced); consumers that only follow
    // linesReceived (the CLI) get the history as new lines.
    std::size_t shownLines = 0;
    std::size_t shownBytes = 0;
    std::size_t emptyLines = 0;
    for (device::ConsoleLine& line : lines) {
        if (line.dir != device::Direction::Marker) {
            ++shownLines;
            shownBytes += line.text.size();
            emptyLines += line.text.empty() ? 1 : 0;
        }
        line.seq = m_log->append(line);
    }
    // ADR-0430: one line per sync, counts and lengths only.
    qCDebug(log::lcRemote).noquote() << QString::fromStdString(std::format(
        "Joiner sync received: {} entries, {} output strings, {} bytes, {} line feeds; "
        "kept {} entries, truncated: {}; {} lines, {} bytes after sanitising, {} empty",
        raw.entries, raw.outputs, raw.bytes, raw.lineFeeds, transcript->entries().size(),
        transcript->truncated() ? "yes" : "no", shownLines, shownBytes, emptyLines));
    emit linesReceived(lines);
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

void RemoteConsoleSession::end(const std::optional<Error>& error) {
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
