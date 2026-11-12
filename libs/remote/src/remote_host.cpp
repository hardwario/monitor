#include "hwio/remote/remote_host.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/command_tree.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/remote/consent.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/protocol.h"
#include "hwio/remote/relay_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/transcript.h"
#include "hwio/remote/tunnel.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtCore/qttranslation.h>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace hwio::remote {

namespace {

// Not tr(): inside the QObject members that would be QObject::tr with the class as context.
// Callers pass QT_TRANSLATE_NOOP / QT_TRANSLATE_N_NOOP literals so lupdate extracts them.
QString trRemote(const char* text) { return QCoreApplication::translate("hwio::remote", text); }
QString trRemoteN(const char* text, int n) {
    return QCoreApplication::translate("hwio::remote", text, nullptr, n);
}

QString stateOf(device::ConnectionState state) {
    switch (state) {
    case device::ConnectionState::Connected:
        return QStringLiteral("connected");
    case device::ConnectionState::Connecting:
    case device::ConnectionState::ProbeOpen:
        return QStringLiteral("connection");
    case device::ConnectionState::Disconnected:
    case device::ConnectionState::Stopped:
        return QStringLiteral("disconnected");
    }
    return QStringLiteral("disconnected");
}

bool hasControlCharacters(std::string_view line) {
    return std::ranges::any_of(line, [](const char c) {
        const auto byte = static_cast<unsigned char>(c);
        return (byte < 0x20 && byte != '\t') || byte == 0x7F;
    });
}

} // namespace

RemoteHost::RemoteHost(device::ConsoleSession& session, ConsentProvider& consent,
                       HostOperations* operations, HostOptions options, QObject* parent)
    : QObject(parent), m_session(&session), m_consent(&consent), m_operations(operations),
      m_options(std::move(options)), m_code(m_options.code.value_or(SessionCode::generate())),
      m_transcript(m_options.limits.transcript) {
    for (QTimer* timer :
         {&m_collisionTimer, &m_joinTimer, &m_operationTimer, &m_idleWarningTimer, &m_idleTimer,
          &m_lifetimeTimer, &m_helloTimer, &m_commandsTimer, &m_questionTimer}) {
        timer->setSingleShot(true);
    }
    // ADR-0368: no answer from support in time = no (cancelled before anything changed).
    connect(&m_questionTimer, &QTimer::timeout, this, [this] {
        if (!m_question) {
            return;
        }
        audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                         "Support did not answer within %1 s: cancelled."))
                  .arg(std::chrono::duration_cast<std::chrono::seconds>(
                           m_options.limits.operationTimeout)
                           .count()));
        SupportQuestion question = std::move(*m_question);
        m_question.reset();
        if (question.answer) {
            question.answer(false);
        }
    });
    connect(&m_commandsTimer, &QTimer::timeout, this, &RemoteHost::sendCommands);
    connect(&m_collisionTimer, &QTimer::timeout, this, [this] {
        m_inCollisionWindow = false;
        if (m_state == HostState::Connecting && !m_everShared) {
            m_everShared = true;
            m_state = HostState::Sharing;
            audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Sharing started, session %1 (%2)."))
                      .arg(toQString(m_code.grouped()), m_options.relay.host()));
            sendAt(QStringLiteral("state"), m_deviceState, false);
            m_lifetimeTimer.start(m_options.limits.maxLifetime);
            activity();
            emit sharingStarted();
        }
    });
    connect(&m_joinTimer, &QTimer::timeout, this, [this] {
        audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                         "The join question was not answered within 2 minutes.")));
        finish(makeError(ErrorCode::RemoteJoinNotAllowed, "join approval timed out"));
    });
    connect(&m_operationTimer, &QTimer::timeout, this, [this] {
        if (m_operation && !m_operation->accepted) {
            if (m_operation->withdraw) {
                std::exchange(m_operation->withdraw, {})();
            }
            const std::string id = m_operation->request.id;
            const QString summary = operationSummary(m_operation->question);
            m_operation.reset();
            denyOperation(
                id, ErrorCode::RemoteOperationDenied,
                trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: denied (no answer within 60 s)."))
                    .arg(summary));
        }
    });
    connect(&m_idleWarningTimer, &QTimer::timeout, this, [this] {
        emit idleWarning(
            std::chrono::duration_cast<std::chrono::seconds>(m_options.limits.idleWarningBefore));
    });
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        audit(trRemote(
            QT_TRANSLATE_NOOP("hwio::remote", "Sharing ended after 30 minutes without activity.")));
        Error error = makeError(ErrorCode::Timeout, "remote session idle");
        error.message =
            trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                       "Sharing ended after 30 minutes without activity."))
                .toStdString();
        error.cause.clear();
        error.suggestion =
            trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                       "Start sharing again if support still needs the console."))
                .toStdString();
        finish(std::move(error));
    });
    connect(&m_lifetimeTimer, &QTimer::timeout, this, [this] {
        audit(trRemote(
            QT_TRANSLATE_NOOP("hwio::remote", "Sharing ended after the maximum of 8 hours.")));
        Error error = makeError(ErrorCode::Timeout, "remote session lifetime");
        error.message = trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                                   "Sharing ended after the maximum of 8 hours."))
                            .toStdString();
        error.cause.clear();
        error.suggestion =
            trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                       "Start sharing again if support still needs the console."))
                .toStdString();
        finish(std::move(error));
    });
    connect(&m_helloTimer, &QTimer::timeout, this, [this] {
        if (m_peerApp.empty() && m_allowed) {
            audit(trRemote(
                QT_TRANSLATE_NOOP("hwio::remote", "Participant app: web Terminal or unknown.")));
        }
    });
    connect(&session, &device::ConsoleSession::linesReceived, this, &RemoteHost::onLines);
    connect(&session, &device::ConsoleSession::connectionChanged, this, &RemoteHost::onConnection);
    connect(&session, &device::ConsoleSession::firmwareIdentityAdopted, this, [this] {
        if (m_helloSent) {
            sendHello(); // the device firmware is known now (or changed)
        }
    });
    connect(&session, &QObject::destroyed, this, [this] {
        finish(makeError(ErrorCode::RemoteHostEnded, "the shared console was closed"));
    });
    m_deviceState = stateOf(session.connection().state);
}

