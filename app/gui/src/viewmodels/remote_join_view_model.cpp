#include "viewmodels/remote_join_view_model.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/remote_operation_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "viewmodels/device_tab_view_model.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QVariantMap>
#include <QtContainerFwd>

#include <memory>
#include <optional>
#include <string>

namespace hwio::gui {

namespace tunnel = remote::tunnel;

RemoteJoinViewModel::RemoteJoinViewModel(DeviceTabViewModel* tab) : QObject(tab), m_tab(tab) {
    connect(m_tab, &DeviceTabViewModel::sessionChanged, this, &RemoteJoinViewModel::attach);
    connect(m_tab, &DeviceTabViewModel::transportChanged, this, &RemoteJoinViewModel::changed);
}

RemoteJoinViewModel::~RemoteJoinViewModel() = default;

void RemoteJoinViewModel::attach() {
    m_client.reset();
    m_idle = false;
    m_status.clear();
    m_failed = false;
    m_progress = -1.0;
    if (remote::RemoteConsoleSession* session = m_tab->remoteSession()) {
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
        connect(m_client.get(), &remote::RemoteOperationClient::accepted, this, [this] {
            m_status = tr("The customer allowed it. Running on the customer's side…");
            operationChanged(); // signal
        });
        connect(m_client.get(), &remote::RemoteOperationClient::progress, this,
                [this](const tunnel::OpProgress& progress) {
                    m_progress = progress.total > 0 ? static_cast<double>(progress.done) /
                                                          static_cast<double>(progress.total)
                                                    : -1.0;
                    m_status =
                        tr("Running on the customer's side: %1").arg(toQString(progress.phase));
                    operationChanged(); // signal
                });
        connect(m_client.get(), &remote::RemoteOperationClient::finished, this,
                [this](bool ok, const Error& error) {
                    m_failed = !ok;
                    m_progress = -1.0;
                    m_status = ok ? tr("Done (reported by the customer's Monitor).")
                                  : toQString(error.message);
                    operationChanged(); // signal
                });
    }
    changed();          // signal
    operationChanged(); // signal
}

bool RemoteJoinViewModel::active() const { return m_tab->isRemote(); }

QString RemoteJoinViewModel::state() const {
    const remote::RemoteConsoleSession* session = m_tab->remoteSession();
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
    if (current == QStringLiteral("ended")) {
        return tr("The session ended. The console stays here so that you can save it.");
    }
    return {};
}

bool RemoteJoinViewModel::operationsAvailable() const {
    const remote::RemoteConsoleSession* session = m_tab->remoteSession();
    return session != nullptr && session->joinState() == remote::JoinState::Allowed &&
           session->hostHello() && session->hostHello()->ops;
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

bool RemoteJoinViewModel::operationPending() const { return m_client && m_client->busy(); }

bool RemoteJoinViewModel::isFirmwareId(const QString& text) {
    return firmware::FirmwareId::parse(toStdString(text.trimmed())).has_value();
}

void RemoteJoinViewModel::requestOperation(const QString& kind, const QString& target,
                                           const QString& firmwareId) {
    if (!m_client) {
        return;
    }
    const auto opKind = tunnel::kindFromId(toStdString(kind));
    const auto opTarget = tunnel::targetFromId(toStdString(target));
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
    auto requested = m_client->request(*opKind, *opTarget, id);
    m_failed = !requested;
    m_progress = -1.0;
    m_status = requested ? tr("Waiting for the customer's confirmation…")
                         : toQString(requested.error().message);
    qCInfo(lcGui) << "Remote operation requested:" << kind << target;
    operationChanged(); // signal
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
