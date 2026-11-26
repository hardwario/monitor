#include "viewmodels/remote_join_view_model.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/remote/consent.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/remote_operation_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "mcp/gui_remote_device.h"
#include "viewmodels/device_tab_view_model.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTime>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::gui {

namespace tunnel = remote::tunnel;
namespace tools = hwio::mcp::tools;

namespace {

tools::RemoteOperationProgress runningProgress(const tunnel::OpProgress* progress) {
    tools::RemoteOperationProgress out{.stage = tools::RemoteOperationProgress::Stage::Running};
    if (progress != nullptr) {
        out.phase = progress->phase;
        out.step = progress->step;
        out.steps = progress->steps;
        out.done = progress->done;
        out.total = progress->total;
    }
    return out;
}

} // namespace

RemoteJoinViewModel::RemoteJoinViewModel(DeviceTabViewModel* tab) : QObject(tab), m_tab(tab) {
    connect(m_tab, &DeviceTabViewModel::sessionChanged, this, &RemoteJoinViewModel::attach);
    connect(m_tab, &DeviceTabViewModel::transportChanged, this, &RemoteJoinViewModel::changed);
    // ADR-0369: a success does not linger once support leaves the Flash page.
    connect(m_tab, &DeviceTabViewModel::currentPageChanged, this, [this] {
        if (m_tab->currentPage() != QStringLiteral("flash") && !m_failed && !operationPending()) {
            clearResult();
        }
    });
}

void RemoteJoinViewModel::onFinished(bool ok, const Error& error) {
    // ADR-0369: named, with the local time, also in the console (kept in its log and file).
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    // Support's own No (ADR-0368): a cancellation, not an error.
    const bool declined = !ok && error.code == ErrorCode::Cancelled && m_client->declined();
    m_failed = !ok && !declined;
    m_progress = -1.0;
    if (ok) {
        m_status = remote::operationResultText(m_kind, m_target, time);
    } else if (declined) {
        m_status = remote::lteUpdateDeclinedText(time);
    } else {
        m_status = remote::operationFailedText(m_kind, m_target, time, toQString(error.message));
    }
    m_tab->addStatusLine(m_status);
    qCInfo(lcGui) << "Remote operation result:"
                  << (ok ? QStringLiteral("ok") : toQString(idOf(error.code)));
    m_lastProgress.reset();
    operationChanged(); // signal
    // Plan 0062: the AI client waiting for it (D12: not after Stop waiting). The error is built
    // fresh from its id (ADR-0434: nothing of the session in an MCP result).
    if (m_aiOperation) {
        AiOperation operation = std::move(*m_aiOperation);
        m_aiOperation.reset();
        if (operation.callbacks.done) {
            operation.callbacks.done(
                ok ? Status{} : Status(std::unexpected(makeError(error.code, "remote operation"))));
        }
    }
}

void RemoteJoinViewModel::clearResult() {
    if (m_status.isEmpty() && !m_failed) {
        return;
    }
    m_status.clear();
    m_failed = false;
    operationChanged(); // signal
}

bool RemoteJoinViewModel::resultDismissable() const { return m_failed && !operationPending(); }

void RemoteJoinViewModel::dismissResult() {
    if (resultDismissable()) {
        clearResult();
    }
}

RemoteJoinViewModel::~RemoteJoinViewModel() { releaseDevice(); }

void RemoteJoinViewModel::releaseDevice() {
    if (!m_device) {
        return;
    }
    // ADR-0436: the grants end with the session (running calls end `cancelled`).
    m_device->revokeAi({});
    m_device.reset();
    aiChanged(); // signal
}