RemoteHost::~RemoteHost() { finish(std::nullopt, false); }

QUrl RemoteHost::link() const { return sessionLink(m_options.relay, m_code); }

void RemoteHost::start() {
    if (m_state != HostState::Connecting || m_relay) {
        return;
    }
    connectRelay();
}

void RemoteHost::connectRelay() {
    ++m_codeTries;
    RelayConfig config{.relay = m_options.relay,
                       .code = m_code,
                       .limits = m_options.limits.relay,
                       .incomingEventBytes = hostIncomingEventBytes};
    m_relay = std::make_unique<RelayClient>(std::move(config));
    connect(m_relay.get(), &RelayClient::connected, this, &RemoteHost::onRelayConnected);
    connect(m_relay.get(), &RelayClient::connectionLost, this, [this](const Error& /*error*/) {
        m_collisionTimer.stop();
        emit connectionLost();
    });
    connect(m_relay.get(), &RelayClient::failed, this,
            [this](const Error& error) { finish(error); });
    connect(m_relay.get(), &RelayClient::newClient, this, &RemoteHost::onNewClient);
    connect(m_relay.get(), &RelayClient::atReceived, this, &RemoteHost::onAtOrSync);
    connect(m_relay.get(), &RelayClient::syncReceived, this, &RemoteHost::onAtOrSync);
    connect(m_relay.get(), &RelayClient::writeReceived, this, &RemoteHost::onWrite);
    connect(m_relay.get(), &RelayClient::warning, this,
            [this](const Error& error) { emit warning(error); });
    m_relay->start();
}

void RemoteHost::onRelayConnected(bool reconnected) {
    m_ownNewClientPending = true;
    if (!m_everShared) {
        m_inCollisionWindow = true;
        m_collisionTimer.start(m_options.limits.collisionWindow);
        return;
    }
    if (reconnected) {
        emit connectionRestored();
        audit(trRemote(
            QT_TRANSLATE_NOOP("hwio::remote", "Connection to the support server restored.")));
        sendAt(QStringLiteral("state"), m_deviceState, false);
        if (m_allowed) {
            // A participant that joined while the host was away was never announced: ask again.
            m_allowed = false;
            askJoin();
        }
    }
}

