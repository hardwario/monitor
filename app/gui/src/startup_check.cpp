#include "startup_check.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/diag/check.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/error_info.h"
#include "wizard/setup_wizard.h"
#include "wizard/wizard_registry.h"
#include "wizard/wizard_view_model.h"
#include "workers/diag_worker.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <optional>
#include <utility>
#include <vector>

namespace hwio::gui {

StartupCheck::Decision StartupCheck::decide(bool completed, bool jlinkOk, bool showAtStart) {
    if (!completed) {
        return Decision::Welcome;
    }
    if (!jlinkOk && showAtStart) {
        return Decision::JLinkNeeded;
    }
    return Decision::None;
}

StartupCheck::StartupCheck(AppViewModel& app, QObject* parent) : QObject(parent), m_app(app) {
    connect(&m_app.diagWorker(), &DiagWorker::checksFinished, this, &StartupCheck::onFinished);
}

void StartupCheck::run() {
    if (!m_app.wizardRegistry().has(WizardId::FirstRunSetup) || m_app.settingsStore()->readOnly()) {
        m_decision = Decision::None;
        emit decided();
        return;
    }
    m_generation = m_app.diagWorker().runChecks(
        DiagWorker::ChecksRequest{.context = m_app.diagEnvironment(), .ids = {"jlink-software"}});
}

void StartupCheck::onFinished(quint64 generation, const std::vector<diag::CheckResult>& results) {
    if (generation != m_generation || m_generation == 0) {
        return;
    }
    m_generation = 0;
    m_jlinkOk = false;
    for (const diag::CheckResult& result : results) {
        if (result.id == "jlink-software") {
            m_jlinkOk = result.status == diag::CheckStatus::Ok;
        }
    }
    openWizard();
}

void StartupCheck::openWizard() {
    SettingsStore& settings = *m_app.settingsStore();
    const Decision decision =
        decide(!settings.value(QStringLiteral("setup/completedVersion")).toString().isEmpty(),
               m_jlinkOk, settings.value(QStringLiteral("setup/showAtStart")).toBool());
    WizardViewModel& wizard = *m_app.wizard();
    if (decision != Decision::None && wizard.isOpen()) {
        if (!m_waiting) {
            // 0035 § C: once more when that wizard closes.
            m_waiting = true;
            m_waitConnection = connect(
                &wizard, &WizardViewModel::openChanged, this,
                [this] {
                    if (!m_app.wizard()->isOpen()) {
                        disconnect(m_waitConnection);
                        openWizard();
                    }
                },
                Qt::QueuedConnection);
            return;
        }
        m_decision = Decision::None; // still busy at the second try: not this start
        emit decided();
        return;
    }
    m_decision = decision;
    qCInfo(lcGui) << "Start-up check: J-Link Software" << (m_jlinkOk ? "ok" : "not ok")
                  << "- setup wizard" << (decision == Decision::None ? "not opened" : "opened");
    if (decision != Decision::None) {
        SetupWizard::Options options;
        options.mode = decision == Decision::JLinkNeeded ? SetupWizard::Mode::JLinkNeeded
                                                         : SetupWizard::Mode::Normal;
        // Qt parent ownership: WizardViewModel::start reparents it (or deletes it when refused).
        auto* setup =
            new SetupWizard(m_app, std::move(options), // NOLINT(cppcoreguidelines-owning-memory)
                            &wizard);
        (void)wizard.start(setup, ErrorInfo(), nullptr);
    }
    emit decided();
}

} // namespace hwio::gui