void RemoteJoinViewModel::attach() {
    releaseDevice();
    m_client.reset();
    m_aiOperation.reset();
    m_aiText.clear();
    m_aiCancel = nullptr;
    m_lastProgress.reset();
    m_idle = false;
    m_status.clear();
    m_failed = false;
    m_progress = -1.0;
    if (remote::RemoteConsoleSession* session = m_tab->remoteSession()) {
        m_left = false; // Join again (ADR-0429)
        connect(session, &remote::RemoteConsoleSession::joinStateChanged, this,
                &RemoteJoinViewModel::changed);
        connect(session, &remote::RemoteConsoleSession::hostHelloReceived, this,
                &RemoteJoinViewModel::changed);
        connect(session, &remote::RemoteConsoleSession::linesReceived, this, [this] {
            if (m_idle) {
                m_idle = false;
                changed(); // signal
            }
        });
        connect(session, &remote::RemoteConsoleSession::idleNotice, this, [this] {
            m_idle = true;
            changed(); // signal
        });
        m_client = std::make_unique<remote::RemoteOperationClient>(*session);
        m_client->setAnswersQuestions(true); // ADR-0368: support answers on the Flash card
        connect(m_client.get(), &remote::RemoteOperationClient::question, this, [this] {
            operationChanged(); // signal
            // D13 (ADR-0438): the person answers here; the AI client sees it as progress.
            tools::RemoteOperationProgress waiting =
                runningProgress(m_lastProgress ? &*m_lastProgress : nullptr);
            waiting.stage = tools::RemoteOperationProgress::Stage::WaitingForSupport;
            aiProgress(waiting);
        });
        connect(m_client.get(), &remote::RemoteOperationClient::questionClosed, this, [this] {
            operationChanged(); // signal
            if (m_client && m_client->isAccepted()) {
                aiProgress(runningProgress(m_lastProgress ? &*m_lastProgress : nullptr));
            }
        });
        connect(m_client.get(), &remote::RemoteOperationClient::accepted, this, [this] {
            m_status = tr("The customer allowed it. Running on the customer's side…");
            operationChanged(); // signal
            aiProgress(runningProgress(nullptr));
        });
        connect(m_client.get(), &remote::RemoteOperationClient::progress, this,
                [this](const tunnel::OpProgress& progress) {
                    m_progress = progress.total > 0 ? static_cast<double>(progress.done) /
                                                          static_cast<double>(progress.total)
                                                    : -1.0;
                    m_status = tr("Running on the customer's side: %1")
                                   .arg(remote::operationPhaseText(progress.phase));
                    m_lastProgress = progress;
                    operationChanged(); // signal
                    aiProgress(runningProgress(&progress));
                });
        connect(m_client.get(), &remote::RemoteOperationClient::finished, this,
                &RemoteJoinViewModel::onFinished);
        // Plan 0062: the AI clients' view of this session (Qt parent: this; owned by m_device).
        m_device = std::make_unique<GuiRemoteDevice>(*session, *this);
        connect(m_device.get(), &GuiRemoteDevice::aiClientsChanged, this,
                &RemoteJoinViewModel::aiChanged);
    }
    changed();          // signal
    operationChanged(); // signal
    aiChanged();        // signal
}

bool RemoteJoinViewModel::active() const { return m_tab->isRemote(); }

void RemoteJoinViewModel::setLeft(bool left) {
    if (m_left == left) {
        return;
    }
    m_left = left;
    changed(); // signal
}

QString RemoteJoinViewModel::state() const {
    const remote::RemoteConsoleSession* session = m_tab->remoteSession();
    if (m_tab->isRemote() && session == nullptr && m_left) {
        return QStringLiteral("ended"); // ADR-0429: support left the session
    }
    if (!m_tab->isRemote() || session == nullptr) {
        return QStringLiteral("connecting");
    }
    switch (session->joinState()) {
    case remote::JoinState::Connecting:
        return QStringLiteral("connecting");
    case remote::JoinState::Waiting:
        return QStringLiteral("waiting");
    case remote::JoinState::Allowed:
        return m_idle ? QStringLiteral("idle") : QStringLiteral("joined");
    case remote::JoinState::Ended:
        return QStringLiteral("ended");
    }
    return QStringLiteral("connecting");
}

QString RemoteJoinViewModel::bannerText() const {
    const QString current = state();
    if (current == QStringLiteral("connecting")) {
        return tr("Connecting to the support server…");
    }
    if (current == QStringLiteral("waiting")) {
        return tr("Waiting for the customer to allow you… The live output may already appear.");
    }
    if (current == QStringLiteral("idle")) {
        return tr("No activity from the customer for 30 min. The session may have ended.");
    }
    if (current == QStringLiteral("ended") && m_left) {
        return tr("You left the session. The console stays here so that you can save it.");
    }
    if (current == QStringLiteral("ended")) {
        return tr("The session ended. The console stays here so that you can save it.");
    }
    return {};
}

bool RemoteJoinViewModel::operationsAvailable() const {
    const remote::RemoteConsoleSession* session = m_tab->remoteSession();
    return session != nullptr && session->joinState() == remote::JoinState::Allowed &&
           session->hostHello() &&
           remote::tunnel::offersOperations(*session->hostHello()); // plan 0059
}

QString RemoteJoinViewModel::operationsUnavailableText() const {
    const remote::RemoteConsoleSession* session = m_tab->remoteSession();
    if (session == nullptr || session->joinState() != remote::JoinState::Allowed) {
        return tr("Operations are available once the customer has allowed you.");
    }
    if (!session->hostHello()) {
        return tr("The customer uses the web Terminal, which supports only the console. Ask them "
                  "to use HARDWARIO Monitor for flashing.");
    }
    return tr("The customer's console (Bluetooth) has no flashing.");
}