void RemoteHost::onAtOrSync() {
    if (m_state == HostState::Ended) {
        return;
    }
    if (m_inCollisionWindow && !m_everShared && m_codeTries < m_options.limits.maxCodeTries) {
        qCInfo(log::lcRemote) << "Session code in use by another host, picking a new one";
        m_collisionTimer.stop();
        m_inCollisionWindow = false;
        m_relay->stop();
        m_relay.release()->deleteLater(); // inside its own signal
        m_code = SessionCode::generate();
        connectRelay();
        return;
    }
    if (m_everShared) {
        audit(trRemote(QT_TRANSLATE_NOOP(
            "hwio::remote",
            "Another device uses this session code; the session was ended for safety.")));
    }
    finish(makeError(ErrorCode::RemoteCodeCollision, "at or sync received by the host"));
}

void RemoteHost::onNewClient() {
    if (m_state == HostState::Ended) {
        return;
    }
    if (m_ownNewClientPending) {
        m_ownNewClientPending = false; // the host's own connect
        return;
    }
    audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Someone joined the session.")));
    askJoin();
}

void RemoteHost::askJoin() {
    if (m_joinPending && m_joinWithdraw) {
        std::exchange(m_joinWithdraw, {})(); // asked again: a fresh question and timeout
    }
    m_joinPending = true;
    // A pending operation question is denied (busy): one question at a time.
    if (m_operation && !m_operation->accepted) {
        m_operationTimer.stop();
        if (m_operation->withdraw) {
            std::exchange(m_operation->withdraw, {})();
        }
        const std::string id = m_operation->request.id;
        const QString summary = operationSummary(m_operation->question);
        m_operation.reset();
        denyOperation(id, ErrorCode::InternalBusy,
                      trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: denied (someone joined)."))
                          .arg(summary));
    }
    emit participantsChanged();
    const std::uint64_t token = ++m_joinToken;
    m_joinTimer.start(m_options.limits.joinApprovalTimeout);
    ConsentProvider::Withdraw withdraw =
        m_consent->askJoin(JoinQuestion{.code = m_code}, [this, token](bool allowed) {
            if (token == m_joinToken && m_joinPending) {
                answerJoin(allowed);
            }
        });
    // The answer may have come synchronously (the question is gone then).
    if (token == m_joinToken && m_joinPending) {
        m_joinWithdraw = std::move(withdraw);
    }
}

void RemoteHost::answerJoin(bool allowed) {
    m_joinTimer.stop();
    m_joinWithdraw = {};
    m_joinPending = false;
    if (m_droppedWhilePending > 0) {
        audit(trRemoteN(
            QT_TRANSLATE_N_NOOP("hwio::remote",
                                "%n remote command(s) were dropped while waiting for your answer."),
            static_cast<int>(m_droppedWhilePending)));
        m_droppedWhilePending = 0;
    }
    if (!allowed) {
        audit(trRemote(QT_TRANSLATE_NOOP(
            "hwio::remote", "You ended the session instead of allowing the participant.")));
        finish(makeError(ErrorCode::RemoteJoinNotAllowed, "the host ended the session"));
        return;
    }
    m_allowed = true;
    audit(m_peerApp.empty()
              ? trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Participant allowed."))
              : trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Participant allowed (%1)."))
                    .arg(toQString(m_peerApp)));
    sendSync();
    sendHello();
    m_sentTree.reset();
    sendCommands(); // ADR-0364
    if (m_peerApp.empty()) {
        m_helloTimer.start(m_options.limits.helloWait);
    }
    activity();
    emit participantsChanged();
}

void RemoteHost::sendSync() {
    QJsonObject sync;
    sync.insert(QStringLiteral("state"), m_deviceState);
    sync.insert(QStringLiteral("commands"), m_transcript.toJson());
    if (m_relay) {
        m_relay->emitEvent(QStringLiteral("sync"), sync, false);
    }
}

void RemoteHost::sendHello() {
    if (m_session.isNull()) {
        return;
    }
    const device::SessionIdentity identity = m_session->identity();
    tunnel::HostHello hello;
    hello.version = m_options.appVersion;
    hello.device.transport = std::string(device::transportId(identity.transport));
    hello.device.name = identity.transport == device::Transport::Ble
                            ? identity.displayName
                            : std::string(m_session->profile().displayName);
    if (const auto& adopted = m_session->adoptedIdentity()) {
        hello.device.serial = adopted->deviceSerial;
        hello.device.firmware =
            adopted->version.empty() ? adopted->name : adopted->name + " " + adopted->version;
    }
    hello.ops = m_operations != nullptr;
    if (m_operations != nullptr) {
        hello.opKinds = m_operations->kinds(); // ADR-0363
        // ADR-0368: support answers the questions of the LTE update.
        hello.opQuestions =
            std::ranges::find(hello.opKinds, tunnel::OpKind::LteUpdate) != hello.opKinds.end();
    }
    sendHost(hello);
    m_helloSent = true;
}

