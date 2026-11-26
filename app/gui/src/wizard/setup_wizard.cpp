#include "wizard/setup_wizard.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/diag/check.h"
#include "hwio/diag/environment.h"
#include "hwio/diag/usb_access.h"
#include "hwio/hw/jlink/types.h"
#include "hwio/hw/nrfutil/nrfutil.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/probe_firmware_view_model.h"
#include "viewmodels/probe_picker_view_model.h"
#include "viewmodels/ui_mode.h"
#include "viewmodels/ui_mode_controller.h"
#include "wizard/check_list_model.h"
#include "wizard/probe_picker_step.h"
#include "wizard/wizard.h"
#include "wizard/wizard_launcher.h"
#include "wizard/wizard_registry.h"
#include "wizard/wizard_view_model.h"
#include "workers/diag_worker.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

const QString completedVersionKey = QStringLiteral("setup/completedVersion");
const QString showAtStartKey = QStringLiteral("setup/showAtStart");
const QString jlinkPathKey = QStringLiteral("jlink/libraryPath");
const QString nrfUtilPathKey = QStringLiteral("nrfutil/path");
const QString setupStepControl = QStringLiteral("setup-step");

const QString welcomeStep = QStringLiteral("welcome");
const QString jlinkStep = QStringLiteral("jlink");
const QString usbStep = QStringLiteral("usb");
const QString nrfUtilStep = QStringLiteral("nrfutil");
const QString probeStep = QStringLiteral("probe");
const QString doneStep = QStringLiteral("done");

const QString skipAction = QStringLiteral("skip-setup");
const QString continueAction = QStringLiteral("continue-anyway");
const QString nextAction = QStringLiteral("next");
const QString checkAgainAction = QStringLiteral("check-again");
const QString automaticAction = QStringLiteral("automatic-detection");
const QString chooseAction = QStringLiteral("choose-path");
const QString connectAction = QStringLiteral("connect-device");
const QString firmwarePrefix = QStringLiteral("update-firmware:");

QString pathOf(const QUrl& file) {
    return file.isLocalFile() ? file.toLocalFile() : file.toString();
}

QVariantMap button(const QString& id, const QString& text, bool file = false) {
    return QVariantMap{
        {QStringLiteral("id"), id}, {QStringLiteral("text"), text}, {QStringLiteral("file"), file}};
}

QVariantMap actionOf(const QString& id, const QString& text, bool enabled = true) {
    return QVariantMap{{QStringLiteral("id"), id},
                       {QStringLiteral("text"), text},
                       {QStringLiteral("enabled"), enabled}};
}

} // namespace

SetupWizard::SetupWizard(AppViewModel& app, Options options, QObject* parent)
    : Wizard(parent), m_app(app), m_settings(*app.settingsStore()), m_options(std::move(options)),
      // Qt parent ownership: the check list and the summary are children.
      m_checks(new CheckListModel(this)),            // NOLINT(cppcoreguidelines-owning-memory)
      m_summary(new CheckListModel(this)),           //  NOLINT(cppcoreguidelines-owning-memory)
      m_requester(app.diagWorker().newRequester()) { // ADR-0394
    if (!m_options.environment) {
        m_options.environment = [&app] { return app.diagEnvironment(); };
    }
    connect(&m_app.diagWorker(), &DiagWorker::checkProgress, this, &SetupWizard::onProgress);
    connect(&m_app.diagWorker(), &DiagWorker::checksFinished, this, &SetupWizard::onFinished);
    // Review 0009 F7: a request replaced by another one runs again.
    connect(&m_app.diagWorker(), &DiagWorker::requestDropped, this, [this](quint64 generation) {
        if (generation != 0 && generation == m_generation) {
            m_generation = 0;
            runChecks(std::exchange(m_candidate, std::nullopt));
        }
    });
    // ADR-0254: the step texts follow a language change at once (they are built by current()).
    connect(&m_app, &AppViewModel::languageChanged, this, [this] {
        if (stepId() == doneStep) {
            fillSummary();
        }
        emit changed();
    });
    connect(&m_settings, &SettingsStore::valueChanged, this, [this](const QString& key) {
        if (key == jlinkPathKey || key == showAtStartKey) {
            emit changed(); // [Use automatic detection], the checkbox
        }
    });
    // ADR-0291: the firmware actions follow the tabs and a finished update re-checks the probes.
    connect(&m_app, &AppViewModel::probeUsageChanged, this, &Wizard::changed);
    if (ProbeFirmwareViewModel* update = m_app.probeFirmware(); update != nullptr) {
        connect(update, &ProbeFirmwareViewModel::changed, this, &Wizard::changed);
        connect(update, &ProbeFirmwareViewModel::finished, this, [this] {
            if (stepId() == probeStep) {
                runChecks();
            }
        });
    }
}