bool RemoteJoinViewModel::lteUpdateAvailable() const {
    const remote::RemoteConsoleSession* session = m_tab->remoteSession();
    if (!operationsAvailable() || session == nullptr) {
        return false;
    }
    const std::optional<tunnel::HostHello>& hello = session->hostHello();
    return hello.has_value() && tunnel::supports(*hello, tunnel::OpKind::LteUpdate);
}

QVariantList RemoteJoinViewModel::targets() const {
    const remote::RemoteConsoleSession* session = m_tab->remoteSession();
    const std::optional<tunnel::HostHello> hello =
        session != nullptr ? session->hostHello() : std::nullopt;
    const device::DeviceProfile& profile = remote::RemoteConsoleSession::profileFor(hello);
    const auto name = [&profile](std::string_view id) {
        if (profile.id == "chester" && id == "app") {
            return tr("Application core");
        }
        if (profile.id == "chester" && id == "lte") {
            return tr("LTE modem core");
        }
        const device::TargetProfile* target = device::findTarget(profile, id);
        return target != nullptr ? device::targetDisplayName(*target) : toQString(id);
    };
    const auto entry = [&name](std::string_view id, bool catalog) {
        return QVariantMap{{QStringLiteral("id"), toQString(id)},
                           {QStringLiteral("name"), name(id)},
                           {QStringLiteral("catalog"), catalog}};
    };
    QVariantList out;
    if (!hello || !hello->targets) {
        out.append(entry("app", true));
        out.append(entry("lte", false));
        return out;
    }
    for (const tunnel::HostTarget& target : *hello->targets) {
        out.append(entry(target.id, target.catalog));
    }
    return out;
}

bool RemoteJoinViewModel::operationPending() const { return m_client && m_client->busy(); }

QString RemoteJoinViewModel::operationStatus() const {
    if (!m_aiText.isEmpty() && operationPending()) {
        return m_status.isEmpty() ? m_aiText : QStringLiteral("%1 %2").arg(m_aiText, m_status);
    }
    return m_status;
}

QString RemoteJoinViewModel::operationQuestion() const {
    if (!m_client) {
        return {};
    }
    const auto question = m_client->openQuestion();
    return question ? remote::operationQuestionKindText(*question) : QString();
}

void RemoteJoinViewModel::answerQuestion(bool yes) {
    if (!m_client) {
        return;
    }
    if (Status sent = m_client->answer(yes); !sent) {
        qCWarning(lcGui).noquote() << "Remote answer not sent:" << toQString(sent.error().detail);
        return;
    }
    qCInfo(lcGui) << "Remote question answered:" << (yes ? "continue" : "cancel");
}

bool RemoteJoinViewModel::isFirmwareId(const QString& text) {
    return firmware::FirmwareId::parse(toStdString(text.trimmed())).has_value();
}

void RemoteJoinViewModel::requestOperation(const QString& kind, const QString& target,
                                           const QString& firmwareId) {
    if (!m_client) {
        return;
    }
    const auto opKind = tunnel::kindFromId(toStdString(kind));
    const auto opTarget = opKind == tunnel::OpKind::LteUpdate
                              ? std::optional(tunnel::OpTarget::Lte)
                              : tunnel::targetFromId(toStdString(target));
    if (!opKind || !opTarget) {
        return;
    }
    std::optional<std::string> id;
    if (*opKind == tunnel::OpKind::Flash) {
        auto parsed = firmware::FirmwareId::parse(toStdString(firmwareId.trimmed()));
        if (!parsed) {
            m_failed = true;
            m_status =
                tr("The firmware ID must be 32 hexadecimal digits from the HARDWARIO catalog.");
            operationChanged(); // signal
            return;
        }
        id = parsed->value();
    }
    m_kind = *opKind;
    m_target = *opTarget;
    auto requested = m_client->request(*opKind, *opTarget, id);
    m_failed = !requested;
    m_progress = -1.0;
    m_status = requested ? tr("Waiting for the customer's confirmation…")
                         : toQString(requested.error().message);
    qCInfo(lcGui) << "Remote operation requested:" << kind << target;
    operationChanged(); // signal
}

// ---- plan 0062: AI clients ---------------------------------------------------------------------

