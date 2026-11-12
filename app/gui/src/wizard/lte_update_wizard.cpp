#include "wizard/lte_update_wizard.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/lte_update_procedure.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/remote/lte_update_report.h"
#include "pages/firmware_services.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/dependency_status.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/firmware_source_view_model.h"
#include "viewmodels/flash_view_model.h"
#include "viewmodels/remote_share_view_model.h"
#include "viewmodels/settings_view_model.h"
#include "wizard/wizard.h"
#include "wizard/wizard_registry.h"
#include "workers/firmware_loader.h"

#include <QDir>
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

using Step = device::LteUpdateProcedure::Step;
using Kind = FirmwareSourceViewModel::Kind;

// The ids of plannedSteps(), in order.
const std::array<QString, 8> stepIds{
    QStringLiteral("firmware"),    QStringLiteral("app-connector"),
    QStringLiteral("stop-app"),    QStringLiteral("lte-connector"),
    QStringLiteral("update-lte"),  QStringLiteral("app-connector-again"),
    QStringLiteral("application"), QStringLiteral("done")};

const QString embeddedName = QStringLiteral("lte-update");

} // namespace

LteUpdateWizard::LteUpdateWizard(FirmwareServices& services, QObject* parent)
    : Wizard(parent), m_services(services) {
    const auto profileOf = [this]() -> const device::DeviceProfile* {
        return m_tab.isNull() ? nullptr
                              : device::ProfileRegistry::find(toStdString(m_tab->profileId()));
    };
    // Qt parent ownership: both source parts are children of this wizard.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    m_lteSource = new FirmwareSourceViewModel(services, profileOf, this);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    m_appSource = new FirmwareSourceViewModel(services, profileOf, this);
    m_lteSource->setKind(Kind::LteFirmware);
    m_appSource->setKind(Kind::AppFirmware);
    const auto refresh = [this] {
        if (needsNrfUtil()) {
            m_services.dependencies()->ensureChecked();
        }
        emit changed();
    };
    connect(m_lteSource, &FirmwareSourceViewModel::sourceChanged, this, refresh);
    connect(m_appSource, &FirmwareSourceViewModel::sourceChanged, this, &Wizard::changed);
    connect(m_services.dependencies(), &DependencyStatus::nrfUtilChanged, this, &Wizard::changed);
    connect(&m_services, &FirmwareServices::languageChanged, this, &Wizard::changed);
}

LteUpdateWizard::~LteUpdateWizard() {
    if (!m_remote.isNull()) {
        m_remote->finish(); // the wizard closed: the procedure's state is the request's result
    }
    m_procedure.reset(); // releases the hold before the tab's busy state
    releaseTab();
}

QString LteUpdateWizard::title() const { return tr("Update the LTE firmware"); }

void LteUpdateWizard::start(const ErrorInfo& context, DeviceTabViewModel* tab) {
    (void)context; // opened from the banner, the Flash page or "Help me fix this": the same start
    m_tab = tab;
    if (needsNrfUtil()) {
        m_services.dependencies()->ensureChecked();
    }
    // Plan 0044 (ADR-0363): a remote request starts with the recommended firmware by itself.
    if (tab != nullptr && tab->remoteShare() != nullptr) {
        m_remote = tab->remoteShare()->takeLteUpdateRequest();
    }
    if (!m_remote.isNull()) {
        connect(m_remote.data(), &remote::LteUpdateReporter::supportQuestionChanged, this,
                &Wizard::changed); // ADR-0368
        m_lteSource->setLteSource(FirmwareSourceViewModel::Designated);
        m_lteSource->prepareDesignated();
        m_services.dependencies()->ensureChecked();
        connect(m_lteSource, &FirmwareSourceViewModel::designatedChanged, this,
                &LteUpdateWizard::checkRemoteStart);
        connect(m_services.dependencies(), &DependencyStatus::nrfUtilChanged, this,
                &LteUpdateWizard::checkRemoteStart);
        checkRemoteStart();
    }
    emit changed();
}

