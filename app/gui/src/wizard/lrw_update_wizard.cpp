#include "wizard/lrw_update_wizard.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/lrw_update_procedure.h"
#include "hwio/device/operation.h"
#include "pages/firmware_services.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/flash_view_model.h"
#include "wizard/wizard.h"
#include "wizard/wizard_registry.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

using Step = device::LrwUpdateProcedure::Step;
using Action = device::LrwUpdateProcedure::Action;

// The ids of plannedSteps(), in order.
const std::array<QString, 6> stepIds{
    QStringLiteral("app-connector"),       QStringLiteral("stop-app"),
    QStringLiteral("lrw-connector"),       QStringLiteral("module"),
    QStringLiteral("app-connector-again"), QStringLiteral("done")};

const QString embeddedName = QStringLiteral("lrw-update");

} // namespace

LrwUpdateWizard::LrwUpdateWizard(FirmwareServices& services, QObject* parent) : Wizard(parent) {
    connect(&services, &FirmwareServices::languageChanged, this, &Wizard::changed);
}

LrwUpdateWizard::~LrwUpdateWizard() {
    m_procedure.reset(); // releases the hold before the tab's busy state
    releaseTab();
}

Action LrwUpdateWizard::requestAction() const { return m_action.value_or(Action::Flash); }

QString LrwUpdateWizard::title() const {
    switch (requestAction()) {
    case Action::Flash:
        return tr("Flash the LoRaWAN module");
    case Action::Erase:
        return tr("Erase the LoRaWAN module");
    case Action::Unlock:
        return tr("Unlock the LoRaWAN module");
    case Action::Reset:
        return tr("Reset the LoRaWAN module");
    }
    return {};
}

void LrwUpdateWizard::start(const ErrorInfo& context, DeviceTabViewModel* tab) {
    (void)context; // opened by the Flash page only
    m_tab = tab;
    auto* page = tab != nullptr
                     ? qobject_cast<FlashViewModel*>(tab->pageViewModel(FlashViewModel::pageId))
                     : nullptr;
    std::optional<device::LrwUpdateProcedure::Request> request =
        page != nullptr ? page->takeLrwRequest() : std::nullopt;
    if (!request) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, "no LoRaWAN module request");
        error.message = tr("Start the LoRaWAN module operations on the Flash page.").toStdString();
        m_startError = std::move(error);
        emit changed();
        return;
    }
    m_action = request->action;
    startProcedure(std::move(*request));
}

void LrwUpdateWizard::startProcedure(device::LrwUpdateProcedure::Request request) {
    m_startError.reset();
    // The tab's session (a stopped one is replaced).
    if (Status opened = m_tab->ensureSession(); !opened) {
        m_startError = std::move(opened).error();
        emit changed();
        return;
    }
    device::DeviceSession* session = m_tab->session();
    if (session == nullptr) {
        m_startError = makeError(ErrorCode::InternalUnexpected, "no session after ensureSession");
        emit changed();
        return;
    }
    m_procedure = std::make_unique<device::LrwUpdateProcedure>(
        *session, device::LrwUpdateProcedure::Options{});
    connect(m_procedure.get(), &device::LrwUpdateProcedure::changed, this, &Wizard::changed);
    connect(m_procedure.get(), &device::LrwUpdateProcedure::progressChanged, this,
            &LrwUpdateWizard::progressChanged);
    connect(m_procedure.get(), &device::LrwUpdateProcedure::finished, this,
            &LrwUpdateWizard::onProcedureFinished);
    if (Status started = m_procedure->start(std::move(request)); !started) {
        m_startError = std::move(started).error();
        m_procedure.reset();
        emit changed();
        return;
    }
    // The console shows the pause; the tab cannot be closed without asking.
    m_tab->setBusy(id(), tr("Paused for the LoRaWAN module"),
                   tr("A LoRaWAN module operation is in progress. Cancel it and close the tab?"),
                   [this] { cancelOperation(); });
    m_tabBusy = true;
    emit changed();
}

void LrwUpdateWizard::onProcedureFinished() {
    if (!m_procedure) {
        return;
    }
    const Step step = m_procedure->step();
    if (step == Step::Done || step == Step::Cancelled ||
        (step == Step::Failed && !m_procedure->canRetry())) {
        releaseTab();
    }
    if (step == Step::Cancelled) {
        emit finished(false); // the dialog closes (also after Cancel and close)
        return;
    }
    emit changed();
}

void LrwUpdateWizard::releaseTab() {
    if (m_tabBusy && !m_tab.isNull()) {
        m_tab->clearBusy(id());
    }
    m_tabBusy = false;
}

// ---- steps --------------------------------------------------------------------------------------