SetupWizard::~SetupWizard() {
    if (m_generation != 0) {
        m_app.diagWorker().cancel(m_generation);
    }
}

SetupWizard::Platform SetupWizard::currentPlatform() {
    const QString kernel = QSysInfo::kernelType();
    if (kernel == QStringLiteral("darwin")) {
        return Platform::MacOS;
    }
    if (kernel == QStringLiteral("winnt")) {
        return Platform::Windows;
    }
    return Platform::Linux;
}

QString SetupWizard::startStepFor(ErrorCode code) {
    switch (code) {
    case ErrorCode::DependencyJLinkNotFound:
    case ErrorCode::DependencyJLinkLoadFailed:
    case ErrorCode::DependencyJLinkTooOld:
        return jlinkStep;
    case ErrorCode::UsbAccessDenied:
    case ErrorCode::UsbWorldAccessible:
        return usbStep;
    case ErrorCode::DependencyNrfUtilNotFound:
    case ErrorCode::DependencyNrfUtilDeviceMissing:
    case ErrorCode::DependencyNrfUtilTooOld:
        return nrfUtilStep;
    default:
        return {};
    }
}

QString SetupWizard::title() const { return tr("Setup"); }

std::vector<QString> SetupWizard::stepIds() const {
    std::vector<QString> ids;
    if (!m_fromError) {
        ids.push_back(welcomeStep);
    }
    ids.push_back(jlinkStep);
    if (m_options.platform == Platform::Linux) {
        ids.push_back(usbStep); // udev rules (ADR-0072, ADR-0185)
    }
    ids.push_back(nrfUtilStep);
    ids.push_back(probeStep);
    ids.push_back(doneStep);
    return ids;
}

void SetupWizard::start(const ErrorInfo& context, DeviceTabViewModel* /*tab*/) {
    QString first;
    if (context.valid()) {
        if (const auto code = errorCodeFromId(toStdString(context.code()))) {
            first = startStepFor(*code);
        }
    }
    m_fromError = !first.isEmpty();
    // Plan 0056 (D-2): the mode question only while the mode is not chosen (snapshot: it stays
    // when the mode is changed in Settings meanwhile); never when started from an error.
    const UiModeController* uiMode = m_app.uiMode();
    m_askMode = !m_fromError && uiMode != nullptr && !uiMode->chosen();
    m_selectedMode = static_cast<int>(uiMode != nullptr ? uiMode->mode() : UiMode::Advanced);
    m_steps = stepIds();
    m_index = 0;
    if (m_fromError) {
        const auto it = std::ranges::find(m_steps, first);
        // usb.* on a system without the USB step: the J-Link step (the first one).
        m_index = it != m_steps.end() ? static_cast<int>(it - m_steps.begin()) : 0;
    }
    enterStep();
}

std::vector<WizardStep> SetupWizard::plannedSteps() const {
    std::vector<WizardStep> steps;
    steps.reserve(m_steps.size());
    for (const QString& id : m_steps) {
        WizardStep step;
        step.id = id;
        if (id == welcomeStep) {
            step.title = tr("Welcome");
        } else if (id == jlinkStep) {
            step.title = tr("J-Link");
        } else if (id == usbStep) {
            step.title = tr("USB access");
        } else if (id == nrfUtilStep) {
            step.title = tr("nRF Util");
        } else if (id == probeStep) {
            step.title = tr("Test");
        } else {
            step.title = tr("Done");
        }
        steps.push_back(step);
    }
    return steps;
}

QString SetupWizard::stepId() const {
    return m_index >= 0 && std::cmp_less(m_index, m_steps.size())
               ? m_steps.at(static_cast<std::size_t>(m_index))
               : QString();
}

diag::CheckStatus SetupWizard::status(const char* checkId) const {
    return m_checks->status(QString::fromLatin1(checkId));
}

std::optional<ErrorCode> SetupWizard::errorCode(const char* checkId) const {
    const diag::CheckResult* row = m_checks->row(QString::fromLatin1(checkId));
    if (row == nullptr || !row->error) {
        return std::nullopt;
    }
    return row->error->code;
}

bool SetupWizard::stepPassed() const {
    const QString id = stepId();
    if (id == jlinkStep) {
        return status("jlink-software") == diag::CheckStatus::Ok;
    }
    if (id == usbStep) {
        return status("usb-access") == diag::CheckStatus::Ok;
    }
    if (id == nrfUtilStep) {
        return status("nrfutil") == diag::CheckStatus::Ok;
    }
    return true;
}