void LteUpdateWizard::checkRemoteStart() {
    if (m_remote.isNull() || m_remoteStarted || m_remote->done()) {
        return;
    }
    switch (m_lteSource->designatedState()) {
    case FirmwareSourceViewModel::DesignatedFailed:
        m_remoteStarted = true;
        // Only the code reaches support (op-result).
        m_remote->finish(std::unexpected(
            makeError(errorCodeFromId(toStdString(m_lteSource->designatedError().code()))
                          .value_or(ErrorCode::NetworkDownloadFailed),
                      "the designated LTE firmware")));
        return;
    case FirmwareSourceViewModel::DesignatedReady:
        break;
    default: {
        const qreal progress = m_lteSource->designatedProgress();
        m_remote->download(progress < 0 ? 0 : static_cast<std::uint64_t>(progress * 1000.0),
                           progress < 0 ? 0 : 1000);
        return;
    }
    }
    const DependencyStatus* dependencies = m_services.dependencies();
    if (dependencies->nrfUtilState() == DependencyStatus::Problem &&
        dependencies->nrfUtilProblem()) {
        m_remoteStarted = true;
        m_remote->finish(std::unexpected(*dependencies->nrfUtilProblem()));
        return;
    }
    if (dependencies->nrfUtilState() != DependencyStatus::Found) {
        return; // checked soon (nrfUtilChanged)
    }
    if (!canPrimary()) {
        return;
    }
    m_remoteStarted = true;
    startProcedure();
    if (m_procedure) {
        m_remote->follow(*m_procedure);
    } else {
        m_remote->finish(std::unexpected(
            m_startError.value_or(makeError(ErrorCode::InternalUnexpected, "not started"))));
    }
}

// ---- steps --------------------------------------------------------------------------------------

std::vector<WizardStep> LteUpdateWizard::plannedSteps() const {
    const std::array titles{tr("Firmware"),      tr("APP connector"), tr("Stop the application"),
                            tr("LTE connector"), tr("Update"),        tr("APP connector"),
                            tr("Application"),   tr("Done")};
    std::vector<WizardStep> steps;
    for (std::size_t i = 0; i < stepIds.size(); ++i) {
        WizardStep step;
        step.id = stepIds.at(i);
        step.title = titles.at(i);
        steps.push_back(step);
    }
    return steps;
}

int LteUpdateWizard::stepIndex() const {
    if (!m_procedure) {
        return 0;
    }
    const Step step = m_procedure->step();
    switch (step) {
    case Step::Idle:
        return 0;
    case Step::ConnectApp:
    case Step::Confirm: // plan 0046: the APP connector answered, nothing changed yet
        return 1;
    case Step::Backup:
    case Step::BackupFailed:
    case Step::Erase:
        return 2;
    case Step::ConnectLte:
        return 3;
    case Step::UpdateLte:
        return 4;
    case Step::ConnectAppAgain:
        return 5;
    case Step::ChooseApplication:
    case Step::Restore:
    case Step::Cancelled:
        return 6;
    case Step::Done:
        return 7;
    case Step::Failed:
        switch (m_procedure->failedStep()) {
        case Step::UpdateLte:
            return 4;
        case Step::Restore:
        case Step::ChooseApplication:
            return 6;
        default:
            return 2;
        }
    }
    return 0;
}

int LteUpdateWizard::stage() const {
    if (m_startError) {
        return Failed;
    }
    if (!m_procedure) {
        return Firmware;
    }
    switch (m_procedure->step()) {
    case Step::Idle:
        return Firmware;
    case Step::ConnectApp:
    case Step::ConnectLte:
    case Step::ConnectAppAgain:
        return Connector;
    case Step::Confirm:
        return Confirm;
    case Step::Backup:
    case Step::Erase:
    case Step::UpdateLte:
    case Step::Restore:
        return Working;
    case Step::BackupFailed:
        return BackupFailed;
    case Step::ChooseApplication:
        return Application;
    case Step::Failed:
    case Step::Cancelled:
        return Failed;
    case Step::Done:
        return Summary;
    }
    return Firmware;
}

