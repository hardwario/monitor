#include "wizard/flash_wizard.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/diag/check.h"
#include "hwio/diag/environment.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/program_progress.h"
#include "pages/firmware_services.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/firmware_source_view_model.h"
#include "viewmodels/flash_view_model.h"
#include "viewmodels/probe_picker_view_model.h"
#include "wizard/check_list_model.h"
#include "wizard/probe_picker_step.h"
#include "wizard/wizard.h"
#include "wizard/wizard_registry.h"
#include "workers/diag_worker.h"
#include "workers/firmware_loader.h"

#include <QCoreApplication>
#include <QLocale>
#include <QLoggingCategory>
#include <QModelIndex>
#include <QObject>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

using device::OperationKind;
using firmware::ProgramPhase;

const std::array<QString, 6> stepIds{QStringLiteral("probe"),    QStringLiteral("cable"),
                                     QStringLiteral("firmware"), QStringLiteral("confirm"),
                                     QStringLiteral("flash"),    QStringLiteral("done")};

// The Diagnose steps the Cable step shows (plan 0010 step 3: RTT is not needed for flashing).
constexpr std::array<std::string_view, 4> cableCheckIds{"probe", "power", "connect", "identify"};

// The environment checks of the Probe step (plan 0035 § A; usb-access on Linux only).
std::vector<std::string> probeCheckIds() {
#ifdef Q_OS_LINUX
    return {"usb-access", "probe-holders", "probes"};
#else
    return {"probe-holders", "probes"};
#endif
}

QVariantMap button(const QString& id, const QString& text) {
    return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("text"), text}};
}

QString secondsText(std::chrono::milliseconds duration) {
    const double seconds = static_cast<double>(duration.count()) / 1000.0;
    return seconds < 10.0 ? QLocale().toString(seconds, 'f', 1)
                          : QLocale().toString(static_cast<qlonglong>(std::lround(seconds)));
}

QString elapsedText(std::chrono::milliseconds elapsed) {
    const auto total = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

std::uint64_t imageBytes(const firmware::FirmwareImage& image) {
    std::uint64_t bytes = 0;
    for (const firmware::Segment& segment : image.segments()) {
        bytes += segment.data.size();
    }
    return bytes;
}

bool fits(const device::TargetProfile& target, const firmware::FirmwareImage& image) {
    return std::ranges::all_of(image.segments(), [&target](const firmware::Segment& segment) {
        return device::checkFlashWrite(target, segment.address, segment.data.size(), true)
            .has_value();
    });
}

const QString openWizardPrefix = QStringLiteral("open-wizard:");
const QString lteUpdateId = QStringLiteral("lte-update");

} // namespace

FlashWizard::FlashWizard(AppViewModel& app, FirmwareServices& services, QObject* parent)
    : Wizard(parent), m_app(app), m_services(services),
      // Qt parent ownership: the source part and the check lists are children of this wizard.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      m_source(new FirmwareSourceViewModel(
          services, [this]() -> const device::DeviceProfile* { return profile(); }, this)),
      m_probeChecks(new CheckListModel(this)),       // NOLINT(cppcoreguidelines-owning-memory)
      m_cableChecks(new CheckListModel(this)),       //  NOLINT(cppcoreguidelines-owning-memory)
      m_requester(app.diagWorker().newRequester()) { // ADR-0394
    // ADR-0245: application firmware only; the kind never changes.
    m_source->setKind(FirmwareSourceViewModel::AppFirmware);
    connect(m_source, &FirmwareSourceViewModel::sourceChanged, this, &Wizard::changed);
    connect(m_source, &FirmwareSourceViewModel::allowUicrChanged, this, &Wizard::changed);
    // The kind hint's [Switch] (an LTE bundle or modem zip): LTE firmware is the LTE update's.
    connect(m_source, &FirmwareSourceViewModel::kindSuggestionAccepted, this, [this](int kind) {
        if (kind == FirmwareSourceViewModel::LteFirmware) {
            updateLteFirmware();
        }
    });
    connect(m_cableChecks, &CheckListModel::rowsChanged, this, &Wizard::changed);
    connect(m_probeChecks, &CheckListModel::rowsChanged, this, &Wizard::changed);
    DiagWorker& worker = m_app.diagWorker();
    connect(&worker, &DiagWorker::checkProgress, this,
            [this](quint64 generation, const diag::CheckResult& result) {
                if (generation == m_probeCheckRun) {
                    m_probeChecks->update(result);
                }
            });
    connect(&worker, &DiagWorker::checksFinished, this,
            [this](quint64 generation, const std::vector<diag::CheckResult>& results) {
                if (generation != m_probeCheckRun) {
                    return;
                }
                for (const diag::CheckResult& result : results) {
                    m_probeChecks->update(result);
                }
                m_probeCheckRun = 0;
                emit changed();
            });
    // Review 0009 F7: a request replaced by another one (the start-up check) runs again.
    connect(&worker, &DiagWorker::requestDropped, this, [this](quint64 generation) {
        if (generation != 0 && generation == m_probeCheckRun) {
            m_probeCheckRun = 0;
            runProbeChecks();
        }
    });
    connect(&m_services, &FirmwareServices::languageChanged, this, &FlashWizard::retranslate);
}