WizardStep SetupWizard::current() const { return stepFor(stepId()); }

WizardStep SetupWizard::stepFor(const QString& id) const {
    WizardStep step;
    for (const WizardStep& planned : plannedSteps()) {
        if (planned.id == id) {
            step = planned;
        }
    }
    if (id == welcomeStep) {
        fillWelcome(step);
    } else if (id == jlinkStep) {
        fillJLink(step);
    } else if (id == usbStep) {
        fillUsb(step);
    } else if (id == nrfUtilStep) {
        fillNrfUtil(step);
    } else if (id == probeStep) {
        step.kind = StepKind::Embedded;
        step.embedded = probePickerStepName;
        // ADR-0290: plan 0008 step 5 shows the probes for information only.
        step.embeddedOptions = QVariantMap{{QStringLiteral("readOnly"), true}};
        step.illustration = QStringLiteral("jlink-usb");
        step.heading = tr("Test with a probe");
        step.body = tr("Connect a J-Link to your computer. You do not need a device for this "
                       "test.<br>No J-Link yet? You can test later from Help → Setup wizard.");
        step.primaryText = tr("Next");
        step.actions = firmwareActions();
    } else {
        step.kind = StepKind::Done;
        step.embedded = setupStepControl;
        step.heading = tr("Setup finished");
        if (!m_fromError && m_app.launcher()->canOpen(connectWizardId)) {
            step.actions = QVariantList{actionOf(connectAction, tr("Connect a device"))};
        }
    }
    return step;
}

void SetupWizard::fillWelcome(WizardStep& step) const {
    step.kind = StepKind::Info;
    step.embedded = setupStepControl;
    step.illustration = QStringLiteral("setup-welcome");
    step.heading = m_options.mode == Mode::JLinkNeeded ? tr("J-Link Software is needed")
                                                       : tr("Welcome to HARDWARIO Monitor");
    step.body = tr("This assistant checks that your computer is ready to work with CHESTER "
                   "over a J-Link probe. It takes about two minutes.");
    step.primaryText = tr("Next");
    step.actions = QVariantList{actionOf(skipAction, tr("Skip setup"))};
}

QString SetupWizard::installerHint() const {
    switch (m_options.platform) {
    case Platform::Linux:
        return tr("Choose the 64-bit DEB or RPM installer.");
    case Platform::MacOS:
        return tr("Choose the macOS universal installer.");
    case Platform::Windows:
        return tr("Choose the Windows 64-bit installer.");
    }
    return {};
}

void SetupWizard::fillJLink(WizardStep& step) const {
    step.kind = StepKind::Checks;
    step.embedded = setupStepControl;
    step.heading = tr("J-Link Software");
    const bool passed = stepPassed();
    const bool checking = m_generation != 0;
    step.primaryText = passed || checking ? tr("Next") : tr("Check again");
    if (checking || passed) {
        return;
    }
    QString body;
    switch (errorCode("jlink-software").value_or(ErrorCode::InternalUnexpected)) {
    case ErrorCode::DependencyJLinkNotFound:
        body = tr("HARDWARIO Monitor uses the J-Link Software from SEGGER. Install version "
                  "8.00 or newer, then press Check again.") +
               QStringLiteral(" ") + installerHint();
        break;
    case ErrorCode::DependencyJLinkTooOld:
        body = tr("This J-Link Software is too old. Install version 8.00 or newer.");
        break;
    case ErrorCode::DependencyJLinkLoadFailed:
        body = tr("The J-Link library was found but could not be loaded. Reinstall J-Link "
                  "Software.");
        break;
    default:
        break;
    }
    if (!body.isEmpty()) {
        step.linkText = tr("Download J-Link Software");
        step.linkUrl = jlinkDownloadUrl;
        body += QStringLiteral("<br><br>");
    }
    step.body = body + tr("You can continue without J-Link, but you cannot connect to a "
                          "device.");
    step.actions = QVariantList{actionOf(continueAction, tr("Continue anyway"))};
}