void RemoteHost::sendHost(const tunnel::HostMessage& message) {
    if (m_relay) {
        m_relay->emitEvent(QStringLiteral("at"), tunnel::encodeHost(message), false);
    }
}

void RemoteHost::sendAt(const QString& event, const QJsonValue& data, bool droppable) {
    if (!m_relay || m_state == HostState::Ended) {
        return;
    }
    QJsonObject at;
    at.insert(QStringLiteral("event"), event);
    at.insert(QStringLiteral("data"), data);
    m_relay->emitEvent(QStringLiteral("at"), at, droppable);
}

void RemoteHost::onWrite(const QJsonValue& payload) {
    if (m_state == HostState::Ended) {
        return;
    }
    if (payload.isObject()) {
        onTunnel(payload);
        return;
    }
    if (!payload.isString()) {
        emit warning(
            makeError(ErrorCode::RemoteProtocolError, "write that is neither text nor object"));
        return;
    }
    onRemoteLine(payload.toString());
}

void RemoteHost::onRemoteLine(const QString& text) {
    const std::string line = toStdString(text);
    if (!m_allowed || m_joinPending) {
        ++m_droppedWhilePending;
        qCInfo(log::lcRemote) << "Remote command dropped: no participant is allowed";
        return;
    }
    if (line.size() > m_options.limits.maxRemoteLineBytes || hasControlCharacters(line)) {
        audit(trRemote(QT_TRANSLATE_NOOP(
            "hwio::remote",
            "A remote command was dropped (too long or with control characters).")));
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    while (!m_recentLines.empty() && now - m_recentLines.front() >= std::chrono::seconds(1)) {
        m_recentLines.pop_front();
    }
    if (std::cmp_greater_equal(m_recentLines.size(), m_options.limits.remoteLinesPerSecond)) {
        if (now - m_lastRateAudit >= std::chrono::seconds(1)) {
            m_lastRateAudit = now;
            audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                             "Remote commands dropped: more than %1 per second."))
                      .arg(m_options.limits.remoteLinesPerSecond));
        }
        return;
    }
    m_recentLines.push_back(now);
    if (m_session.isNull()) {
        return;
    }
    audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "command %1")).arg(redactedCommand(line)));
    if (Status sent = m_session->sendRemoteLine(line); !sent) {
        emit warning(sent.error());
    }
    activity();
}

void RemoteHost::onTunnel(const QJsonValue& payload) {
    auto message = tunnel::decodeJoiner(payload);
    if (!message) {
        emit warning(message.error());
        return;
    }
    if (!m_allowed || m_joinPending) {
        qCInfo(log::lcRemote) << "Tunnel message dropped: no participant is allowed";
        if (const auto* request = std::get_if<tunnel::OpRequest>(&*message)) {
            denyOperation(
                request->id, ErrorCode::RemoteOperationDenied,
                trRemote(QT_TRANSLATE_NOOP(
                    "hwio::remote",
                    "An operation request was refused: the participant is not allowed.")));
        }
        return;
    }
    if (const auto* learn = std::get_if<tunnel::Learn>(&*message)) {
        onLearn(*learn);
        return;
    }
    if (const auto* answer = std::get_if<tunnel::OpAnswer>(&*message)) {
        onAnswer(*answer);
        return;
    }
    if (const auto* hello = std::get_if<tunnel::JoinerHello>(&*message)) {
        const std::string app = "HARDWARIO Monitor " + hello->version;
        if (app != m_peerApp) {
            m_peerApp = app;
            m_helloTimer.stop();
            audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Participant app: %1."))
                      .arg(toQString(app)));
            emit participantsChanged();
        }
        return;
    }
    onOperationRequest(std::get<tunnel::OpRequest>(*message));
}