FlashWizard::~FlashWizard() {
    for (const QMetaObject::Connection& connection : m_sessionConnections) {
        disconnect(connection);
    }
    if (m_probeCheckRun != 0) {
        m_app.diagWorker().cancel(m_probeCheckRun);
    }
    // Review 0009 F1: a Cable-step Diagnose still running is cancelled; the tab releases the
    // wizards' probe-only session when it has ended (ADR-0300).
    stopDiagnose();
    releaseTab();
}

QString FlashWizard::title() const { return tr("Flash application firmware"); }

std::optional<FlashWizard::Step> FlashWizard::startStepFor(ErrorCode code) {
    switch (code) {
    case ErrorCode::FirmwareHexParseError:
    case ErrorCode::FirmwareInvalidImage:
    case ErrorCode::FirmwareOutOfRange: // with the LTE hand-off when the image fits the LTE MCU
    case ErrorCode::FirmwareNotFoundInCatalog:
        return Firmware;
    default:
        return std::nullopt;
    }
}

// ---- start --------------------------------------------------------------------------------------

void FlashWizard::start(const ErrorInfo& context, DeviceTabViewModel* tab) {
    m_tab = tab;
    m_context = context;
    if (m_tab.isNull()) {
        return;
    }
    connect(m_tab, &DeviceTabViewModel::sessionChanged, this, &FlashWizard::onSessionChanged);
    connect(m_tab, &DeviceTabViewModel::busyChanged, this, &Wizard::changed);
    watchSession();
    // firmware.out_of_range from the Flash page: does the page's image fit the LTE MCU (0035)?
    if (m_context.valid() && m_context.code() == toQString(idOf(ErrorCode::FirmwareOutOfRange))) {
        if (const auto* flash =
                qobject_cast<FlashViewModel*>(m_tab->pageViewModel(FlashViewModel::pageId))) {
            const LoadedSource* loaded = flash->source()->loadedSource();
            const device::DeviceProfile* deviceProfile = profile();
            const device::TargetProfile* lte =
                deviceProfile != nullptr ? device::findTarget(*deviceProfile, "lte") : nullptr;
            const device::TargetProfile* app = target();
            m_contextFitsLte = loaded != nullptr && loaded->image && lte != nullptr &&
                               fits(*lte, *loaded->image) &&
                               (app == nullptr || !fits(*app, *loaded->image));
        }
    }
    if (hasProbe()) {
        // Steps already satisfied are skipped (ADR-0074): the Cable step only while Diagnose has
        // not passed.
        m_skipCableOnPass = true;
        goTo(Cable);
    } else {
        m_probeStepShown = true;
        goTo(Probe);
    }
}

bool FlashWizard::hasProbe() const {
    return !m_tab.isNull() && !m_tab->choosing() && !m_tab->isBle() &&
           m_tab->probeSerial().has_value();
}

const device::DeviceProfile* FlashWizard::profile() const {
    if (!m_tab.isNull() && !m_tab->choosing()) {
        return device::ProfileRegistry::find(toStdString(m_tab->profileId()));
    }
    if (m_picker != nullptr) {
        return device::ProfileRegistry::find(toStdString(m_picker->profileId()));
    }
    return nullptr;
}

const device::TargetProfile* FlashWizard::target() const {
    const device::DeviceProfile* deviceProfile = profile();
    return deviceProfile != nullptr ? device::findTarget(*deviceProfile, toStdString(targetId()))
                                    : nullptr;
}

QString FlashWizard::targetId() {
    // ADR-0233: the console target (CHESTER app) is the target of application firmware.
    return FirmwareSourceViewModel::targetIdFor(FirmwareSourceViewModel::AppFirmware);
}

// ---- steps --------------------------------------------------------------------------------------

std::vector<WizardStep> FlashWizard::plannedSteps() const {
    const std::array titles{tr("Probe"),   tr("Cable"), tr("Firmware"),
                            tr("Confirm"), tr("Flash"), tr("Done")};
    std::vector<WizardStep> steps;
    for (std::size_t i = 0; i < stepIds.size(); ++i) {
        WizardStep step;
        step.id = stepIds.at(i);
        step.title = titles.at(i);
        steps.push_back(step);
    }
    return steps;
}

int FlashWizard::stage() const {
    switch (m_step) {
    case Probe:
        return ProbeStage;
    case Cable:
        return CableStage;
    case Firmware:
        return FirmwareStage;
    case Confirm:
        return ConfirmStage;
    case Flash:
        return Flashing;
    case Done:
        break;
    }
    if (!m_outcome || m_outcome->has_value()) {
        return Succeeded;
    }
    return m_outcome->error().code == ErrorCode::Cancelled ? Cancelled : Failed;
}