void SetupWizard::fillUsb(WizardStep& step) const {
    step.kind = StepKind::Checks;
    step.embedded = setupStepControl;
    step.heading = tr("USB access");
    const diag::CheckStatus usb = status("usb-access");
    if (m_generation != 0 || usb == diag::CheckStatus::Ok) {
        step.primaryText = tr("Next");
    } else if (usb == diag::CheckStatus::Failed) {
        step.body = tr("Linux needs a rule that allows your user to use SEGGER USB devices. "
                       "Run these commands in a terminal, then unplug and reconnect the "
                       "J-Link.");
        step.primaryText = tr("Check again");
        step.actions = QVariantList{actionOf(nextAction, tr("Next"))};
    } else if (usb == diag::CheckStatus::Warning) {
        // usb.world_accessible (ADR-0185 amendment): replace the rule by the uaccess rule.
        step.body = tr("Every user account on this computer can use the J-Link. Run these "
                       "commands in a terminal to allow it only for your user, then unplug "
                       "and reconnect the J-Link.");
        step.primaryText = tr("Check again");
        step.actions = QVariantList{actionOf(nextAction, tr("Next"))};
    } else {
        step.primaryText = tr("Next"); // Skipped: no J-Link connected
        step.actions = QVariantList{actionOf(checkAgainAction, tr("Check again"))};
    }
}

void SetupWizard::fillNrfUtil(WizardStep& step) const {
    step.kind = StepKind::Checks;
    step.embedded = setupStepControl;
    step.heading = tr("nRF Util (optional)");
    step.body = tr("Needed only to update the LTE modem firmware. Everything else works "
                   "without it.");
    if (m_generation != 0 || stepPassed()) {
        step.primaryText = tr("Next");
        return;
    }
    if (errorCode("nrfutil") == ErrorCode::DependencyNrfUtilNotFound) {
        step.linkText = tr("How to install nRF Util");
        step.linkUrl = nrfUtilInstallUrl;
    }
    step.primaryText = tr("Skip");
    step.actions = QVariantList{actionOf(checkAgainAction, tr("Check again"))};
}

QVariantList SetupWizard::firmwareActions() const {
    // ADR-0291: one action per probe with outdated firmware (from the facts of the last check, not
    // from the error text), only while the `probe-firmware` row is a Warning.
    QVariantList actions;
    ProbeFirmwareViewModel* update = m_app.probeFirmware();
    if (update == nullptr || !m_facts || m_generation != 0 ||
        status("probe-firmware") != diag::CheckStatus::Warning) {
        return actions;
    }
    for (const diag::EnvironmentFacts::Probe& probe : m_facts->probes) {
        if (probe.firmware != jlink::FirmwareStatus::Outdated) {
            continue;
        }
        const QString serial = QString::number(probe.info.serialNumber);
        const QString reason = update->blockedReason(probe.info.serialNumber);
        if (reason.isEmpty()) {
            actions.append(
                actionOf(firmwarePrefix + serial, tr("Update J-Link firmware %1…").arg(serial)));
        } else {
            // ADR-0246: a probe used by a tab (or another reason): the button says why.
            actions.append(actionOf(firmwarePrefix + serial, reason, false));
        }
    }
    return actions;
}

bool SetupWizard::canBack() const { return m_index > 0 && stepId() != doneStep; }

bool SetupWizard::canPrimary() const {
    const QString id = stepId();
    if (id == jlinkStep || id == usbStep || id == nrfUtilStep) {
        return m_generation == 0;
    }
    return true;
}

void SetupWizard::primary() {
    const QString id = stepId();
    if (id == doneStep) {
        emit finished(true);
        return;
    }
    if (id == jlinkStep || id == usbStep) {
        if (m_generation != 0) {
            return;
        }
        if (!stepPassed()) {
            const bool skippedUsb =
                id == usbStep && status("usb-access") == diag::CheckStatus::Skipped;
            if (!skippedUsb) {
                runChecks(); // Check again
                return;
            }
        }
    }
    goTo(m_index + 1); // nRF Util: Next or Skip; probe: always
}

void SetupWizard::back() {
    if (canBack()) {
        goTo(m_index - 1);
    }
}

void SetupWizard::action(const QString& actionId) {
    if (actionId == skipAction) {
        applyMode(); // plan 0056 (D-4)
        complete();
        emit finished(false);
    } else if (actionId == continueAction || actionId == nextAction) {
        goTo(m_index + 1);
    } else if (actionId == checkAgainAction) {
        runChecks();
    } else if (actionId == automaticAction) {
        savePath(jlinkPathKey, QString());
    } else if (actionId == connectAction) {
        connectDevice();
    } else if (actionId.startsWith(firmwarePrefix)) {
        if (ProbeFirmwareViewModel* update = m_app.probeFirmware(); update != nullptr) {
            update->requestForSerial(actionId.mid(firmwarePrefix.size()));
        }
    }
}