void RemoteHost::setCommandTree(std::shared_ptr<const device::CommandTree> tree) {
    m_commandTree = std::move(tree);
    if (m_allowed && !m_joinPending && m_helloSent && !m_commandsTimer.isActive()) {
        m_commandsTimer.start(m_options.limits.commandsDelay);
    }
}

void RemoteHost::sendCommands() {
    if (!m_relay || m_state == HostState::Ended || !m_allowed || m_joinPending || !m_commandTree ||
        m_commandTree->empty() || m_commandTree == m_sentTree) {
        return;
    }
    m_sentTree = m_commandTree;
    // ADR-0365: a Monitor joiner accepts events up to joinerIncomingEventBytes, so a tree within
    // the joiner's tunnel bound (tunnel::maxCommandsBytes, which it checks on `data`) goes with its
    // help texts. Safety net (a bigger tree): without them, else not at all.
    constexpr std::size_t eventOverhead = 1024;
    for (const bool withHelp : {true, false}) {
        const QJsonObject at = tunnel::encodeCommands(*m_commandTree, withHelp);
        const qsizetype bytes = QJsonDocument(at).toJson(QJsonDocument::Compact).size();
        const qsizetype dataBytes = QJsonDocument(at.value(QStringLiteral("data")).toObject())
                                        .toJson(QJsonDocument::Compact)
                                        .size();
        if (std::cmp_less_equal(dataBytes, tunnel::maxCommandsBytes) &&
            std::cmp_less_equal(bytes + static_cast<qsizetype>(eventOverhead),
                                joinerIncomingEventBytes)) {
            if (!withHelp) {
                qCWarning(log::lcRemote)
                    << "Command tree sent without help texts: too large for the relay"
                    << m_commandTree->nodeCount();
            }
            m_relay->emitEvent(QStringLiteral("at"), at, false);
            return;
        }
    }
    qCWarning(log::lcRemote) << "Command tree not sent: too large for the relay"
                             << m_commandTree->nodeCount();
}

void RemoteHost::onLearn(const tunnel::Learn& learn) {
    // ADR-0364: a known command only, rate-limited per second and per session.
    if (!m_commandTree || m_commandTree->find(learn.path) == nullptr) {
        qCInfo(log::lcRemote) << "Learn request for an unknown command dropped";
        return;
    }
    if (m_learnCount >= m_options.limits.learnPerSession) {
        qCInfo(log::lcRemote) << "Learn request dropped: the session limit is reached";
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    while (!m_recentLearn.empty() && now - m_recentLearn.front() >= std::chrono::seconds(1)) {
        m_recentLearn.pop_front();
    }
    if (std::cmp_greater_equal(m_recentLearn.size(), m_options.limits.learnPerSecond)) {
        qCInfo(log::lcRemote) << "Learn request dropped: more than"
                              << m_options.limits.learnPerSecond << "per second";
        return;
    }
    m_recentLearn.push_back(now);
    ++m_learnCount;
    QStringList path;
    for (const std::string& word : learn.path) {
        path.append(toQString(word));
    }
    emit learnRequested(path);
}

void RemoteHost::denyOperation(const std::string& id, ErrorCode code, const QString& auditText) {
    audit(auditText);
    sendHost(tunnel::OpDenied{.id = id, .code = std::string(idOf(code))});
}

void RemoteHost::onOperationRequest(const tunnel::OpRequest& request) {
    OperationQuestion question{.kind = request.kind,
                               .target = request.target,
                               .firmwareId = request.firmwareId.value_or(""),
                               .firmwareName = {},
                               .firmwareVersion = {}};
    if (request.kind == tunnel::OpKind::LteUpdate && m_operations != nullptr) {
        // ADR-0363: the host's recommended LTE firmware.
        std::tie(question.firmwareName, question.firmwareVersion) =
            m_operations->lteUpdateFirmware();
    }
    const QString summary = operationSummary(question);
    audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Support asks: %1.")).arg(summary));
    if (m_operations == nullptr || !m_helloSent) {
        denyOperation(request.id, ErrorCode::RemoteOperationUnsupportedPeer,
                      trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                                 "%1: refused (this console has no operations)."))
                          .arg(summary));
        return;
    }
    if (const std::vector<tunnel::OpKind> kinds = m_operations->kinds();
        std::ranges::find(kinds, request.kind) == kinds.end()) {
        denyOperation(request.id, ErrorCode::RemoteOperationUnsupportedPeer,
                      trRemote(QT_TRANSLATE_NOOP("hwio::remote",
                                                 "%1: refused (not available on this console)."))
                          .arg(summary));
        return;
    }
    if (m_operation || m_joinPending) {
        denyOperation(request.id, ErrorCode::InternalBusy,
                      trRemote(QT_TRANSLATE_NOOP(
                                   "hwio::remote",
                                   "%1: refused (another request or operation is in progress)."))
                          .arg(summary));
        return;
    }
    if (Status valid = m_operations->validate(request); !valid) {
        denyOperation(request.id, ErrorCode::RemoteProtocolError,
                      trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: refused (%2)."))
                          .arg(summary, toQString(valid.error().detail)));
        return;
    }
    m_operation = PendingOperation{
        .request = request, .question = question, .accepted = false, .withdraw = {}};
    const std::uint64_t token = ++m_operationToken;
    if (request.kind != tunnel::OpKind::Flash) {
        askOperation(std::move(*m_operation));
        return;
    }
    // The firmware must be in the catalog; its name and version go into the question.
    m_operations->lookup(question.firmwareId, [this, token](Result<firmware::CatalogInfo> info) {
        if (token != m_operationToken || !m_operation) {
            return;
        }
        if (!info) {
            const std::string id = m_operation->request.id;
            const QString text = operationSummary(m_operation->question);
            const ErrorCode code = info.error().code == ErrorCode::RemoteFirmwareNotInCatalog
                                       ? ErrorCode::RemoteFirmwareNotInCatalog
                                       : info.error().code;
            m_operation.reset();
            denyOperation(id, code,
                          trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: refused (%2)."))
                              .arg(text, toQString(info.error().message)));
            return;
        }
        m_operation->question.firmwareName = info->name;
        m_operation->question.firmwareVersion = info->version;
        askOperation(std::move(*m_operation));
    });
}