WizardStep FlashWizard::current() const {
    WizardStep step = plannedSteps().at(static_cast<std::size_t>(m_step));
    switch (m_step) {
    case Probe:
        step.kind = StepKind::Embedded;
        step.embedded = probePickerStepName;
        step.heading = tr("Choose the J-Link");
        step.body = tr("Connect the J-Link to your computer with a USB cable.");
        step.primaryText = tr("Next");
        break;
    case Cable: {
        step.kind = StepKind::Checks;
        step.heading = tr("Connect the J-Link to the device");
        const device::TargetProfile* consoleTarget = target();
        if (consoleTarget != nullptr && !consoleTarget->connectionGuide.empty()) {
            const device::ConnectionGuideStep& guide = consoleTarget->connectionGuide.front();
            step.illustration = toQString(guide.illustrationSlot);
            step.body = device::guideStepBody(guide);
        }
        if (wrongConnector()) {
            step.body = tr("The J-Link answers from the LTE MCU. Move the flat cable to the APP "
                           "connector and check again, or update the LTE firmware.");
            if (lteUpdateAvailable()) {
                step.actions = QVariantList{button(lteUpdateId, tr("Update LTE firmware…"))};
            }
        } else if (targetProtected()) {
            step.body = tr("The MCU is protected. Flashing removes the protection only through "
                           "Recover, which erases everything.");
            step.actions =
                QVariantList{button(QStringLiteral("open-flash-page"), tr("Open Flash page"))};
        }
        step.primaryText = cableRunning() || cablePassed() ? tr("Next") : tr("Check again");
        break;
    }
    case Firmware:
        step.kind = StepKind::Embedded;
        step.embedded = firmwareSourceStep;
        step.heading = tr("Choose the firmware");
        step.primaryText = tr("Next");
        break;
    case Confirm:
        step.kind = StepKind::Embedded;
        step.embedded = summaryStep;
        step.heading = tr("Ready to flash");
        step.primaryText = tr("Flash");
        break;
    case Flash:
        step.kind = StepKind::Embedded;
        step.embedded = progressStep;
        step.heading = tr("Flashing");
        break;
    case Done:
        switch (static_cast<Stage>(stage())) {
        case Failed: {
            step.kind = StepKind::Embedded;
            step.embedded = progressStep;
            step.heading = tr("Flashing failed");
            step.primaryText = tr("Try again");
            if (!helpWizard().isEmpty()) {
                step.actions = QVariantList{
                    button(QStringLiteral("help-me-fix-this"), tr("Help me fix this"))};
            }
            break;
        }
        case Cancelled:
            step.kind = StepKind::Info;
            step.heading = tr("Flashing was cancelled");
            step.body = tr("The device may not start until you flash it again.");
            step.primaryText = tr("Flash again");
            break;
        default:
            step.kind = StepKind::Done;
            step.heading = resultText();
            step.primaryText = tr("Open console");
            step.actions =
                QVariantList{button(QStringLiteral("flash-another"), tr("Flash another"))};
            break;
        }
        break;
    }
    return step;
}

CheckListModel* FlashWizard::checks() {
    switch (m_step) {
    case Probe:
        return m_probeChecks;
    case Cable:
        return m_cableChecks;
    default:
        return nullptr;
    }
}

QObject* FlashWizard::embeddedViewModel() {
    if (m_step == Probe) {
        return m_picker;
    }
    return this;
}

void FlashWizard::goTo(Step step) {
    m_step = step;
    if (m_picker != nullptr) {
        m_picker->setActive(step == Probe); // the scanner runs only while the step is shown
    }
    if (step == Probe) {
        enterProbe();
    }
    if (step == Cable) {
        startDiagnose();
    }
    emit changed();
}

void FlashWizard::enterProbe() {
    if (m_picker == nullptr) {
        // Qt parent ownership: this wizard.
        m_picker = m_app.makeProbePicker(this);
        connect(m_picker, &ProbePickerViewModel::selectedRowChanged, this, &Wizard::changed);
        connect(m_picker, &ProbePickerViewModel::countChanged, this, [this] {
            // No probe (any more): the checks explain why (plan 0035 § A).
            if (m_step == Probe && m_picker->scanned() && m_picker->count() == 0) {
                runProbeChecks();
            }
        });
        connect(m_picker, &ProbePickerViewModel::scanChanged, this, [this] {
            if (m_step == Probe && m_picker->scanned() && m_picker->count() == 0 &&
                m_probeChecks->count() == 0) {
                runProbeChecks();
            }
        });
    }
    m_picker->setActive(true);
}

void FlashWizard::runProbeChecks() {
    if (m_probeCheckRun != 0) {
        return; // one run at a time
    }
    const diag::EnvironmentContext context = m_app.diagEnvironment();
    const std::vector<std::string> ids = probeCheckIds();
    std::vector<QString> rowIds;
    std::vector<QString> titles;
    for (const diag::Check& check : diag::environmentChecks(context)) {
        if (std::ranges::find(ids, check.id) != ids.end()) {
            rowIds.push_back(toQString(check.id));
            titles.push_back(QCoreApplication::translate("hwio::diag", check.titleSourceText));
        }
    }
    m_probeChecks->reset(rowIds, titles);
    m_probeCheckRun = m_app.diagWorker().runChecks(
        DiagWorker::ChecksRequest{.context = context, .ids = ids, .requester = m_requester});
    emit changed();
}