WizardStep LteUpdateWizard::current() const {
    const int index = stepIndex();
    WizardStep step = plannedSteps().at(static_cast<std::size_t>(index));
    if (m_procedure && m_procedure->step() == Step::Done) {
        step.kind = StepKind::Done;
        step.heading = tr("LTE firmware updated");
        step.body = tr("The LTE MCU has its new firmware and the application runs again. The "
                       "console reconnects.");
        step.actions =
            QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("open-console")},
                                     {QStringLiteral("text"), tr("Open console")}}};
        return step;
    }
    step.kind = StepKind::Embedded;
    step.embedded = embeddedName;
    step.illustration = illustration(); // the connector steps: the picture of the connector
    switch (static_cast<Stage>(stage())) {
    case Firmware:
        step.heading = tr("Choose the LTE firmware");
        step.primaryText = tr("Next");
        break;
    case Connector:
        step.heading = m_procedure->step() == Step::ConnectLte
                           ? tr("Connect the J-Link to the LTE connector")
                           : tr("Connect the J-Link to the APP connector");
        break;
    case Confirm:
        step.heading = tr("Ready to update the LTE MCU");
        // ADR-0368: in a remote update support answers; the customer has no button.
        step.primaryText = supportDecides() ? QString() : tr("Continue");
        break;
    case Working:
        switch (m_procedure->step()) {
        case Step::Backup:
        case Step::Erase:
            step.heading = tr("Stopping the application");
            break;
        case Step::UpdateLte:
            step.heading = tr("Updating the LTE MCU");
            break;
        default:
            step.heading = tr("Writing the application");
            break;
        }
        break;
    case BackupFailed:
        step.heading = tr("No copy of the application");
        step.primaryText = supportDecides() ? QString() : tr("Continue without a copy");
        break;
    case Application:
        step.heading = tr("Put the application back");
        step.primaryText = useBackup() && hasBackup() ? tr("Write back") : tr("Flash");
        break;
    case Failed:
        step.heading = tr("The LTE firmware update failed");
        step.primaryText = m_procedure && m_procedure->canRetry() ? tr("Try again") : QString();
        break;
    case Summary:
        break;
    }
    return step;
}

bool LteUpdateWizard::needsNrfUtil() const {
    // 0021 (ADR-0174): whenever the LTE source contains modem firmware - the designated bundle
    // always does.
    if (m_lteSource->designatedActive()) {
        return true;
    }
    const LoadedSource* loaded = m_lteSource->loadedSource();
    return loaded != nullptr && (loaded->sourceClass == SourceClass::LteBundle ||
                                 loaded->sourceClass == SourceClass::ModemZip);
}

QString LteUpdateWizard::prerequisiteText() const {
    if (!needsNrfUtil()) {
        return {};
    }
    const std::optional<Error>& problem = m_services.dependencies()->nrfUtilProblem();
    if (!problem) {
        return {};
    }
    if (problem->code == ErrorCode::DependencyNrfUtilNotFound) {
        return tr("nRF Util was not found. It is needed for LTE firmware.");
    }
    return toQString(problem->message) + QStringLiteral(" ") + toQString(problem->suggestion);
}

QString LteUpdateWizard::nrfUtilDownloadUrl() { return SettingsViewModel::nrfUtilDownloadUrl(); }

bool LteUpdateWizard::firmwareReady() const {
    return m_lteSource->readySource() != nullptr && prerequisiteText().isEmpty() &&
           (!needsNrfUtil() || !m_services.dependencies()->nrfUtilProblem());
}

bool LteUpdateWizard::canPrimary() const {
    switch (static_cast<Stage>(stage())) {
    case Firmware:
        return firmwareReady() && !m_tab.isNull() && !m_tab->choosing() && !m_tab->isBusy();
    case Confirm:
    case BackupFailed:
        return !supportDecides(); // ADR-0368: support answers in a remote update
    case Application:
        return (useBackup() && hasBackup()) || m_appSource->readySource() != nullptr;
    case Failed:
        return m_procedure && m_procedure->canRetry();
    case Connector:
    case Working:
    case Summary:
        break;
    }
    return false;
}

bool LteUpdateWizard::canBack() const { return false; } // the procedure only moves forward

void LteUpdateWizard::back() {}

void LteUpdateWizard::primary() {
    if (!canPrimary()) {
        return;
    }
    switch (static_cast<Stage>(stage())) {
    case Firmware:
        startProcedure();
        break;
    case Confirm:
        m_procedure->confirm(); // ADR-0366: the copy and the erase start now
        break;
    case BackupFailed:
        m_procedure->continueWithoutBackup();
        break;
    case Application: {
        std::shared_ptr<const firmware::FirmwareImage> image;
        device::FlashOptions options;
        if (!(useBackup() && hasBackup())) {
            const LoadedSource* loaded = m_appSource->readySource();
            image = loaded != nullptr ? loaded->image : nullptr;
            options.allowUicr = m_appSource->hasUicr() && m_appSource->allowUicr();
        }
        if (Status started = m_procedure->restoreApplication(image, options); !started) {
            qCWarning(lcGui).noquote()
                << "LTE update: restore refused:" << toQString(started.error().detail);
        }
        break;
    }
    case Failed:
        m_procedure->retry();
        break;
    case Connector:
    case Working:
    case Summary:
        break;
    }
}