void SetupWizard::cancelOperation() {
    if (m_generation != 0) {
        m_app.diagWorker().cancel(m_generation);
        m_generation = 0;
    }
    if (m_picker != nullptr) {
        m_picker->setActive(false);
    }
}

CheckListModel* SetupWizard::checks() { return m_checks; }

QObject* SetupWizard::embeddedViewModel() {
    if (stepId() == probeStep && m_picker != nullptr) {
        return m_picker;
    }
    return this;
}

void SetupWizard::goTo(int index) {
    if (index < 0 || std::cmp_greater_equal(index, m_steps.size()) || index == m_index) {
        return;
    }
    if (stepId() == welcomeStep) {
        applyMode(); // plan 0056 (D-4): leaving Welcome stores the selected mode
    }
    record();
    m_index = index;
    enterStep();
}

void SetupWizard::enterStep() {
    const QString id = stepId();
    if (m_generation != 0) {
        m_app.diagWorker().cancel(m_generation);
        m_generation = 0;
    }
    m_candidate.reset();
    m_pathError.clear();
    m_facts.reset();
    if (id == probeStep && m_picker == nullptr) {
        m_picker = m_app.makeProbePicker(this); // Qt parent ownership: this wizard
        connect(m_picker, &ProbePickerViewModel::countChanged, this, &SetupWizard::onProbesChanged);
        connect(m_picker, &ProbePickerViewModel::scanChanged, this, &SetupWizard::onProbesChanged);
    }
    if (m_picker != nullptr) {
        m_picker->setActive(id == probeStep); // the scanner runs only while the step is shown
    }
    if (id == doneStep) {
        complete();
        fillSummary();
    }
    const std::vector<std::string> ids = checkIdsFor(id);
    std::vector<QString> rowIds;
    std::vector<QString> titles;
    const diag::EnvironmentContext context = m_options.environment();
    for (const diag::Check& check : diag::environmentChecks(context)) {
        if (std::ranges::find(ids, check.id) != ids.end()) {
            rowIds.push_back(toQString(check.id));
            titles.push_back(QCoreApplication::translate("hwio::diag", check.titleSourceText));
        }
    }
    m_checks->reset(rowIds, titles);
    if (!ids.empty()) {
        runChecks();
        return;
    }
    emit changed();
}

std::vector<std::string> SetupWizard::checkIdsFor(const QString& stepId) {
    if (stepId == jlinkStep) {
        return {"jlink-software"};
    }
    if (stepId == usbStep) {
        return {"usb-access"};
    }
    if (stepId == nrfUtilStep) {
        return {"nrfutil"};
    }
    if (stepId == probeStep) {
        return {"probes", "probe-firmware", "probe-holders"};
    }
    return {};
}

void SetupWizard::runChecks(std::optional<std::filesystem::path> candidate) {
    const QString id = stepId();
    std::vector<std::string> ids = checkIdsFor(id);
    if (ids.empty()) {
        return;
    }
    if (m_generation != 0) {
        m_app.diagWorker().cancel(m_generation);
    }
    diag::EnvironmentContext context = m_options.environment();
    if (candidate) {
        // The chosen path is checked as an override; it is saved only when the check passes.
        if (id == jlinkStep) {
            context.jlink.option = *candidate;
        } else {
            context.nrfutil.option = *candidate;
        }
    }
    m_candidate = std::move(candidate);
    m_candidateStep = id;
    // Owned by the request: written by the worker while it runs, read here after checksFinished.
    m_pendingFacts = std::make_shared<diag::EnvironmentFacts>();
    context.facts = m_pendingFacts;
    m_checks->setAllPending();
    m_generation = m_app.diagWorker().runChecks(DiagWorker::ChecksRequest{
        .context = std::move(context), .ids = std::move(ids), .requester = m_requester});
    emit changed();
}

void SetupWizard::onProgress(quint64 generation, const diag::CheckResult& result) {
    if (generation != m_generation || m_generation == 0) {
        return;
    }
    m_checks->update(result);
    emit changed();
}

