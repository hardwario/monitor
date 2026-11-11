#include "viewmodels/remote_share_view_model.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/version.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/remote/consent.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/remote_host.h"
#include "hwio/remote/session_code.h"
#include "settings_store.h"
#include "viewmodels/device_tab_view_model.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QtCore/qnamespace.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hwio::gui {

RemoteShareViewModel::RemoteShareViewModel(DeviceTabViewModel* tab) : QObject(tab), m_tab(tab) {
    // Disconnect, another device, tab close, quit: sharing ends before the session goes away.
    connect(m_tab, &DeviceTabViewModel::sessionChanged, this, [this] {
        if (m_host) {
            stopSharing(true);
        }
        changed(); // signal (available)
    });
    connect(m_tab, &DeviceTabViewModel::stateChanged, this, &RemoteShareViewModel::changed);
}

RemoteShareViewModel::~RemoteShareViewModel() { stopSharing(false); }

bool RemoteShareViewModel::available() const {
    const device::ConsoleSession* session = m_tab->consoleSession();
    return session != nullptr && !m_tab->isRemote() &&
           session->connection().state == device::ConnectionState::Connected;
}

QString RemoteShareViewModel::state() const {
    if (!m_host) {
        return QStringLiteral("none");
    }
    if (m_host->state() != remote::HostState::Sharing) {
        return QStringLiteral("connecting");
    }
    return m_reconnecting ? QStringLiteral("reconnecting") : QStringLiteral("sharing");
}

QString RemoteShareViewModel::code() const {
    return m_host && m_host->state() == remote::HostState::Sharing
               ? toQString(m_host->code().grouped())
               : QString();
}

QString RemoteShareViewModel::link() const {
    return m_host && m_host->state() == remote::HostState::Sharing ? m_host->link().toString()
                                                                   : QString();
}

QString RemoteShareViewModel::relayHost() const {
    const SettingsStore* store = m_tab->settingsStore();
    return remote::configuredRelayUrl(store != nullptr ? store->settings() : nullptr).host();
}

QString RemoteShareViewModel::shareExplanation() const {
    return tr("Support will see this console and can send commands after you allow them. They "
              "can also ask to flash, erase, recover or reset the device; each request needs your "
              "confirmation.\n\nThe session passes through the HARDWARIO server %1.\n\nAnyone who "
              "knows the code sees the console output. Read the code only to HARDWARIO Support.\n\n"
              "Sharing ends after 30 minutes without activity or after 8 hours.")
        .arg(relayHost());
}

bool RemoteShareViewModel::participantAllowed() const {
    return m_host && m_host->commandsAllowed();
}