void FlashWizard::primary() {
    if (!canPrimary()) {
        return;
    }
    switch (m_step) {
    case Probe: {
        const std::optional<std::uint32_t> serial = m_picker->selectedSerial();
        const QString product =
            m_picker
                ->data(m_picker->index(m_picker->selectedRow()), ProbePickerViewModel::ProductRole)
                .toString();
        if (!serial || m_tab.isNull() ||
            !m_tab->configure(m_picker->profileId(), m_picker->targetId(), *serial, product)) {
            return;
        }
        goTo(Cable);
        break;
    }
    case Cable:
        if (cablePassed()) {
            goTo(Firmware);
        } else {
            startDiagnose(); // Check again
            emit changed();
        }
        break;
    case Firmware:
        goTo(Confirm);
        break;
    case Confirm:
        startFlash();
        break;
    case Flash:
        break;
    case Done:
        switch (static_cast<Stage>(stage())) {
        case Failed:
        case Cancelled:
            goTo(Confirm); // Try again / Flash again
            break;
        default:
            handOffAndClose(QStringLiteral("open-console"));
            break;
        }
        break;
    }
}

bool FlashWizard::canPrimary() const {
    switch (m_step) {
    case Probe:
        return m_picker != nullptr && m_picker->selectedSerial().has_value() && !m_tab.isNull();
    case Cable:
        return !cableRunning() && !m_diagnose && !targetProtected();
    case Firmware:
        return firmwareReady();
    case Confirm:
        return cannotFlashReason().isEmpty();
    case Flash:
        return false;
    case Done:
        return true;
    }
    return false;
}

void FlashWizard::back() {
    if (!canBack()) {
        return;
    }
    switch (m_step) {
    case Cable:
        // Another probe: the tab goes back to its picker (the wizard configured it).
        stopDiagnose();
        if (!m_tab.isNull()) {
            m_tab->chooseAnotherProbeForWizard(); // plan 0056 (D-11)
        }
        m_cableChecks->reset({}, {});
        goTo(Probe);
        break;
    case Firmware:
        goTo(Cable);
        break;
    case Confirm:
        goTo(Firmware);
        break;
    default:
        break;
    }
}

bool FlashWizard::canBack() const {
    switch (m_step) {
    case Cable:
        return m_probeStepShown;
    case Firmware:
    case Confirm:
        return true;
    default:
        return false;
    }
}

void FlashWizard::action(const QString& actionId) {
    if (actionId == lteUpdateId) {
        updateLteFirmware();
    } else if (actionId == QStringLiteral("open-flash-page")) {
        // target.protected (plan 0033 E, ADR-0236): Recover is shown once on the Flash page.
        handOffAndClose(QStringLiteral("open-flash-page:recover:") + targetId());
    } else if (actionId == QStringLiteral("flash-another")) {
        m_source->clear();
        m_outcome.reset();
        goTo(Firmware);
    } else if (actionId == QStringLiteral("help-me-fix-this")) {
        // ADR-0297: the error's wizard opens with the error (e.g. the matching troubleshooting
        // topic), not at its start.
        if (!helpWizard().isEmpty()) {
            const ErrorInfo shown = error();
            releaseTab();
            emit handOffError(shown);
            emit finished(false);
        }
    }
}

void FlashWizard::handOffAndClose(const QString& actionId) {
    releaseTab();
    emit handOff(actionId);
    emit finished(actionId == QStringLiteral("open-console"));
}

void FlashWizard::updateLteFirmware() {
    if (!lteUpdateAvailable() || m_flash) {
        return;
    }
    stopDiagnose();
    handOffAndClose(openWizardPrefix + lteUpdateId);
}

bool FlashWizard::lteUpdateAvailable() const { return m_app.wizardRegistry().has(lteUpdateId); }

void FlashWizard::openCatalog() {
    if (!m_tab.isNull() && m_source->docsUrl().isValid()) {
        m_tab->openExternalUrl(m_source->docsUrl());
    }
}

QString FlashWizard::helpWizard() const {
    const ErrorInfo shown = error();
    if (!shown.valid() || shown.wizard().isEmpty() || shown.wizard() == wizardId ||
        !m_app.wizardRegistry().has(shown.wizard())) {
        return {};
    }
    return shown.wizard();
}

// ---- Cable: Diagnose ----------------------------------------------------------------------------

void FlashWizard::startDiagnose() {
    if (m_diagnose || m_tab.isNull()) {
        return; // one at a time (repeated clicks are ignored while one runs)
    }
    m_diagnoseError.reset();
    std::vector<QString> ids;
    std::vector<QString> titles;
    if (const device::TargetProfile* consoleTarget = target(); consoleTarget != nullptr) {
        for (const device::DiagnoseStepInfo& info : device::diagnoseSteps(*consoleTarget)) {
            if (std::ranges::find(cableCheckIds, info.id) != cableCheckIds.end()) {
                ids.push_back(toQString(info.id));
                titles.push_back(toQString(info.title));
            }
        }
    }
    m_cableChecks->reset(ids, titles);
    Result<device::OperationId> started = [this]() -> Result<device::OperationId> {
        if (Status opened = m_tab->ensureWizardSession(); !opened) {
            return std::unexpected(std::move(opened).error());
        }
        device::DeviceSession* session = m_tab->session();
        if (session == nullptr) {
            return std::unexpected(
                makeError(ErrorCode::InternalUnexpected, "no session after ensureSession"));
        }
        // ADR-0296: up to `identify`; the RTT steps are not needed for flashing (plan 0010).
        return session->diagnose(toStdString(targetId()), cableCheckIds.back());
    }();
    watchSession();
    if (!started) {
        // The first row shows why (e.g. internal.busy: another operation runs).
        Error error = std::move(started).error();
        m_cableChecks->update(device::DiagnoseStep{.id = std::string(cableCheckIds.front()),
                                                   .status = diag::CheckStatus::Failed,
                                                   .result = error.message,
                                                   .error = error});
        m_diagnoseError = std::move(error);
        m_skipCableOnPass = false;
        emit changed();
        return;
    }
    m_diagnose =
        DiagnoseRun{.id = *started, .session = m_tab->session(), .identifyReported = false};
    m_cableChecks->update(device::DiagnoseStep{.id = std::string(cableCheckIds.front()),
                                               .status = diag::CheckStatus::Running,
                                               .result = {},
                                               .error = std::nullopt});
    emit changed();
}