void RemoteHost::askOperation(PendingOperation pending) {
    m_operation = std::move(pending);
    const std::uint64_t token = ++m_operationToken;
    m_operationTimer.start(m_options.limits.operationTimeout);
    const OperationQuestion question = m_operation->question;
    ConsentProvider::Withdraw withdraw =
        m_consent->askOperation(question, [this, token](bool allowed) {
            if (token != m_operationToken || !m_operation || m_operation->accepted) {
                return;
            }
            m_operationTimer.stop();
            m_operation->withdraw = {};
            if (!allowed) {
                const std::string id = m_operation->request.id;
                const QString summary = operationSummary(m_operation->question);
                m_operation.reset();
                denyOperation(
                    id, ErrorCode::RemoteOperationDenied,
                    trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: denied by you.")).arg(summary));
                return;
            }
            startOperation();
        });
    // The answer may have come synchronously (the question is gone then).
    if (token == m_operationToken && m_operation && !m_operation->accepted) {
        m_operation->withdraw = std::move(withdraw);
    }
}

void RemoteHost::startOperation() {
    if (!m_operation) {
        return;
    }
    PendingOperation& operation = *m_operation;
    operation.accepted = true;
    const QString summary = operationSummary(operation.question);
    const std::string id = operation.request.id;
    audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: allowed by you.")).arg(summary));
    sendHost(tunnel::OpAccepted{.id = id});
    emit operationStarted(summary);
    m_lastProgress = {};
    m_lastProgressPhase.clear();
    const std::uint64_t token = m_operationToken;
    Status started = m_operations->start(
        operation.request,
        [this, token](const OperationUpdate& update) {
            if (token != m_operationToken || !m_operation) {
                return;
            }
            activity();
            emit operationProgress(update);
            const auto now = std::chrono::steady_clock::now();
            // A new phase is sent at once (the guided LTE update's steps, ADR-0363).
            if (now - m_lastProgress < m_options.limits.progressInterval &&
                update.phase == m_lastProgressPhase) {
                return;
            }
            m_lastProgress = now;
            m_lastProgressPhase = update.phase;
            sendHost(tunnel::OpProgress{.id = m_operation->request.id,
                                        .phase = update.phase,
                                        .step = update.step,
                                        .steps = update.steps,
                                        .done = update.done,
                                        .total = update.total});
        },
        [this, token](Status status) {
            if (token == m_operationToken && m_operation) {
                finishOperation(status);
            }
        },
        // ADR-0368: only a joiner that answers, and only for the LTE update.
        operation.request.questions && operation.request.kind == tunnel::OpKind::LteUpdate
            ? AskSupport(
                  [this, token](tunnel::OpQuestionKind kind, std::function<void(bool)> answer) {
                      askSupport(token, kind, std::move(answer));
                  })
            : AskSupport());
    if (!started) {
        finishOperation(started);
    }
}

void RemoteHost::askSupport(std::uint64_t token, tunnel::OpQuestionKind kind,
                            std::function<void(bool)> answer) {
    if (token != m_operationToken || !m_operation || !m_operation->accepted ||
        m_state == HostState::Ended) {
        // The operation is over: no (asked on the next turn, never inside the caller).
        QTimer::singleShot(0, this, [answer = std::move(answer)] { answer(false); });
        return;
    }
    const QString summary = operationSummary(m_operation->question);
    const std::string operationId = m_operation->request.id;
    // One question at a time: the earlier one is answered no.
    if (auto earlier = takeQuestion()) {
        earlier(false);
    }
    const std::string qid = std::format("q-{:08x}{:08x}", QRandomGenerator::system()->generate(),
                                        QRandomGenerator::system()->generate());
    m_question = SupportQuestion{.qid = qid, .kind = kind, .answer = std::move(answer)};
    m_questionTimer.start(m_options.limits.operationTimeout);
    audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: waiting for support's answer: %2"))
              .arg(summary, operationQuestionKindText(kind)));
    sendHost(tunnel::OpQuestion{.id = operationId, .qid = qid, .question = kind});
}