void SetupWizard::onFinished(quint64 generation, const std::vector<diag::CheckResult>& results) {
    if (generation != m_generation || m_generation == 0) {
        return;
    }
    m_generation = 0;
    for (const diag::CheckResult& result : results) {
        m_checks->update(result);
    }
    m_facts = std::move(m_pendingFacts);
    m_pendingFacts.reset();
    if (m_candidate) {
        const std::filesystem::path candidate = *m_candidate;
        m_candidate.reset();
        const bool jlink = m_candidateStep == jlinkStep;
        const diag::CheckStatus result = status(jlink ? "jlink-software" : "nrfutil");
        if (result == diag::CheckStatus::Ok) {
            // savePath runs the step's checks again with the saved setting.
            savePath(jlink ? jlinkPathKey : nrfUtilPathKey, toQString(toUtf8(candidate)));
            return;
        }
        // Not saved; the check row shows why (plan 0008: a failed validation is not stored).
    }
    if (stepId() == probeStep && m_picker != nullptr) {
        m_probeSerials.clear();
        for (int row = 0; row < m_picker->rowCount(); ++row) {
            m_probeSerials.push_back(
                m_picker->data(m_picker->index(row), ProbePickerViewModel::SerialRole).toUInt());
        }
    }
    emit changed();
}

void SetupWizard::onProbesChanged() {
    if (stepId() != probeStep || m_picker == nullptr || !m_picker->scanned()) {
        return;
    }
    std::vector<std::uint32_t> serials;
    for (int row = 0; row < m_picker->rowCount(); ++row) {
        serials.push_back(
            m_picker->data(m_picker->index(row), ProbePickerViewModel::SerialRole).toUInt());
    }
    if (serials != m_probeSerials && m_generation == 0) {
        m_probeSerials = serials;
        runChecks(); // a probe was connected or removed
    }
}

void SetupWizard::choosePath(const QUrl& file) {
    const QString path = pathOf(file);
    const QString id = stepId();
    if (path.isEmpty() || settingsReadOnly() || (id != jlinkStep && id != nrfUtilStep)) {
        return;
    }
    m_pathError.clear();
    runChecks(std::filesystem::path(path.toStdU16String()));
}

void SetupWizard::savePath(const QString& key, const QString& path) {
    // DependencyStatus (Settings page, Flash prerequisites) re-checks on
    // SettingsStore::valueChanged.
    if (const Status saved = m_settings.setValue(key, path); !saved) {
        m_pathError = toQString(saved.error().message);
    } else {
        m_pathError.clear();
    }
    runChecks();
}

void SetupWizard::complete() {
    if (settingsReadOnly()) {
        return; // settings.newer_schema: the key cannot be written (the dialog shows a banner)
    }
    if (const Status saved = m_settings.setValue(completedVersionKey, AppViewModel::version());
        !saved) {
        qCWarning(lcGui) << "Setup: setup/completedVersion not saved:"
                         << toQString(saved.error().message);
    }
}

void SetupWizard::record() {
    const QString id = stepId();
    QString text;
    diag::CheckStatus result = diag::CheckStatus::Skipped;
    if (id == jlinkStep) {
        result = status("jlink-software");
        if (result == diag::CheckStatus::Ok && m_facts && m_facts->jlinkVersion != 0) {
            text = toQString(jlink::formatLibraryVersion(m_facts->jlinkVersion));
        }
    } else if (id == usbStep) {
        result = status("usb-access");
    } else if (id == nrfUtilStep) {
        result = status("nrfutil");
        if (result == diag::CheckStatus::Ok && m_facts) {
            text = toQString(m_facts->nrfUtilVersion);
        } else {
            result = diag::CheckStatus::Skipped; // optional: not passed = skipped
        }
    } else if (id == probeStep) {
        result = status("probes");
        QStringList serials;
        if (m_facts) {
            for (const diag::EnvironmentFacts::Probe& probe : m_facts->probes) {
                serials.append(QString::number(probe.info.serialNumber));
            }
        }
        text = serials.join(QStringLiteral(", "));
        if (serials.isEmpty()) {
            result = diag::CheckStatus::Skipped;
        }
    } else {
        return;
    }
    if (result == diag::CheckStatus::Pending || result == diag::CheckStatus::Running) {
        result = diag::CheckStatus::Skipped;
    }
    std::erase_if(m_records, [&id](const StepRecord& record) { return record.step == id; });
    m_records.push_back(StepRecord{.step = id, .status = result, .text = text});
}

