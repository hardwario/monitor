#include "wizard/connect_wizard.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/device/device_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/diag/check.h"
#include "hwio/diag/environment.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/probe_picker_view_model.h"
#include "wizard/check_list_model.h"
#include "wizard/diagnose_runner.h"
#include "wizard/probe_picker_step.h"
#include "wizard/wizard.h"
#include "wizard/wizard_registry.h"
#include "workers/diag_worker.h"

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hwio::gui {

namespace {

const QString openWizardPrefix = QStringLiteral("open-wizard:");
const QString lteUpdateId = QStringLiteral("lte-update");
const QString flashWizardId = QStringLiteral("flash-firmware");
const QString checkAgainAction = QStringLiteral("check-again");
const QString showPowerAction = QStringLiteral("show-power");
const QString showCableAction = QStringLiteral("show-cable");
const QString recoverAction = QStringLiteral("open-flash-page");
const QString helpAction = QStringLiteral("help-me-fix-this");

QVariantMap button(const QString& id, const QString& text, bool enabled = true) {
    return QVariantMap{{QStringLiteral("id"), id},
                       {QStringLiteral("text"), text},
                       {QStringLiteral("enabled"), enabled}};
}

// The environment checks of the Probe step (plan 0009 step 2; usb-access on Linux only).
std::vector<std::string> probeCheckIds() {
#ifdef Q_OS_LINUX
    return {"usb-access", "probe-holders", "probes"};
#else
    return {"probe-holders", "probes"};
#endif
}

bool hasError(const diag::CheckResult* row, ErrorCode code) {
    return row != nullptr && row->error && row->error->code == code;
}

bool failed(const diag::CheckResult* row) {
    return row != nullptr && row->status == diag::CheckStatus::Failed;
}

} // namespace

ConnectWizard::ConnectWizard(AppViewModel& app, QObject* parent)
    : Wizard(parent), m_app(app),
      m_probeChecks(new CheckListModel(this)),       // NOLINT(cppcoreguidelines-owning-memory)
      m_checks(new CheckListModel(this)),            // NOLINT(cppcoreguidelines-owning-memory)
      m_runner(new DiagnoseRunner(wizardId, this)) { // NOLINT(cppcoreguidelines-owning-memory)
    connect(m_checks, &CheckListModel::rowsChanged, this, &Wizard::changed);
    connect(m_probeChecks, &CheckListModel::rowsChanged, this, &Wizard::changed);
    connect(m_runner, &DiagnoseRunner::changed, this, &Wizard::changed);
    DiagWorker& worker = m_app.diagWorker();
    connect(&worker, &DiagWorker::checkProgress, this,
            [this](quint64 generation, const diag::CheckResult& result) {
                if (generation != 0 && generation == m_probeCheckRun) {
                    m_probeChecks->update(result);
                }
            });
    connect(&worker, &DiagWorker::checksFinished, this,
            [this](quint64 generation, const std::vector<diag::CheckResult>& results) {
                if (generation == 0 || generation != m_probeCheckRun) {
                    return;
                }
                for (const diag::CheckResult& result : results) {
                    m_probeChecks->update(result);
                }
                m_probeCheckRun = 0;
                emit changed();
            });
    connect(&m_app, &AppViewModel::languageChanged, this, &Wizard::changed);
}

ConnectWizard::~ConnectWizard() {
    if (m_probeCheckRun != 0) {
        m_app.diagWorker().cancel(m_probeCheckRun);
    }
}

QString ConnectWizard::title() const { return tr("Connect a device"); }

std::optional<ConnectWizard::Step> ConnectWizard::startStepFor(ErrorCode code) {
    switch (code) {
    case ErrorCode::ProbeNotFound:
        return Probe;
    case ErrorCode::TargetNoPower:
        return Power;
    case ErrorCode::TargetConnectFailed:
    case ErrorCode::TargetWrongFamily:
        return Cable;
    case ErrorCode::TargetNotConnected:
        return Check;
    default:
        return std::nullopt;
    }
}

// ---- start --------------------------------------------------------------------------------------

void ConnectWizard::start(const ErrorInfo& context, DeviceTabViewModel* tab) {
    m_tab = tab;
    m_runner->setTab(tab);
    if (m_tab.isNull()) {
        return;
    }
    connect(m_tab, &DeviceTabViewModel::stateChanged, this, &Wizard::changed);
    connect(m_tab, &DeviceTabViewModel::sessionChanged, this, &Wizard::changed);
    connect(m_tab, &DeviceTabViewModel::busyChanged, this, &Wizard::changed);
    buildFlow();
    std::optional<Step> startStep;
    if (context.valid()) {
        if (const std::optional<ErrorCode> code = errorCodeFromId(toStdString(context.code()));
            code) {
            startStep = startStepFor(*code);
        }
    }
    if (!hasProbe()) {
        startStep = Probe;
    } else if (!startStep) {
        if (m_tab->state() == DeviceTabViewModel::Connected) {
            m_pureCheck = true; // steps 1–4 are skipped: a pure check
            startStep = Check;
        } else {
            startStep = Cable; // ADR-0074: the probe step is satisfied
        }
    }
    goToStep(*startStep);
}

void ConnectWizard::buildFlow() {
    m_flow.clear();
    m_flow.push_back(Flow{.step = Probe});
    const device::TargetProfile* consoleTarget = target();
    const std::size_t guides = consoleTarget != nullptr
                                   ? std::max<std::size_t>(consoleTarget->connectionGuide.size(), 1)
                                   : 1;
    for (std::size_t i = 0; i < guides; ++i) {
        m_flow.push_back(Flow{.step = Cable, .guide = i});
    }
    m_flow.push_back(Flow{.step = Power});
    m_flow.push_back(Flow{.step = Check});
    m_flow.push_back(Flow{.step = Done});
}

bool ConnectWizard::hasProbe() const {
    return !m_tab.isNull() && !m_tab->choosing() && !m_tab->isBle() &&
           m_tab->probeSerial().has_value();
}

const device::DeviceProfile* ConnectWizard::profile() const {
    if (!m_tab.isNull()) {
        if (const device::DeviceProfile* found =
                device::ProfileRegistry::find(toStdString(m_tab->profileId()));
            found != nullptr) {
            return found;
        }
    }
    if (m_picker != nullptr) {
        if (const device::DeviceProfile* found =
                device::ProfileRegistry::find(toStdString(m_picker->profileId()));
            found != nullptr) {
            return found;
        }
    }
    const auto all = device::ProfileRegistry::all();
    return all.empty() ? nullptr : all.front();
}

QString ConnectWizard::targetId() const {
    // ADR-0233 / ADR-0249: the console target (CHESTER app).
    const device::DeviceProfile* deviceProfile = profile();
    return deviceProfile != nullptr ? toQString(device::consoleTargetId(*deviceProfile))
                                    : QStringLiteral("app");
}

const device::TargetProfile* ConnectWizard::target() const {
    const device::DeviceProfile* deviceProfile = profile();
    return deviceProfile != nullptr ? device::findTarget(*deviceProfile, toStdString(targetId()))
                                    : nullptr;
}

ConnectWizard::Step ConnectWizard::step() const {
    return m_index < m_flow.size() ? m_flow.at(m_index).step : Probe;
}

// ---- steps --------------------------------------------------------------------------------------

std::vector<WizardStep> ConnectWizard::plannedSteps() const {
    std::vector<WizardStep> steps;
    const device::TargetProfile* consoleTarget = target();
    for (const Flow& flow : m_flow) {
        WizardStep step;
        switch (flow.step) {
        case Probe:
            step.id = QStringLiteral("probe");
            step.title = tr("Probe");
            break;
        case Cable:
            step.id = QStringLiteral("cable-%1").arg(flow.guide + 1);
            step.title =
                consoleTarget != nullptr && flow.guide < consoleTarget->connectionGuide.size()
                    ? device::guideStepTitle(consoleTarget->connectionGuide[flow.guide])
                    : tr("Cable");
            break;
        case Power:
            step.id = QStringLiteral("power");
            step.title = tr("Power");
            break;
        case Check:
            step.id = QStringLiteral("check");
            step.title = tr("Check");
            break;
        case Done:
            step.id = QStringLiteral("done");
            step.title = tr("Done");
            break;
        }
        steps.push_back(step);
    }
    return steps;
}

WizardStep ConnectWizard::current() const {
    if (m_index >= m_flow.size()) {
        return {};
    }
    WizardStep step = plannedSteps().at(m_index);
    const Flow& flow = m_flow.at(m_index);
    switch (flow.step) {
    case Probe:
        step.kind = StepKind::Embedded;
        step.embedded = probePickerStepName;
        step.heading = tr("Choose the J-Link");
        step.body = tr("Connect the J-Link to your computer with a USB cable.");
        step.illustration = QStringLiteral("jlink-usb");
        step.primaryText = tr("Next");
        if (m_picker != nullptr && m_picker->scanned() && m_picker->count() == 0) {
            step.actions =
                QVariantList{button(checkAgainAction, tr("Check again"), m_probeCheckRun == 0)};
        }
        break;
    case Cable: {
        step.kind = StepKind::Info;
        step.heading = tr("Connect the J-Link to the device");
        const device::TargetProfile* consoleTarget = target();
        if (consoleTarget != nullptr && flow.guide < consoleTarget->connectionGuide.size()) {
            const device::ConnectionGuideStep& guide = consoleTarget->connectionGuide[flow.guide];
            step.illustration = toQString(guide.illustrationSlot);
            step.body = device::guideStepBody(guide);
        }
        step.primaryText = tr("Next");
        break;
    }
    case Power: {
        step.kind = StepKind::Info;
        step.heading = tr("Power the device");
        step.illustration = QStringLiteral("chester-power");
        if (const device::DeviceProfile* deviceProfile = profile(); deviceProfile != nullptr) {
            step.body = device::powerHint(*deviceProfile);
        }
        step.primaryText = tr("Next");
        break;
    }
    case Check:
        fillCheck(step);
        break;
    case Done:
        fillDone(step);
        break;
    }
    return step;
}

void ConnectWizard::fillCheck(WizardStep& step) const {
    step.kind = StepKind::Checks;
    step.heading = tr("Checking the connection");
    QVariantList actions;
    switch (outcome()) {
    case Outcome::Running:
        step.primaryText = tr("Connect");
        break;
    case Outcome::Passed:
        step.heading = tr("The device answers");
        step.primaryText = tr("Connect");
        break;
    case Outcome::Protected:
        step.body = tr("The MCU is read-back protected. Recover removes the protection but erases "
                       "the MCU completely.");
        actions.append(button(recoverAction, tr("Open Flash page")));
        step.primaryText = tr("Connect");
        break;
    case Outcome::NoConsole:
        step.body = tr("The device has no console. It may need firmware with the CHESTER shell.");
        if (m_app.wizardRegistry().has(flashWizardId)) {
            actions.append(button(flashWizardId, tr("Flash firmware…")));
        }
        step.primaryText = tr("Connect anyway");
        break;
    case Outcome::WrongConnector:
        step.body = tr("The J-Link is on the LTE connector. Move it to the APP connector.");
        actions.append(button(showCableAction, tr("Show me how")));
        if (m_app.wizardRegistry().has(lteUpdateId)) {
            actions.append(button(lteUpdateId, tr("Update LTE firmware…")));
        }
        step.primaryText = tr("Check again");
        break;
    case Outcome::NoPower:
        actions.append(button(showPowerAction, tr("Show me how")));
        step.primaryText = tr("Check again");
        break;
    case Outcome::ConnectFailed:
        actions.append(button(showCableAction, tr("Show me how")));
        step.primaryText = tr("Check again");
        break;
    case Outcome::OtherFailed:
        if (const std::optional<ErrorInfo> error = failedRowError();
            error && !otherWizardFor(*error).isEmpty()) {
            actions.append(button(helpAction, tr("Help me fix this")));
        }
        step.primaryText = tr("Check again");
        break;
    }
    if (outcome() == Outcome::Protected || outcome() == Outcome::NoConsole) {
        actions.append(button(checkAgainAction, tr("Check again")));
    }
    step.actions = actions;
}

void ConnectWizard::fillDone(WizardStep& step) const {
    if (connectFailed()) {
        step.kind = StepKind::Info;
        step.heading = tr("Could not connect");
        step.embedded = QStringLiteral("wizard-error");
        step.primaryText = tr("Check again");
        return;
    }
    if (m_tab.isNull() || m_tab->state() != DeviceTabViewModel::Connected) {
        step.kind = StepKind::Info;
        step.heading = tr("Connecting…");
        return;
    }
    step.kind = StepKind::Done;
    const device::DeviceProfile* deviceProfile = profile();
    const QString device =
        deviceProfile != nullptr ? toQString(deviceProfile->displayName) : m_tab->profileId();
    const QString serial =
        m_tab->probeSerial() ? QString::number(*m_tab->probeSerial()) : QString();
    // Runtime data: the heading is plain text (ADR-0184).
    step.heading = tr("Connected to %1 %2 · %3.").arg(device, m_tab->consoleTargetId(), serial);
    step.primaryText = tr("Open console");
}

bool ConnectWizard::connectFailed() const {
    if (step() != Done || !m_connectRequested || m_tab.isNull()) {
        return false;
    }
    const auto state = m_tab->state();
    if (state == DeviceTabViewModel::Connected || state == DeviceTabViewModel::Connecting) {
        return false;
    }
    return m_tab->hasError() || m_tab->session() == nullptr ||
           m_tab->session()->connection().state != device::ConnectionState::ProbeOpen;
}

ErrorInfo ConnectWizard::shownError() const {
    if (connectFailed() && !m_tab.isNull() && m_tab->connection() != nullptr) {
        return m_tab->connection()->error();
    }
    return {};
}

CheckListModel* ConnectWizard::checks() {
    switch (step()) {
    case Probe:
        return m_probeChecks;
    case Check:
        return m_checks;
    default:
        return nullptr;
    }
}

QObject* ConnectWizard::embeddedViewModel() {
    if (step() == Probe) {
        return m_picker;
    }
    return this;
}

// ---- navigation ---------------------------------------------------------------------------------

void ConnectWizard::goToStep(Step step) {
    for (std::size_t i = 0; i < m_flow.size(); ++i) {
        if (m_flow.at(i).step == step) {
            goTo(i);
            return;
        }
    }
}

void ConnectWizard::goTo(std::size_t index) {
    m_index = index;
    if (m_picker != nullptr) {
        m_picker->setActive(step() == Probe); // the scanner runs only while the step is shown
    }
    if (step() == Probe) {
        m_probeStepShown = true;
        enterProbe();
    } else if (step() == Check) {
        runCheck();
    }
    emit changed();
}

void ConnectWizard::enterProbe() {
    if (!m_tab.isNull() && !m_tab->choosing() && !m_tab->isBle()) {
        // The tab's probe would show as used by this tab: the tab returns to its picker first.
        m_preferSerial = m_tab->probeSerial();
        m_tab->chooseAnotherProbe();
    }
    if (m_picker == nullptr) {
        // Qt parent ownership: this wizard.
        m_picker = m_app.makeProbePicker(this);
        connect(m_picker, &ProbePickerViewModel::selectedRowChanged, this, &Wizard::changed);
        connect(m_picker, &ProbePickerViewModel::countChanged, this, [this] {
            preselect();
            // No probe (any more): the checks explain why (plan 0009 step 2).
            if (step() == Probe && m_picker->scanned() && m_picker->count() == 0) {
                runProbeChecks();
            }
            emit changed();
        });
        connect(m_picker, &ProbePickerViewModel::scanChanged, this, [this] {
            preselect();
            if (step() == Probe && m_picker->scanned() && m_picker->count() == 0 &&
                m_probeChecks->count() == 0) {
                runProbeChecks();
            }
            emit changed();
        });
    }
    m_picker->setActive(true);
    preselect();
}

void ConnectWizard::preselect() {
    if (m_picker == nullptr || !m_preferSerial) {
        return;
    }
    const QString serial = QString::number(*m_preferSerial);
    for (int row = 0; row < m_picker->count(); ++row) {
        const QModelIndex index = m_picker->index(row);
        if (m_picker->data(index, ProbePickerViewModel::SerialRole).toString() == serial &&
            m_picker->data(index, ProbePickerViewModel::SelectableRole).toBool()) {
            m_picker->setSelectedRow(row);
            return;
        }
    }
}

void ConnectWizard::runProbeChecks() {
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
    m_probeCheckRun =
        m_app.diagWorker().runChecks(DiagWorker::ChecksRequest{.context = context, .ids = ids});
    emit changed();
}

void ConnectWizard::runCheck() {
    m_connectRequested = false;
    (void)m_runner->start({}, m_checks); // all six Diagnose steps (ADR-0069)
}

bool ConnectWizard::running() const { return m_runner->running() || m_probeCheckRun != 0; }

bool ConnectWizard::canPrimary() const {
    if (m_tab.isNull()) {
        return false;
    }
    switch (step()) {
    case Probe:
        return m_picker != nullptr && m_picker->selectedSerial().has_value();
    case Cable:
    case Power:
        return true;
    case Check:
        return outcome() != Outcome::Running;
    case Done:
        return connectFailed() || m_tab->state() == DeviceTabViewModel::Connected;
    }
    return false;
}

void ConnectWizard::primary() {
    if (!canPrimary()) {
        return;
    }
    switch (step()) {
    case Probe: {
        const std::optional<std::uint32_t> serial = m_picker->selectedSerial();
        const QString product =
            m_picker
                ->data(m_picker->index(m_picker->selectedRow()), ProbePickerViewModel::ProductRole)
                .toString();
        if (!serial ||
            !m_tab->configure(m_picker->profileId(), m_picker->targetId(), *serial, product)) {
            return;
        }
        goTo(m_index + 1);
        break;
    }
    case Cable:
    case Power:
        goTo(m_index + 1);
        break;
    case Check:
        switch (outcome()) {
        case Outcome::Passed:
        case Outcome::Protected:
        case Outcome::NoConsole:
            connectTab();
            break;
        default:
            runCheck(); // Check again
            emit changed();
            break;
        }
        break;
    case Done:
        if (connectFailed()) {
            goToStep(Check);
        } else {
            handOffAndClose(QStringLiteral("open-console"));
        }
        break;
    }
}

void ConnectWizard::connectTab() {
    // [Connect]: the probe-only session is promoted (startConnection with RTT; the probe and its
    // lock stay held); a connected tab stays as it is.
    m_runner->keepSession();
    m_connectRequested = true;
    goToStep(Done);
    if (m_tab->state() != DeviceTabViewModel::Connected) {
        m_tab->connectDevice();
    }
    emit changed();
}

bool ConnectWizard::canBack() const {
    if (m_pureCheck || running() || m_index == 0) {
        return false;
    }
    if (step() == Done || step() == Probe) {
        return false;
    }
    // Back to the Probe step only when it was shown (the tab had no probe).
    return m_flow.at(m_index - 1).step != Probe || m_probeStepShown;
}

void ConnectWizard::back() {
    if (!canBack()) {
        return;
    }
    if (step() == Check) {
        m_runner->cancel();
    }
    goTo(m_index - 1);
}

// ---- Check outcome ------------------------------------------------------------------------------

ConnectWizard::Outcome ConnectWizard::outcome() const {
    if (m_runner->running() || m_checks->count() == 0) {
        return Outcome::Running;
    }
    const auto row = [this](const char* id) { return m_checks->row(QString::fromLatin1(id)); };
    for (const char* id : {"connect", "identify"}) {
        if (hasError(row(id), ErrorCode::TargetWrongFamily) ||
            hasError(row(id), ErrorCode::TargetOtherConnector)) {
            return Outcome::WrongConnector;
        }
    }
    if (failed(row("power")) || hasError(row("power"), ErrorCode::TargetNoPower)) {
        return Outcome::NoPower;
    }
    if (failed(row("connect"))) {
        return Outcome::ConnectFailed;
    }
    if (failed(row("probe")) || m_runner->startError()) {
        return Outcome::OtherFailed;
    }
    if (hasError(row("identify"), ErrorCode::TargetProtected) ||
        hasError(row("connect"), ErrorCode::TargetProtected)) {
        return Outcome::Protected;
    }
    if (failed(row("rtt")) || failed(row("rtt-buffers"))) {
        return Outcome::NoConsole;
    }
    if (m_checks->anyFailed()) {
        return Outcome::OtherFailed;
    }
    return Outcome::Passed;
}

std::optional<ErrorInfo> ConnectWizard::failedRowError() const {
    for (int i = 0; i < m_checks->count(); ++i) {
        const QString id = m_checks->data(m_checks->index(i), CheckListModel::IdRole).toString();
        const diag::CheckResult* row = m_checks->row(id);
        if (row != nullptr && row->status == diag::CheckStatus::Failed && row->error) {
            return ErrorInfo(*row->error);
        }
    }
    return std::nullopt;
}

QString ConnectWizard::otherWizardFor(const ErrorInfo& error) const {
    QString wizard = error.wizard();
    if (wizard.isEmpty() || wizard == wizardId || !m_app.wizardRegistry().has(wizard)) {
        return {};
    }
    return wizard;
}

// ---- actions ------------------------------------------------------------------------------------

void ConnectWizard::action(const QString& actionId) {
    if (m_tab.isNull()) {
        return;
    }
    if (actionId == checkAgainAction) {
        if (step() == Probe) {
            m_picker->refresh();
            m_probeChecks->reset({}, {});
            runProbeChecks();
        } else if (step() == Check) {
            runCheck();
        }
        emit changed();
    } else if (actionId == showPowerAction) {
        goToStep(Power);
    } else if (actionId == showCableAction) {
        goToStep(Cable);
    } else if (actionId == recoverAction) {
        // target.protected (plan 0033, ADR-0236): [Recover…] shown once on the Flash page.
        handOffAndClose(QStringLiteral("open-flash-page:recover:") + targetId());
    } else if (actionId == lteUpdateId) {
        handOffAndClose(openWizardPrefix + lteUpdateId);
    } else if (actionId == flashWizardId) {
        handOffAndClose(openWizardPrefix + flashWizardId);
    } else if (actionId == helpAction) {
        // ADR-0297: e.g. probe.open_failed → the troubleshooting topic `probe-busy`.
        if (const std::optional<ErrorInfo> error = failedRowError();
            error && !otherWizardFor(*error).isEmpty()) {
            m_runner->keepSession();
            emit handOffError(*error);
            emit finished(false);
        }
    }
}

void ConnectWizard::handOffAndClose(const QString& action) {
    m_runner->keepSession();
    emit handOff(action);
    emit finished(action == QStringLiteral("open-console"));
}

QString ConnectWizard::busyCancelText() const {
    return m_runner->running() ? tr("The connection is being checked. Cancel it and close the "
                                    "wizard?")
                               : QString();
}

void ConnectWizard::cancelOperation() {
    if (m_probeCheckRun != 0) {
        m_app.diagWorker().cancel(m_probeCheckRun);
        m_probeCheckRun = 0;
    }
    m_runner->cancel();
}

} // namespace hwio::gui