void RemoteShareViewModel::startSharing() {
    if (m_host || !available()) {
        return;
    }
    device::ConsoleSession* session = m_tab->consoleSession();
    m_endMessage.clear();
    if (device::DeviceSession* jlink = m_tab->session(); jlink != nullptr) {
        // ADR-0322: operations need the J-Link session (a Bluetooth console has none).
        const device::DeviceProfile& profile = m_tab->profile();
        m_operations = std::make_unique<remote::DeviceSessionOperations>(
            *jlink,
            firmware::CatalogOptions{.baseUrl =
                                         firmware::catalogBaseUrl(profile.firmwareCatalogUrl),
                                     .transferTimeout = std::chrono::seconds(60),
                                     .maxBodyBytes = 32ULL * 1024 * 1024,
                                     .lookupTimeout = std::chrono::seconds(15),
                                     .maxPageBytes = 1024ULL * 1024},
            m_tab->firmwareCache());
    }
    const SettingsStore* store = m_tab->settingsStore();
    m_host = std::make_unique<remote::RemoteHost>(
        *session, *this, m_operations.get(),
        remote::HostOptions{
            .relay = remote::configuredRelayUrl(store != nullptr ? store->settings() : nullptr),
            .limits = {},
            .appVersion = std::string(buildInfo().version),
            .code = std::nullopt});
    connect(m_host.get(), &remote::RemoteHost::sharingStarted, this,
            &RemoteShareViewModel::changed);
    connect(m_host.get(), &remote::RemoteHost::participantsChanged, this,
            &RemoteShareViewModel::changed);
    connect(m_host.get(), &remote::RemoteHost::connectionLost, this, [this] {
        m_reconnecting = true;
        changed(); // signal
    });
    connect(m_host.get(), &remote::RemoteHost::connectionRestored, this, [this] {
        m_reconnecting = false;
        changed(); // signal
    });
    connect(m_host.get(), &remote::RemoteHost::idleWarning, this, [this](std::chrono::seconds) {
        m_idleWarning = true;
        changed(); // signal
    });
    connect(m_host.get(), &remote::RemoteHost::operationStarted, this,
            [this](const QString& summary) {
                m_operationRunning = true;
                m_operationStatus = tr("%1…").arg(summary);
                changed(); // signal
            });
    connect(m_host.get(), &remote::RemoteHost::operationProgress, this,
            [this](const remote::OperationUpdate& update) {
                if (update.total > 0) {
                    const auto percent = (update.done * 100) / update.total;
                    m_operationStatus =
                        tr("Remote operation: %1 %2 %").arg(toQString(update.phase)).arg(percent);
                    changed(); // signal
                }
            });
    connect(m_host.get(), &remote::RemoteHost::operationFinished, this,
            [this](bool /*ok*/, const QString& message) {
                m_operationRunning = false;
                m_operationStatus.clear();
                m_tab->showToast(message);
                changed(); // signal
            });
    connect(m_host.get(), &remote::RemoteHost::ended, this, [this] {
        const std::optional<Error> error = m_host ? m_host->endError() : std::nullopt;
        m_endMessage = error ? toQString(error->message) : QString();
        if (error) {
            m_tab->showToast(tr("Sharing ended: %1").arg(m_endMessage));
        }
        // Not inside the host's own signal.
        QMetaObject::invokeMethod(this, [this] { stopSharing(true); }, Qt::QueuedConnection);
    });
    qCInfo(lcGui) << "Remote: start sharing";
    m_host->start();
    changed(); // signal
}

void RemoteShareViewModel::endSharing() {
    if (m_host) {
        m_host->end();
    }
}

void RemoteShareViewModel::stopSharing(bool notify) {
    std::unique_ptr<remote::RemoteHost> host = std::move(m_host);
    std::unique_ptr<remote::DeviceSessionOperations> operations = std::move(m_operations);
    if (host) {
        host->disconnect(this);
        host.reset(); // ends a running session (audit "Sharing ended")
    }
    operations.reset();
    m_joinAnswer = {};
    m_operationAnswer = {};
    m_operationRunning = false;
    m_operationStatus.clear();
    m_idleWarning = false;
    m_reconnecting = false;
    if (notify) {
        questionsChanged(); // signal
        changed();          // signal
    }
}

void RemoteShareViewModel::copyLink() {
    if (const QString text = link(); !text.isEmpty()) {
        m_tab->copyToClipboard(text, tr("Link copied to the clipboard."));
    }
}

void RemoteShareViewModel::copyCode() {
    if (m_host && m_host->state() == remote::HostState::Sharing) {
        m_tab->copyToClipboard(toQString(m_host->code().text()),
                               tr("Code copied to the clipboard."));
    }
}

void RemoteShareViewModel::keepSharing() {
    m_idleWarning = false;
    if (m_host) {
        m_host->keepSharing();
    }
    changed(); // signal
}

remote::ConsentProvider::Withdraw
RemoteShareViewModel::askJoin(const remote::JoinQuestion& question, Answer answer) {
    m_joinAnswer = std::move(answer);
    m_joinText = remote::joinQuestionText(question);
    questionsChanged(); // signal
    return [this] {
        m_joinAnswer = {};
        questionsChanged(); // signal
    };
}

remote::ConsentProvider::Withdraw
RemoteShareViewModel::askOperation(const remote::OperationQuestion& question, Answer answer) {
    m_operationAnswer = std::move(answer);
    m_operationText = remote::operationQuestionText(question);
    questionsChanged(); // signal
    return [this] {
        m_operationAnswer = {};
        questionsChanged(); // signal
    };
}

void RemoteShareViewModel::answerJoin(bool allowed) {
    Answer answer = std::exchange(m_joinAnswer, {});
    questionsChanged(); // signal
    if (answer) {
        answer(allowed);
    }
}

void RemoteShareViewModel::answerOperation(bool allowed) {
    Answer answer = std::exchange(m_operationAnswer, {});
    questionsChanged(); // signal
    if (answer) {
        answer(allowed);
    }
}

} // namespace hwio::gui