std::vector<WizardStep> LrwUpdateWizard::plannedSteps() const {
    QString module;
    switch (requestAction()) {
    case Action::Flash:
        module = tr("Flash");
        break;
    case Action::Erase:
        module = tr("Erase");
        break;
    case Action::Unlock:
        module = tr("Unlock");
        break;
    case Action::Reset:
        module = tr("Reset");
        break;
    }
    const std::array titles{tr("APP connector"),     tr("Stop the application"),
                            tr("LoRaWAN connector"), module,
                            tr("APP connector"),     tr("Done")};
    std::vector<WizardStep> steps;
    for (std::size_t i = 0; i < stepIds.size(); ++i) {
        WizardStep step;
        step.id = stepIds.at(i);
        step.title = titles.at(i);
        steps.push_back(step);
    }
    return steps;
}

int LrwUpdateWizard::stepIndex() const {
    if (!m_procedure) {
        return 0;
    }
    Step step = m_procedure->step();
    if (step == Step::Failed) {
        step = m_procedure->failedStep();
    }
    switch (step) {
    case Step::HoldApp:
        return 1;
    case Step::ConnectLrw:
        return 2;
    case Step::NeedsUnlockConfirmation:
    case Step::Unlock:
    case Step::Flash:
    case Step::Erase:
    case Step::ResetModule:
        return 3;
    case Step::ConnectAppAgain:
    case Step::ResetApp:
        return 4;
    case Step::Done:
        return 5;
    case Step::Idle:
    case Step::ConnectApp:
    case Step::Failed:
    case Step::Cancelled:
        break;
    }
    return 0;
}

int LrwUpdateWizard::stage() const {
    if (m_startError || !m_procedure) {
        return Failed;
    }
    switch (m_procedure->step()) {
    case Step::Idle:
    case Step::ConnectApp:
    case Step::ConnectLrw:
    case Step::ConnectAppAgain:
        return Connector;
    case Step::HoldApp:
    case Step::Unlock:
    case Step::Flash:
    case Step::Erase:
    case Step::ResetModule:
    case Step::ResetApp:
        return Working;
    case Step::NeedsUnlockConfirmation:
        return UnlockConfirmation;
    case Step::Failed:
    case Step::Cancelled:
        return Failed;
    case Step::Done:
        return Summary;
    }
    return Connector;
}

QString LrwUpdateWizard::workingHeading() const {
    switch (m_procedure ? m_procedure->step() : Step::Idle) {
    case Step::HoldApp:
        return tr("Stopping the application");
    case Step::Unlock:
        return tr("Unlocking the LoRaWAN module");
    case Step::Flash:
        return tr("Flashing the LoRaWAN module");
    case Step::Erase:
        return tr("Erasing the LoRaWAN module");
    case Step::ResetModule:
        return tr("Resetting the LoRaWAN module");
    default:
        return tr("Starting the application");
    }
}

WizardStep LrwUpdateWizard::doneStep(WizardStep step) const {
    step.kind = StepKind::Done;
    switch (requestAction()) {
    case Action::Flash:
        // Plain text (WizardDialog): the firmware's name may be a file name.
        step.heading = tr("LoRaWAN module flashed with %1.")
                           .arg(QString::fromStdString(m_procedure->request().name));
        break;
    case Action::Erase:
        step.heading = tr("LoRaWAN module erased.");
        break;
    case Action::Unlock:
        step.heading = tr("LoRaWAN module unlocked.");
        break;
    case Action::Reset:
        step.heading = tr("LoRaWAN module reset.");
        break;
    }
    step.body = tr("The application runs again. The console reconnects.");
    step.actions = QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("open-console")},
                                            {QStringLiteral("text"), tr("Open console")}}};
    return step;
}

WizardStep LrwUpdateWizard::current() const {
    WizardStep step = plannedSteps().at(static_cast<std::size_t>(stepIndex()));
    if (m_procedure && m_procedure->step() == Step::Done) {
        return doneStep(step);
    }
    step.kind = StepKind::Embedded;
    step.embedded = embeddedName;
    step.illustration = illustration(); // the connector steps: the picture of the connector
    switch (static_cast<Stage>(stage())) {
    case Connector:
        step.heading = m_procedure && m_procedure->step() == Step::ConnectLrw
                           ? tr("Connect the J-Link to the LoRaWAN connector")
                           : tr("Connect the J-Link to the APP connector");
        break;
    case Working:
        step.heading = workingHeading();
        break;
    case UnlockConfirmation:
        step.heading = tr("Unlock the LoRaWAN module?");
        step.primaryText = tr("Unlock and erase");
        break;
    case Failed:
        step.heading = tr("The LoRaWAN module operation failed");
        step.primaryText = m_procedure && m_procedure->canRetry() ? tr("Try again") : QString();
        break;
    case Summary:
        break;
    }
    return step;
}