void LteUpdateWizard::startProcedure() {
    const LoadedSource* loaded = m_lteSource->readySource();
    if (loaded == nullptr || m_tab.isNull()) {
        return;
    }
    device::LteUpdateProcedure::Source source;
    source.name = toStdString(m_lteSource->sourceName());
    source.options.allowUicr = m_lteSource->hasUicr() && m_lteSource->allowUicr();
    if (loaded->sourceClass == SourceClass::ModemZip) {
        source.kind = device::LteUpdateProcedure::Source::Kind::Modem;
        source.modemZip = loaded->path;
    } else if (loaded->sourceClass == SourceClass::LteBundle) {
        source.kind = device::LteUpdateProcedure::Source::Kind::Bundle;
        source.bundle = loaded->bundle;
    } else {
        source.kind = device::LteUpdateProcedure::Source::Kind::Image;
        source.image = loaded->image;
    }
    m_startError.reset();
    // The tab's session (a stopped one, e.g. on the LTE connector, is replaced).
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
    // ADR-0368: a remote update always writes the copy back (no question to the customer).
    m_procedure = std::make_unique<device::LteUpdateProcedure>(
        *session, device::LteUpdateProcedure::Options{.pollInterval = std::chrono::seconds(1),
                                                      .backupDirectory = {},
                                                      .autoRestore = !m_remote.isNull(),
                                                      .now = {}});
    connect(m_procedure.get(), &device::LteUpdateProcedure::changed, this,
            &LteUpdateWizard::onProcedureChanged);
    connect(m_procedure.get(), &device::LteUpdateProcedure::progressChanged, this,
            &LteUpdateWizard::progressChanged);
    connect(m_procedure.get(), &device::LteUpdateProcedure::finished, this,
            &LteUpdateWizard::onProcedureFinished);
    if (Status started = m_procedure->start(std::move(source)); !started) {
        m_startError = std::move(started).error();
        m_procedure.reset();
        emit changed();
        return;
    }
    // D4: the console shows the pause; the tab cannot be closed without asking.
    m_tab->setBusy(id(), tr("Paused for the LTE firmware update"),
                   tr("The LTE firmware update is in progress. Cancel it and close the tab?"),
                   [this] { cancelOperation(); });
    m_tabBusy = true;
    emit changed();
}

void LteUpdateWizard::onProcedureChanged() {
    if (m_procedure && m_procedure->step() == Step::ChooseApplication && !hasBackup()) {
        m_useBackup = false;
        // ADR-0368: nothing to write back; support is told now (the LTE MCU is updated, the
        // application not restored). The customer may still flash an application here.
        if (!m_remote.isNull()) {
            m_remote->finish();
        }
    }
    emit changed();
}