std::function<void(bool)> RemoteHost::takeQuestion() {
    m_questionTimer.stop();
    std::function<void(bool)> answer;
    if (m_question) {
        answer = std::move(m_question->answer);
        m_question.reset();
    }
    return answer;
}

void RemoteHost::onAnswer(const tunnel::OpAnswer& answer) {
    // ADR-0368: an allowed participant (checked by onTunnel), the open question of the running
    // operation, once.
    if (!m_question || !m_operation || !m_operation->accepted ||
        answer.id != m_operation->request.id || answer.qid != m_question->qid) {
        qCInfo(log::lcRemote) << "Operation answer dropped: no such open question";
        return;
    }
    answerSupportQuestion(answer.yes);
}

void RemoteHost::answerSupportQuestion(bool yes) {
    if (!m_question) {
        return;
    }
    m_questionTimer.stop();
    SupportQuestion question = std::move(*m_question);
    m_question.reset();
    activity();
    audit(yes ? trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Support answered: continue."))
              : cancelledBySupportText());
    if (question.answer) {
        question.answer(yes);
    }
}

void RemoteHost::finishOperation(const Status& status) {
    if (!m_operation) {
        return;
    }
    const QString summary = operationSummary(m_operation->question);
    const std::string id = m_operation->request.id;
    m_operation.reset();
    m_questionTimer.stop();
    m_question.reset(); // ADR-0368: the operation ended; its question is gone
    activity();
    if (status) {
        audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: done.")).arg(summary));
        sendHost(tunnel::OpResult{.id = id, .ok = true, .code = std::nullopt});
        emit operationFinished(
            true, trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: done.")).arg(summary));
        return;
    }
    audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "%1: failed: %2"))
              .arg(summary, toQString(status.error().message)));
    sendHost(
        tunnel::OpResult{.id = id, .ok = false, .code = std::string(idOf(status.error().code))});
    emit operationFinished(false, toQString(status.error().message));
}

void RemoteHost::onLines(const QList<device::ConsoleLine>& lines) {
    bool any = false;
    for (const device::ConsoleLine& line : lines) {
        if (line.dir == device::Direction::Marker) {
            continue;
        }
        any = true;
        if (line.dir == device::Direction::In) {
            const QString text = redactedCommand(line.text);
            m_transcript.addCommand(text, line.time);
            sendAt(QStringLiteral("write"), text, false);
        } else {
            const QString text = redactedOutput(line.text);
            m_transcript.addOutput(text, line.time);
            sendAt(QStringLiteral("read"), text, true);
        }
    }
    if (any) {
        activity();
    }
}