bool LrwUpdateWizard::canPrimary() const {
    switch (static_cast<Stage>(stage())) {
    case UnlockConfirmation:
        return true;
    case Failed:
        return m_procedure && m_procedure->canRetry();
    case Connector:
    case Working:
    case Summary:
        break;
    }
    return false;
}

void LrwUpdateWizard::primary() {
    if (!canPrimary()) {
        return;
    }
    if (stage() == UnlockConfirmation) {
        if (Status confirmed = m_procedure->confirmUnlock(); !confirmed) {
            qCWarning(lcGui).noquote()
                << "LoRaWAN module: unlock refused:" << toQString(confirmed.error().detail);
        }
        return;
    }
    m_procedure->retry();
}

// ---- cancel -------------------------------------------------------------------------------------

bool LrwUpdateWizard::canCancel() const {
    // Not while an operation runs (a flash, erase or unlock is never interrupted).
    return !m_procedure || m_procedure->step() == Step::Done ||
           m_procedure->step() == Step::Failed || m_procedure->step() == Step::Cancelled ||
           m_procedure->canCancel();
}

QString LrwUpdateWizard::busyCancelText() const {
    if (!m_procedure || !m_procedure->canCancel() || !m_procedure->applicationHeld()) {
        return {};
    }
    return tr("The application MCU is stopped. It starts again when CHESTER is reset or powered "
              "off and on.");
}

QString LrwUpdateWizard::cancelAcceptText() const { return tr("Cancel anyway"); }

QString LrwUpdateWizard::cancelRejectText() const { return tr("Back"); }

void LrwUpdateWizard::cancelOperation() {
    if (m_procedure && m_procedure->canCancel()) {
        (void)m_procedure->cancel(); // finished(false) follows
        return;
    }
    if (!m_procedure || m_procedure->step() == Step::Failed || m_procedure->step() == Step::Done) {
        releaseTab();
    }
}

void LrwUpdateWizard::action(const QString& actionId) {
    releaseTab();
    emit handOff(actionId);
    if (m_procedure && m_procedure->step() == Step::Done) {
        emit finished(true); // the Done step's [Open console]: the guide is over
    }
}

// ---- texts and progress -------------------------------------------------------------------------

QString LrwUpdateWizard::promptText() const {
    if (m_startError) {
        return toQString(m_startError->message);
    }
    return m_procedure ? m_procedure->promptText() : QString();
}

QString LrwUpdateWizard::illustration() const {
    if (!m_procedure) {
        return {};
    }
    switch (m_procedure->step()) {
    case Step::ConnectApp:
    case Step::ConnectAppAgain:
        return QStringLiteral("chester-app-cable");
    case Step::ConnectLrw:
        return QStringLiteral("chester-lrw-cable");
    default:
        return {};
    }
}

QVariantList LrwUpdateWizard::phases() const {
    QVariantList list;
    if (!m_procedure) {
        return list;
    }
    const device::OperationProgress& progress = m_procedure->progress();
    bool reached = false;
    for (const device::ProgramPhase phase : progress.plannedPhases) {
        QString state = QStringLiteral("pending");
        if (phase == progress.phase) {
            state = QStringLiteral("current");
            reached = true;
        } else if (!reached) {
            state = QStringLiteral("done");
        }
        list.append(QVariantMap{{QStringLiteral("title"), FlashViewModel::phaseTitle(phase)},
                                {QStringLiteral("state"), state}});
    }
    return list;
}

qreal LrwUpdateWizard::progress() const {
    if (!m_procedure) {
        return -1;
    }
    const device::OperationProgress& progress = m_procedure->progress();
    return progress.total > 0
               ? static_cast<qreal>(progress.done) / static_cast<qreal>(progress.total)
               : -1;
}

ErrorInfo LrwUpdateWizard::error() const {
    if (m_startError) {
        return ErrorInfo(*m_startError);
    }
    if (m_procedure && m_procedure->error()) {
        return ErrorInfo(*m_procedure->error());
    }
    return {};
}

// ---- registration -------------------------------------------------------------------------------

void registerLrwUpdateWizard(AppViewModel& app, FirmwareServices& services) {
    (void)app.wizardRegistry().add(WizardDescriptor{
        .id = WizardId::LrwUpdate,
        .perTab = true,
        // Qt parent ownership: the WizardViewModel.
        .create = [&services](QObject* parent) -> Wizard* {
            return new LrwUpdateWizard(services, parent); // NOLINT(cppcoreguidelines-owning-memory)
        }});
    (void)app.wizardRegistry().addEmbedded(
        embeddedName,
        QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/flash/LrwUpdateStep.qml")));
}

} // namespace hwio::gui