void FlashWizard::stopDiagnose() {
    if (!m_diagnose) {
        return;
    }
    cancelDiagnose();
    m_diagnose.reset();
}

void FlashWizard::onDiagnoseStep(device::OperationId id, const device::DiagnoseStep& step) {
    if (!m_diagnose || id != m_diagnose->id || m_diagnose->identifyReported) {
        return;
    }
    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto it = std::ranges::find(cableCheckIds, std::string_view(step.id));
    if (it == cableCheckIds.end()) {
        return;
    }
    m_cableChecks->update(step);
    if (std::next(it) != cableCheckIds.end()) {
        m_cableChecks->update(device::DiagnoseStep{.id = std::string(*std::next(it)),
                                                   .status = diag::CheckStatus::Running,
                                                   .result = {},
                                                   .error = std::nullopt});
        return;
    }
    m_diagnose->identifyReported = true; // the last step of this Diagnose (ADR-0296)
}

void FlashWizard::cancelDiagnose() {
    if (!m_diagnose || m_diagnose->session.isNull()) {
        return;
    }
    if (const Status status = m_diagnose->session->cancel(m_diagnose->id); !status) {
        // The operation has ended already (its result is on the way).
        qCDebug(lcGui).noquote() << "Flash wizard: Diagnose cancel refused:"
                                 << toQString(status.error().detail);
    }
}

void FlashWizard::onDiagnoseDone() {
    m_diagnose.reset();
    // Rows a cancelled or failed Diagnose did not report stay "not checked".
    for (const std::string_view id : cableCheckIds) {
        const diag::CheckStatus status = m_cableChecks->status(toQString(id));
        if (status == diag::CheckStatus::Pending || status == diag::CheckStatus::Running) {
            m_cableChecks->update(device::DiagnoseStep{.id = std::string(id),
                                                       .status = diag::CheckStatus::Skipped,
                                                       .result = tr("Not checked").toStdString(),
                                                       .error = std::nullopt});
        }
    }
    const bool skip = std::exchange(m_skipCableOnPass, false);
    if (skip && m_step == Cable && cablePassed()) {
        goTo(Firmware);
        return;
    }
    emit changed();
}

bool FlashWizard::cableRunning() const { return m_diagnose.has_value(); }

bool FlashWizard::cablePassed() const {
    if (m_diagnose || m_diagnoseError || m_cableChecks->count() == 0 || targetProtected()) {
        return false;
    }
    return std::ranges::all_of(cableCheckIds, [this](std::string_view id) {
        const diag::CheckStatus status = m_cableChecks->status(toQString(id));
        return status == diag::CheckStatus::Ok || status == diag::CheckStatus::Warning;
    });
}

bool FlashWizard::wrongConnector() const {
    return std::ranges::any_of(cableCheckIds, [this](std::string_view id) {
        const diag::CheckResult* row = m_cableChecks->row(toQString(id));
        return row != nullptr && row->error && row->status == diag::CheckStatus::Failed &&
               (row->error->code == ErrorCode::TargetWrongFamily ||
                row->error->code == ErrorCode::TargetOtherConnector);
    });
}

bool FlashWizard::targetProtected() const {
    const diag::CheckResult* row = m_cableChecks->row(QStringLiteral("identify"));
    return row != nullptr && row->error && row->error->code == ErrorCode::TargetProtected;
}

// ---- Firmware and Confirm -----------------------------------------------------------------------

FlashWizard::ImageFit FlashWizard::imageFit() const {
    const LoadedSource* loaded = m_source->readySource();
    const device::TargetProfile* app = target();
    if (loaded == nullptr || !loaded->image || app == nullptr) {
        return ImageFit::NoImage;
    }
    if (fits(*app, *loaded->image)) {
        return ImageFit::Application;
    }
    const device::DeviceProfile* deviceProfile = profile();
    const device::TargetProfile* lte =
        deviceProfile != nullptr ? device::findTarget(*deviceProfile, "lte") : nullptr;
    return lte != nullptr && fits(*lte, *loaded->image) ? ImageFit::Lte : ImageFit::Neither;
}

bool FlashWizard::firmwareReady() const {
    return m_source->readySource() != nullptr && imageFit() == ImageFit::Application &&
           (!m_source->hasUicr() || m_source->allowUicr());
}