Result<tools::RemoteOperationId>
RemoteJoinViewModel::requestAiOperation(const tools::RemoteOperationRequest& request,
                                        tools::RemoteOperationCallbacks callbacks) {
    if (!m_client) {
        return std::unexpected(makeError(ErrorCode::RemoteHostEnded, "no session"));
    }
    tunnel::OpKind kind = tunnel::OpKind::Reset;
    std::optional<tunnel::OpTarget> target = tunnel::targetFromId(request.target);
    switch (request.kind) {
    case tools::RemoteOperationKind::Flash:
        kind = tunnel::OpKind::Flash;
        break;
    case tools::RemoteOperationKind::Reset:
        kind = tunnel::OpKind::Reset;
        break;
    case tools::RemoteOperationKind::LteUpdate:
        kind = tunnel::OpKind::LteUpdate;
        target = tunnel::OpTarget::Lte;
        break;
    }
    if (!target) {
        return std::unexpected(makeError(ErrorCode::RemoteTargetUnsupported, "target"));
    }
    // ADR-0368: lteUpdate carries `questions` (setAnswersQuestions): the person answers here.
    auto requested = m_client->request(kind, *target, request.firmwareId, request.prepared);
    if (!requested) {
        return std::unexpected(std::move(requested).error());
    }
    m_kind = kind;
    m_target = *target;
    m_failed = false;
    m_progress = -1.0;
    m_lastProgress.reset();
    m_status = tr("Waiting for the customer's confirmation…");
    const tools::RemoteOperationId id = m_nextAiOperation++;
    m_aiOperation = AiOperation{.id = id, .callbacks = std::move(callbacks)};
    qCInfo(lcGui) << "Remote operation requested by an AI client:"
                  << toQString(tunnel::kindId(kind)) << toQString(target->id());
    operationChanged(); // signal
    aiProgress(tools::RemoteOperationProgress{
        .stage = tools::RemoteOperationProgress::Stage::WaitingForCustomer});
    return id;
}

void RemoteJoinViewModel::detachAiOperation(tools::RemoteOperationId id) {
    if (m_aiOperation && m_aiOperation->id == id) {
        m_aiOperation.reset();
        qCInfo(lcGui) << "Remote operation: the AI client stopped waiting";
    }
}

void RemoteJoinViewModel::aiProgress(const tools::RemoteOperationProgress& progress) {
    if (m_aiOperation && m_aiOperation->callbacks.progress) {
        // A copy: the callback may end the AI call (and detach it).
        const auto callback = m_aiOperation->callbacks.progress;
        callback(progress);
    }
}

Status RemoteJoinViewModel::beginAiBusy(QString text, std::function<void()> cancel) {
    if (operationPending() || aiWaiting()) {
        return std::unexpected(
            makeError(ErrorCode::InternalBusy, "a remote operation runs in this tab"));
    }
    m_aiText = std::move(text);
    m_aiCancel = cancel ? std::move(cancel) : std::function<void()>([] {});
    operationChanged(); // signal
    return {};
}

void RemoteJoinViewModel::endAiBusy() {
    if (!m_aiCancel && m_aiText.isEmpty()) {
        return;
    }
    m_aiText.clear();
    m_aiCancel = nullptr;
    operationChanged(); // signal
}

void RemoteJoinViewModel::stopWaiting() {
    if (!m_aiCancel) {
        return;
    }
    qCInfo(lcGui) << "Remote operation: support stopped the AI client's waiting";
    // A copy: the callback ends the AI call, which calls endAiBusy().
    const std::function<void()> cancel = m_aiCancel;
    cancel();
}

QString RemoteJoinViewModel::aiClientText() const {
    const QStringList clients = m_device ? m_device->aiClients() : QStringList();
    return clients.isEmpty() ? QString()
                             : tr("AI client active: %1").arg(clients.join(QStringLiteral(", ")));
}

void RemoteJoinViewModel::stopAi() {
    if (m_device) {
        qCInfo(lcGui) << "MCP: support stopped the AI clients of a remote session";
        m_device->revokeAi({});
    }
}

QVariantMap RemoteJoinViewModel::checkJoinInput(const QString& text, const QUrl& configured) {
    auto target = remote::parseJoinInput(toStdString(text));
    if (!target) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), toQString(target.error().message)}};
    }
    QString foreign;
    if (target->relay && !remote::sameRelay(*target->relay, configured)) {
        foreign = target->relay->toString();
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("code"), toQString(target->code.grouped())},
            {QStringLiteral("codeValue"), toQString(target->code.text())},
            {QStringLiteral("foreignRelay"), foreign},
            {QStringLiteral("error"), QString()}};
}

} // namespace hwio::gui