void LteUpdateWizard::onProcedureFinished() {
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

void LteUpdateWizard::releaseTab() {
    if (m_tabBusy && !m_tab.isNull()) {
        m_tab->clearBusy(id());
    }
    m_tabBusy = false;
}

// ---- cancel -------------------------------------------------------------------------------------

bool LteUpdateWizard::canCancel() const {
    // Plan 0033 C: not while the erase or a flash runs.
    return !m_procedure || m_procedure->step() == Step::Done ||
           m_procedure->step() == Step::Failed || m_procedure->step() == Step::Cancelled ||
           m_procedure->canCancel();
}

QString LteUpdateWizard::busyCancelText() const {
    if (!m_procedure || !m_procedure->canCancel() || !m_procedure->applicationErased()) {
        return {};
    }
    QString text = tr("The application MCU is erased. The device will not run until you flash an "
                      "application.");
    if (m_procedure->hasBackup()) {
        text += QStringLiteral(" ") + tr("The copy of the application stays in %1.")
                                          .arg(QDir::toNativeSeparators(QString::fromStdU16String(
                                              m_procedure->backupFile().u16string())));
    }
    return text;
}

QString LteUpdateWizard::cancelAcceptText() const { return tr("Cancel anyway"); }

QString LteUpdateWizard::cancelRejectText() const { return tr("Back"); }

void LteUpdateWizard::cancelOperation() {
    if (m_procedure && m_procedure->canCancel()) {
        (void)m_procedure->cancel(); // finished(false) follows
        return;
    }
    if (!m_procedure || m_procedure->step() == Step::Failed || m_procedure->step() == Step::Done) {
        releaseTab();
    }
}

void LteUpdateWizard::action(const QString& actionId) {
    releaseTab();
    emit handOff(actionId);
    if (m_procedure && m_procedure->step() == Step::Done) {
        emit finished(true); // the Done step's [Open console]: the guide is over
    }
}

// ---- texts and progress -------------------------------------------------------------------------

QString LteUpdateWizard::promptText() const {
    if (m_startError) {
        return toQString(m_startError->message);
    }
    if (!m_procedure) {
        return tr("Updating the LTE MCU needs the J-Link moved between the APP and LTE "
                  "connectors. The guide stops the application, updates the LTE MCU and puts the "
                  "application back.");
    }
    QString text = m_procedure->promptText();
    // ADR-0368: support answers the remote update's questions.
    if (supportDecides() && m_procedure->step() == Step::Confirm) {
        text += QStringLiteral("\n\n") +
                tr("HARDWARIO Support confirms the update. Keep the J-Link cable on the APP "
                   "connector for now.");
    } else if (supportDecides() && m_procedure->step() == Step::BackupFailed) {
        text += QStringLiteral("\n\n") +
                tr("HARDWARIO Support decides whether to continue without a copy.");
    }
    return text;
}

bool LteUpdateWizard::supportDecides() const {
    return !m_remote.isNull() && m_remote->supportDecides();
}

QString LteUpdateWizard::illustration() const {
    if (!m_procedure) {
        return {};
    }
    switch (m_procedure->step()) {
    case Step::ConnectApp:
    case Step::ConnectAppAgain:
        return QStringLiteral("chester-app-cable");
    case Step::ConnectLte:
        return QStringLiteral("chester-lte-cable");
    default:
        return {};
    }
}

bool LteUpdateWizard::wrongConnector() const {
    return m_procedure && m_procedure->wrongConnector();
}

QVariantList LteUpdateWizard::phases() const {
    QVariantList list;
    if (!m_procedure || m_procedure->step() == Step::Backup) {
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

QString LteUpdateWizard::stepText() const {
    if (!m_procedure) {
        return {};
    }
    const device::OperationProgress& progress = m_procedure->progress();
    return progress.steps > 1 ? tr("Step %1 of %2").arg(progress.step).arg(progress.steps)
                              : QString();
}

qreal LteUpdateWizard::progress() const {
    if (!m_procedure) {
        return -1;
    }
    const device::OperationProgress& progress = m_procedure->progress();
    return progress.total > 0
               ? static_cast<qreal>(progress.done) / static_cast<qreal>(progress.total)
               : -1;
}

void LteUpdateWizard::setUseBackup(bool use) {
    if (use == m_useBackup || (use && !hasBackup())) {
        return;
    }
    m_useBackup = use;
    emit changed();
}

bool LteUpdateWizard::hasBackup() const { return m_procedure && m_procedure->hasBackup(); }

QString LteUpdateWizard::backupText() const {
    if (!hasBackup()) {
        return {};
    }
    return tr("Copy: %1")
        .arg(QDir::toNativeSeparators(
            QString::fromStdU16String(m_procedure->backupFile().u16string())));
}

ErrorInfo LteUpdateWizard::error() const {
    if (m_startError) {
        return ErrorInfo(*m_startError);
    }
    if (m_procedure && m_procedure->error()) {
        return ErrorInfo(*m_procedure->error());
    }
    return {};
}

// ---- registration -------------------------------------------------------------------------------

void registerLteUpdateWizard(AppViewModel& app, FirmwareServices& services) {
    (void)app.wizardRegistry().add(WizardDescriptor{
        .id = WizardId::LteUpdate,
        .perTab = true,
        // Qt parent ownership: the WizardViewModel.
        .create = [&services](QObject* parent) -> Wizard* {
            return new LteUpdateWizard(services, parent); // NOLINT(cppcoreguidelines-owning-memory)
        }});
    (void)app.wizardRegistry().addEmbedded(
        embeddedName,
        QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/flash/LteUpdateStep.qml")));
}

} // namespace hwio::gui