QString FlashWizard::explanation() const {
    // An image of the wizard's own source that does not fit (firmware.out_of_range, 0035).
    switch (imageFit()) {
    case ImageFit::Lte:
        return tr("This is LTE firmware. Update it with the LTE firmware update, which moves the "
                  "J-Link between the connectors.");
    case ImageFit::Neither:
        return tr("This firmware is for another MCU. Check whether it is application or LTE "
                  "firmware.");
    case ImageFit::Application:
        return {};
    case ImageFit::NoImage:
        break;
    }
    if (!m_context.valid() || m_source->sourceState() == FirmwareSourceViewModel::Ready) {
        return {};
    }
    const QString code = m_context.code();
    if (code == toQString(idOf(ErrorCode::FirmwareHexParseError))) {
        static const QRegularExpression linePattern(QStringLiteral("line (\\d+)"));
        const QRegularExpressionMatch match = linePattern.match(m_context.detail());
        if (match.hasMatch()) {
            return tr("The file is not a valid HEX file (line %1). Use the .hex file produced by "
                      "the build, or download it from the catalog again.")
                .arg(match.captured(1));
        }
        return tr("The file is not a valid HEX file. Use the .hex file produced by the build, or "
                  "download it from the catalog again.");
    }
    if (code == toQString(idOf(ErrorCode::FirmwareInvalidImage))) {
        return tr("This file is not CHESTER firmware. Application and LTE firmware are .hex files; "
                  "LTE bundles and modem firmware are .zip files.");
    }
    if (code == toQString(idOf(ErrorCode::FirmwareOutOfRange))) {
        return m_contextFitsLte
                   ? tr("This is LTE firmware. Update it with the LTE firmware update, which "
                        "moves the J-Link between the connectors.")
                   : tr("This firmware is for another MCU. Check whether it is application or LTE "
                        "firmware.");
    }
    if (code == toQString(idOf(ErrorCode::FirmwareNotFoundInCatalog))) {
        return tr("No firmware has this ID. Copy the ID from the firmware catalog again.");
    }
    return {};
}

bool FlashWizard::explanationOffersLteUpdate() const {
    if (!lteUpdateAvailable()) {
        return false;
    }
    const ImageFit fit = imageFit();
    if (fit == ImageFit::Lte) {
        return true;
    }
    return fit == ImageFit::NoImage && m_contextFitsLte && !explanation().isEmpty();
}

bool FlashWizard::explanationOffersCatalog() const {
    return m_context.valid() &&
           m_context.code() == toQString(idOf(ErrorCode::FirmwareNotFoundInCatalog)) &&
           !explanation().isEmpty() && m_source->docsUrl().isValid();
}

bool FlashWizard::otherOperationRunning() const {
    if (m_tab.isNull()) {
        return false;
    }
    if (m_tab->isBusy() && m_tab->busyPage() != wizardId) {
        return true;
    }
    const device::DeviceSession* session = m_tab->session();
    return !m_flash && session != nullptr && session->runningOperation().has_value();
}

QString FlashWizard::cannotFlashReason() const {
    if (m_tab.isNull()) {
        return tr("The device tab was closed");
    }
    if (!firmwareReady()) {
        return tr("Choose a firmware file");
    }
    if (otherOperationRunning()) {
        return tr("Another operation is running");
    }
    return {};
}

QString FlashWizard::summaryText() const {
    const LoadedSource* loaded = m_source->readySource();
    if (loaded == nullptr || !loaded->image || m_tab.isNull()) {
        return {};
    }
    const QString size = QLocale().formattedDataSize(
        static_cast<qint64>(imageBytes(*loaded->image)), 0, QLocale::DataSizeSIFormat);
    const device::DeviceProfile* deviceProfile = profile();
    const QString device =
        deviceProfile != nullptr ? toQString(deviceProfile->displayName) : m_tab->profileId();
    const QString mcu = ProbePickerViewModel::targetTitle(m_tab->profileId(), targetId());
    const QString probe = m_tab->probeSerial() ? QString::number(*m_tab->probeSerial()) : QString();
    // "chester-clime-v3.2.1.hex (Intel HEX, 412 kB) → Application MCU (nRF52840) of CHESTER via
    // J-Link 683012345."
    return tr("%1 (Intel HEX, %2) → %3 of %4 via J-Link %5.")
        .arg(m_source->sourceName(), size, mcu, device, probe);
}

QString FlashWizard::summaryNote() const {
    if (m_source->hasUicr() && m_source->allowUicr()) {
        return tr("The current application firmware is replaced. The UICR configuration is also "
                  "written; the Product Information Block is kept.");
    }
    return tr("The current application firmware is replaced. Configuration and the Product "
              "Information Block are kept.");
}

// ---- Flash --------------------------------------------------------------------------------------