void RemoteHost::onConnection(const device::ConnectionEvent& event) {
    const QString state = stateOf(event.state);
    if (state == m_deviceState) {
        return;
    }
    m_deviceState = state;
    sendAt(QStringLiteral("state"), state, false);
}

void RemoteHost::activity() {
    if (m_state != HostState::Sharing) {
        return;
    }
    const auto warnAfter = m_options.limits.idleTimeout - m_options.limits.idleWarningBefore;
    m_idleWarningTimer.start(std::max(warnAfter, std::chrono::milliseconds(0)));
    m_idleTimer.start(m_options.limits.idleTimeout);
}

void RemoteHost::keepSharing() { activity(); }

void RemoteHost::audit(const QString& text) {
    qCInfo(log::lcRemote).noquote() << "Audit:" << text;
    if (m_session.isNull()) {
        return;
    }
    const QString line = trRemote(QT_TRANSLATE_NOOP("hwio::remote", "— Remote: %1 —")).arg(text);
    if (Status recorded = m_session->recordRemoteEvent(toStdString(line)); !recorded) {
        qCWarning(log::lcRemote).noquote()
            << "Audit line not recorded:" << toQString(recorded.error().detail);
    }
}

QString RemoteHost::redactedCommand(const std::string& line) const {
    if (!m_session.isNull()) {
        const auto prefixes =
            device::sensitivePrefixesOf(m_session->profile().id, m_session->consoleTargetId());
        if (const auto prefix = device::matchSensitiveCommand(line, prefixes)) {
            return toQString(std::string(*prefix) + " <redacted>");
        }
    }
    return toQString(line);
}

QString RemoteHost::redactedOutput(const std::string& line) const {
    if (!m_session.isNull()) {
        const auto keys =
            device::sensitiveOutputKeysOf(m_session->profile().id, m_session->consoleTargetId());
        if (const auto key = device::matchSensitiveOutput(line, keys)) {
            return toQString(*key + ": <redacted>");
        }
    }
    return toQString(line);
}

void RemoteHost::end() {
    if (m_state == HostState::Ended) {
        return;
    }
    audit(trRemote(QT_TRANSLATE_NOOP("hwio::remote", "You ended sharing.")));
    finish(std::nullopt);
}

void RemoteHost::finish(std::optional<Error> error, bool notify) {
    if (m_state == HostState::Ended) {
        return;
    }
    m_state = HostState::Ended;
    m_endError = std::move(error);
    for (QTimer* timer :
         {&m_collisionTimer, &m_joinTimer, &m_operationTimer, &m_idleWarningTimer, &m_idleTimer,
          &m_lifetimeTimer, &m_helloTimer, &m_commandsTimer, &m_questionTimer}) {
        timer->stop();
    }
    // ADR-0368: an open question to support is answered no (nothing was changed yet).
    if (m_question) {
        audit(trRemote(
            QT_TRANSLATE_NOOP("hwio::remote", "Support did not answer before the session ended.")));
        if (auto answer = takeQuestion()) {
            answer(false);
        }
    }
    ++m_joinToken;
    ++m_operationToken;
    if (m_joinWithdraw) {
        std::exchange(m_joinWithdraw, {})();
    }
    if (m_operation && m_operation->withdraw) {
        std::exchange(m_operation->withdraw, {})();
    }
    m_operation.reset();
    if (m_operations != nullptr) {
        m_operations->abandon();
    }
    if (m_relay) {
        if (m_helloSent && m_relay->isConnected()) {
            // ADR-0354: tell the joiner that the session ended on purpose.
            sendHost(tunnel::HostEnd{.code = m_endError ? std::string(idOf(m_endError->code))
                                                        : std::string("cancelled")});
        }
        m_relay->stop();
    }
    if (m_learnCount > 0) {
        audit(
            trRemoteN(QT_TRANSLATE_N_NOOP("hwio::remote",
                                          "Help of %n command(s) read for support's autocomplete."),
                      m_learnCount));
    }
    if (m_everShared && !m_session.isNull()) {
        const QString reason = m_endError ? toQString(m_endError->message) : QString();
        audit(reason.isEmpty()
                  ? trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Sharing ended."))
                  : trRemote(QT_TRANSLATE_NOOP("hwio::remote", "Sharing ended: %1")).arg(reason));
    }
    if (notify) {
        emit participantsChanged();
        emit ended();
    }
}

} // namespace hwio::remote