void SetupWizard::fillSummary() {
    // ADR-0291: J-Link Software ✓ 8.10 · USB access ✓ · nRF Util – skipped · J-Link ✓ serials.
    std::vector<QString> ids;
    std::vector<QString> titles;
    for (const QString& id : m_steps) {
        if (id == jlinkStep) {
            titles.push_back(tr("J-Link Software"));
        } else if (id == usbStep) {
            titles.push_back(tr("USB access"));
        } else if (id == nrfUtilStep) {
            titles.push_back(tr("nRF Util"));
        } else if (id == probeStep) {
            titles.push_back(tr("J-Link"));
        } else {
            continue;
        }
        ids.push_back(id);
    }
    m_summary->reset(ids, titles);
    for (const QString& id : ids) {
        const auto it = std::ranges::find(m_records, id, &StepRecord::step);
        diag::CheckResult row{.id = toStdString(id),
                              .status = diag::CheckStatus::Skipped,
                              .title = {},
                              .result = {},
                              .error = std::nullopt};
        if (it != m_records.end()) {
            row.status = it->status;
            row.result = toStdString(it->text);
        } else {
            row.result = toStdString(tr("Not checked")); // started from an error at a later step
        }
        if (row.result.empty()) {
            switch (row.status) {
            case diag::CheckStatus::Ok:
                row.result = toStdString(tr("Ready"));
                break;
            case diag::CheckStatus::Skipped:
                row.result = toStdString(tr("Skipped"));
                break;
            default:
                row.result = toStdString(tr("Not ready"));
                break;
            }
        }
        m_summary->update(row);
    }
}

void SetupWizard::connectDevice() {
    // This one closes first (a tab's wizard is refused while the setup wizard is open, plan 0056
    // D-6), then the launcher opens connect-device on the current device tab (the launcher
    // outlives this wizard).
    QPointer<WizardLauncher> launcher = m_app.launcher();
    emit finished(true);
    QTimer::singleShot(0, launcher.data(), [launcher] {
        if (!launcher.isNull()) {
            (void)launcher->start(connectWizardId);
        }
    });
}

// ---- Welcome ------------------------------------------------------------------------------------

bool SetupWizard::modeChoice() const { return m_askMode && stepId() == welcomeStep; }

QVariantList SetupWizard::modes() { return UiModeController::modes(); }

void SetupWizard::selectMode(int mode) {
    if (!uiModeFromInt(mode).has_value() || mode == m_selectedMode) {
        return;
    }
    m_selectedMode = mode;
    emit changed();
}

void SetupWizard::applyMode() {
    if (!m_askMode) {
        return;
    }
    if (UiModeController* uiMode = m_app.uiMode(); uiMode != nullptr) {
        uiMode->chooseMode(m_selectedMode); // a failed write: UiModeController::writeFailed
    }
}

void SetupWizard::aboutToClose() {
    if (stepId() == welcomeStep) {
        applyMode(); // plan 0056 (D-4): ×, Esc, Cancel on Welcome
    }
}

QVariantList SetupWizard::languages() const {
    QVariantList list;
    for (const QVariant& entry : m_app.availableLanguages()) {
        const QVariantMap map = entry.toMap();
        const QString name = map.value(QStringLiteral("nativeName")).toString();
        list.append(
            QVariantMap{{QStringLiteral("code"), map.value(QStringLiteral("code"))},
                        {QStringLiteral("name"), name.isEmpty() ? tr("System default") : name}});
    }
    return list;
}