void FlashWizard::startFlash() {
    const LoadedSource* loaded = m_source->readySource();
    if (loaded == nullptr || !loaded->image || m_tab.isNull() || m_flash) {
        return;
    }
    stopDiagnose();
    m_outcome.reset();
    m_chip.reset();
    m_cancelRequested = false;
    m_flashedName = m_source->sourceName();
    m_flashedBytes = imageBytes(*loaded->image);
    const device::FlashOptions options{.halt = false, // expert option, Flash page only (0010)
                                       .allowUicr = m_source->hasUicr() && m_source->allowUicr()};
    // The tab's session, or a probe-only one (operations run on it, ADR-0069).
    Result<device::OperationId> started = [&]() -> Result<device::OperationId> {
        if (Status opened = m_tab->ensureWizardSession(); !opened) {
            return std::unexpected(std::move(opened).error());
        }
        device::DeviceSession* session = m_tab->session();
        if (session == nullptr) {
            return std::unexpected(
                makeError(ErrorCode::InternalUnexpected, "no session after ensureSession"));
        }
        return session->flash(toStdString(targetId()), loaded->image, options);
    }();
    watchSession();
    if (!started) {
        m_outcome = Status(std::unexpected(std::move(started).error()));
        goTo(Done);
        return;
    }
    m_flash = FlashRun{.id = *started, .session = m_tab->session()};
    m_plannedPhases = device::plannedPhases(OperationKind::Flash, false, false);
    m_currentPhase = 0;
    m_done = 0;
    m_total = 0;
    m_elapsed = {};
    qCInfo(lcGui).noquote() << "Flash wizard: flashing" << m_flashedName;
    m_tab->setBusy(wizardId, tr("Flashing…"),
                   tr("Flashing is in progress. Cancel it and close the tab?"),
                   [this] { cancelFlash(); });
    m_tabBusy = true;
    goTo(Flash);
    emit progressChanged();
}

void FlashWizard::cancelFlash() {
    if (!m_flash || m_cancelRequested) {
        return;
    }
    m_cancelRequested = true;
    if (m_flash->session.isNull()) {
        finishFlash(Status(std::unexpected(makeError(ErrorCode::Cancelled, "the session is gone"))),
                    m_elapsed, std::nullopt);
        return;
    }
    if (const Status status = m_flash->session->cancel(m_flash->id); !status) {
        qCInfo(lcGui).noquote() << "Flash wizard: cancel refused:"
                                << toQString(status.error().detail);
    }
    emit changed();
}

void FlashWizard::onOperationProgress(device::OperationId id,
                                      const device::OperationProgress& progress) {
    if (!m_flash || id != m_flash->id) {
        return;
    }
    if (!progress.plannedPhases.empty()) {
        if (progress.plannedPhases.data() != m_plannedPhases.data()) {
            m_currentPhase = 0;
        }
        m_plannedPhases = progress.plannedPhases;
    }
    for (std::size_t i = static_cast<std::size_t>(std::max(m_currentPhase, 0));
         i < m_plannedPhases.size(); ++i) {
        if (m_plannedPhases[i] == progress.phase) {
            m_currentPhase = static_cast<int>(i);
            break;
        }
    }
    m_done = progress.done;
    m_total = progress.total;
    m_elapsed = progress.elapsed;
    emit progressChanged();
}

void FlashWizard::onOperationFinished(device::OperationId id,
                                      const device::OperationResult& result) {
    if (m_flash && id == m_flash->id) {
        finishFlash(result.status, result.duration, result.chip);
        return;
    }
    if (m_diagnose && id == m_diagnose->id) {
        onDiagnoseDone();
        return;
    }
    emit changed(); // another operation ended: [Flash] may be possible again
}

void FlashWizard::finishFlash(const Status& status, std::chrono::milliseconds duration,
                              const std::optional<device::ChipInfo>& chip) {
    m_flash.reset();
    m_cancelRequested = false;
    m_duration = duration;
    m_chip = chip;
    m_outcome = status;
    if (!status) {
        qCInfo(lcGui).noquote() << "Flash wizard: flash ended:"
                                << toQString(idOf(status.error().code))
                                << toQString(status.error().detail);
    }
    releaseTab();
    emit progressChanged();
    goTo(Done);
}

void FlashWizard::releaseTab() {
    if (m_tabBusy && !m_tab.isNull()) {
        m_tab->clearBusy(wizardId);
    }
    m_tabBusy = false;
}

QString FlashWizard::busyCancelText() const {
    if (m_flash) {
        return tr("Flashing is in progress. Cancel it and close the wizard?");
    }
    // Review 0009 F1: the Cable step's Diagnose is cancelled before the wizard closes.
    return m_diagnose ? tr("The connection is being checked. Cancel it and close the wizard?")
                      : QString();
}

void FlashWizard::cancelOperation() {
    if (m_probeCheckRun != 0) {
        m_app.diagWorker().cancel(m_probeCheckRun);
        m_probeCheckRun = 0;
    }
    stopDiagnose();
    cancelFlash();
}

// ---- session ------------------------------------------------------------------------------------

void FlashWizard::watchSession() {
    device::DeviceSession* session = m_tab.isNull() ? nullptr : m_tab->session();
    if (session == m_watched.data() && (session == nullptr || !m_sessionConnections.empty())) {
        return;
    }
    for (const QMetaObject::Connection& connection : m_sessionConnections) {
        disconnect(connection);
    }
    m_sessionConnections.clear();
    m_watched = session;
    if (session == nullptr) {
        return;
    }
    m_sessionConnections.push_back(
        connect(session, &device::DeviceSession::diagnoseStep, this, &FlashWizard::onDiagnoseStep));
    m_sessionConnections.push_back(connect(session, &device::DeviceSession::operationProgress, this,
                                           &FlashWizard::onOperationProgress));
    m_sessionConnections.push_back(connect(session, &device::DeviceSession::operationFinished, this,
                                           &FlashWizard::onOperationFinished));
}

void FlashWizard::onSessionChanged() {
    watchSession();
    const device::DeviceSession* session = m_tab.isNull() ? nullptr : m_tab->session();
    if (m_diagnose && m_diagnose->session.data() != session) {
        onDiagnoseDone(); // the session went away: the remaining rows are not checked
    }
    if (m_flash && m_flash->session.data() != session) {
        // Should not happen (the tab is busy): the flash ends as failed.
        finishFlash(Status(std::unexpected(
                        makeError(ErrorCode::TargetNotConnected,
                                  "the session was closed while the operation was running"))),
                    m_elapsed, std::nullopt);
        return;
    }
    emit changed();
}

// ---- texts and progress -------------------------------------------------------------------------

QString FlashWizard::operationTitle() const {
    const device::TargetProfile* consoleTarget = target();
    const QString mcu =
        consoleTarget != nullptr ? device::targetDisplayName(*consoleTarget) : targetId();
    return tr("Flashing %1 to the %2 MCU").arg(m_flashedName, mcu);
}

QVariantList FlashWizard::phases() const {
    QVariantList list;
    const bool succeeded = m_step == Done && stage() == Succeeded;
    for (std::size_t i = 0; i < m_plannedPhases.size(); ++i) {
        const int index = static_cast<int>(i);
        QString state = QStringLiteral("pending");
        if (index < m_currentPhase || succeeded) {
            state = QStringLiteral("done");
        } else if (index == m_currentPhase && m_flash) {
            state = QStringLiteral("current");
        }
        list.append(
            QVariantMap{{QStringLiteral("title"), FlashViewModel::phaseTitle(m_plannedPhases[i])},
                        {QStringLiteral("state"), state}});
    }
    return list;
}

qreal FlashWizard::progress() const {
    return m_total > 0 ? static_cast<qreal>(m_done) / static_cast<qreal>(m_total) : -1;
}

QString FlashWizard::progressText() const {
    if (cancelling()) {
        return tr("Cancelling…");
    }
    QStringList parts;
    if (m_total > 0) {
        const QLocale locale;
        parts.append(QStringLiteral("%1 / %2").arg(
            locale.formattedDataSize(static_cast<qint64>(m_done), 0, QLocale::DataSizeSIFormat),
            locale.formattedDataSize(static_cast<qint64>(m_total), 0, QLocale::DataSizeSIFormat)));
        parts.append(
            QStringLiteral("%1 %").arg(static_cast<int>((100 * m_done + m_total / 2) / m_total)));
    }
    parts.append(elapsedText(m_elapsed));
    return parts.join(QStringLiteral(" · "));
}

bool FlashWizard::cancelAvailable() const { return m_flash.has_value() && !m_cancelRequested; }

QString FlashWizard::resultText() const {
    switch (static_cast<Stage>(stage())) {
    case Succeeded: {
        QString text = tr("Firmware flashed and verified in %1 s.").arg(secondsText(m_duration));
        if (m_chip && !m_chip->name.empty()) {
            text += QLatin1Char(' ') + tr("Chip: %1").arg(toQString(m_chip->name));
        }
        return text;
    }
    case Failed:
        // ADR-0109: an abandoned download is an interrupted flash.
        if (m_outcome && !m_outcome->has_value() &&
            m_outcome->error().code == ErrorCode::TargetProgramFailed) {
            return tr("Flashing was interrupted. The firmware may be partly written: flash it "
                      "again.");
        }
        return {};
    default:
        return {};
    }
}

ErrorInfo FlashWizard::error() const {
    if (m_step == Done && m_outcome && !m_outcome->has_value()) {
        return ErrorInfo(m_outcome->error());
    }
    return {};
}

void FlashWizard::triggerErrorAction(int errorAction) {
    if (m_step != Done || stage() != Failed) {
        return;
    }
    if (errorAction == error_action::ChooseFile) {
        goTo(Firmware);
    } else if (errorAction == error_action::OpenSettings) {
        const QString section = error().settingsSection();
        handOffAndClose(QStringLiteral("open-settings:") + section);
    } else {
        goTo(Confirm);
    }
}

void FlashWizard::retranslate() {
    emit changed();
    emit progressChanged();
}

// ---- registration -------------------------------------------------------------------------------

void registerFlashWizard(AppViewModel& app, FirmwareServices& services) {
    (void)app.wizardRegistry().add(
        WizardDescriptor{.id = WizardId::FlashFirmware,
                         .perTab = true,
                         .localDeviceOnly = true, // ADR-0393
                         // Qt parent ownership: the WizardViewModel.
                         .create = [&app, &services](QObject* parent) -> Wizard* {
                             return new FlashWizard(
                                 app, services, parent); // NOLINT(cppcoreguidelines-owning-memory)
                         }});
    const auto qml = [](const QString& file) {
        return QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/wizard/%1.qml").arg(file));
    };
    (void)app.wizardRegistry().addEmbedded(FlashWizard::firmwareSourceStep,
                                           qml(QStringLiteral("FirmwareSourceStep")));
    (void)app.wizardRegistry().addEmbedded(FlashWizard::summaryStep,
                                           qml(QStringLiteral("FlashSummaryStep")));
    (void)app.wizardRegistry().addEmbedded(FlashWizard::progressStep,
                                           qml(QStringLiteral("PhaseProgressStep")));
}

} // namespace hwio::gui