int SetupWizard::languageIndex() const {
    const QVariantList list = languages();
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (list.at(i).toMap().value(QStringLiteral("code")).toString() == m_app.language()) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void SetupWizard::selectLanguage(int index) {
    const QVariantList list = languages();
    if (index >= 0 && index < list.size()) {
        const QString code = list.at(index).toMap().value(QStringLiteral("code")).toString();
        if (code != m_app.language()) {
            m_app.setLanguage(code); // the same setting as Settings → General (ui/language)
        }
    }
}

bool SetupWizard::showAtStartChoice() const {
    return m_options.mode == Mode::JLinkNeeded && stepId() == welcomeStep;
}

bool SetupWizard::showAtStart() const { return m_settings.value(showAtStartKey).toBool(); }

void SetupWizard::setShowAtStart(bool show) {
    if (show != showAtStart()) {
        if (const Status saved = m_settings.setValue(showAtStartKey, show); !saved) {
            qCWarning(lcGui) << "Setup: setup/showAtStart not saved:"
                             << toQString(saved.error().message);
        }
    }
}

bool SetupWizard::settingsReadOnly() const { return m_settings.readOnly(); }

// ---- the step control --------------------------------------------------------------------------

bool SetupWizard::customJLinkPath() const {
    return !m_settings.value(jlinkPathKey).toString().isEmpty();
}

QString SetupWizard::statusText() const {
    if (m_generation != 0) {
        return {};
    }
    const QString id = stepId();
    if (id == jlinkStep && stepPassed() && m_facts && m_facts->jlinkLibrary) {
        return tr("J-Link Software %1 found in %2.")
            .arg(toQString(jlink::formatLibraryVersion(m_facts->jlinkVersion)),
                 toQString(toUtf8(m_facts->jlinkLibrary->parent_path())));
    }
    if (id == usbStep) {
        switch (status("usb-access")) {
        case diag::CheckStatus::Ok:
            return tr("Your user can access the J-Link.");
        case diag::CheckStatus::Skipped:
            return tr("Connect the J-Link to check USB access.");
        default:
            return {};
        }
    }
    if (id == nrfUtilStep) {
        if (stepPassed() && m_facts) {
            return tr("nRF Util %1 with device command %2.")
                .arg(toQString(m_facts->nrfUtilVersion), toQString(m_facts->nrfUtilDeviceVersion));
        }
        const auto code = errorCode("nrfutil");
        if (code == ErrorCode::DependencyNrfUtilTooOld ||
            code == ErrorCode::DependencyNrfUtilDeviceMissing) {
            return tr("Minimum versions: nRF Util %1, device command %2.")
                .arg(toQString(nrfutil::formatVersion(nrfutil::minimumCoreVersion)),
                     toQString(nrfutil::formatVersion(nrfutil::minimumDeviceVersion)));
        }
    }
    return {};
}

QStringList SetupWizard::commands() const {
    if (m_generation != 0) {
        return {};
    }
    const QString id = stepId();
    QStringList list;
    if (id == usbStep && (status("usb-access") == diag::CheckStatus::Failed ||
                          status("usb-access") == diag::CheckStatus::Warning)) {
        for (const std::string& command : diag::jlinkUdevCommands()) {
            list.append(toQString(command)); // ADR-0185: the uaccess rule
        }
    } else if (id == nrfUtilStep && !stepPassed()) {
        switch (errorCode("nrfutil").value_or(ErrorCode::DependencyNrfUtilNotFound)) {
        case ErrorCode::DependencyNrfUtilTooOld:
            list = {QStringLiteral("nrfutil self-upgrade"),
                    QStringLiteral("nrfutil upgrade device")};
            break;
        default: // not found, device command missing
            list = {QStringLiteral("nrfutil install device")};
            break;
        }
    }
    return list;
}

QVariantList SetupWizard::buttons() const {
    QVariantList list;
    if (m_generation != 0 || settingsReadOnly()) {
        return list; // read-only settings: paths cannot be saved
    }
    const QString id = stepId();
    if ((id == jlinkStep || id == nrfUtilStep) && !stepPassed()) {
        list.append(button(chooseAction, tr("It is installed elsewhere…"), true));
    }
    if (id == jlinkStep && !stepPassed() && customJLinkPath() &&
        errorCode("jlink-software") == ErrorCode::DependencyJLinkLoadFailed) {
        list.append(button(automaticAction, tr("Use automatic detection")));
    }
    return list;
}

QString SetupWizard::fileDialogTitle() const {
    return stepId() == nrfUtilStep ? tr("Choose the nrfutil program")
                                   : tr("Choose the JLinkARM library");
}

QStringList SetupWizard::fileNameFilters() const {
    QString pattern;
    if (stepId() == nrfUtilStep) {
        pattern = m_options.platform == Platform::Windows ? QStringLiteral("nrfutil.exe")
                                                          : QStringLiteral("nrfutil");
        return {tr("nRF Util (%1)").arg(pattern), tr("All files (*)")};
    }
    switch (m_options.platform) {
    case Platform::Linux:
        pattern = QStringLiteral("libjlinkarm.so*");
        break;
    case Platform::MacOS:
        pattern = QStringLiteral("libjlinkarm.dylib");
        break;
    case Platform::Windows:
        pattern = QStringLiteral("JLink_x64.dll");
        break;
    }
    return {tr("J-Link library (%1)").arg(pattern), tr("All files (*)")};
}

// ---- registration -------------------------------------------------------------------------------

void registerSetupWizard(AppViewModel& app) {
    const std::array keys{
        SettingKey{.path = completedVersionKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(64)},
        SettingKey{
            .path = showAtStartKey, .type = QMetaType::Bool, .defaultValue = true, .validator = {}},
    };
    app.settingsStore()->registerKeys(keys);
    (void)app.wizardRegistry().add(WizardDescriptor{
        .id = WizardId::FirstRunSetup,
        .perTab = false,
        // Qt parent ownership: the WizardViewModel.
        .create = [&app](QObject* parent) -> Wizard* {
            return new SetupWizard(app, {}, parent); // NOLINT(cppcoreguidelines-owning-memory)
        }});
    (void)app.wizardRegistry().addEmbedded(
        setupStepControl,
        QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/wizard/SetupStep.qml")));
}

} // namespace hwio::gui
