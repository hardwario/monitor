#include "wizard/troubleshooting_wizard.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/diag/check.h"
#include "hwio/diag/environment.h"
#include "hwio/hw/jlink/types.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/probe_firmware_view_model.h"
#include "viewmodels/probe_picker_view_model.h"
#include "wizard/check_list_model.h"
#include "wizard/connect_wizard.h"
#include "wizard/diagnose_runner.h"
#include "wizard/probe_picker_step.h"
#include "wizard/troubleshooting_topics.h"
#include "wizard/wizard.h"
#include "wizard/wizard_registry.h"
#include "workers/diag_worker.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QObject>
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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

const QString openWizardPrefix = QStringLiteral("open-wizard:");
const QString connectWizardId = QStringLiteral("connect-device");
const QString flashWizardId = QStringLiteral("flash-firmware");
const QString lteUpdateId = QStringLiteral("lte-update");
const QString setupWizardId = QStringLiteral("first-run-setup");
const QString checkAgainAction = QStringLiteral("check-again");
const QString setupAction = QStringLiteral("setup");
const QString reportAction = QStringLiteral("report");
const QString firmwarePrefix = QStringLiteral("probe-firmware:");
const QString stepAction = QStringLiteral("step-action");

QString tt(const char* sourceText) {
    return QCoreApplication::translate("Troubleshooting", sourceText);
}

QVariantMap button(const QString& id, const QString& text, bool enabled = true) {
    return QVariantMap{{QStringLiteral("id"), id},
                       {QStringLiteral("text"), text},
                       {QStringLiteral("enabled"), enabled}};
}

} // namespace

TroubleshootingWizard::TroubleshootingWizard(AppViewModel& app, QObject* parent)
    : Wizard(parent), m_app(app),
      m_checks(new CheckListModel(this)),            // NOLINT(cppcoreguidelines-owning-memory)
      m_runner(new DiagnoseRunner(wizardId, this)) { // NOLINT(cppcoreguidelines-owning-memory)
    connect(m_checks, &CheckListModel::rowsChanged, this, &Wizard::changed);
    connect(m_runner, &DiagnoseRunner::changed, this, &Wizard::changed);
    connect(m_runner, &DiagnoseRunner::finished, this, [this] {
        if (lteConnector()) {
            m_wrongFamily = true;
        }
        if (const std::optional<Status>& reset = m_runner->resetResult();
            reset && !m_tab.isNull() && topicStep() != nullptr &&
            topicStep()->action == actionReset) {
            m_tab->showToast(*reset ? tr("The device was reset.")
                                    : ErrorInfo(reset->error()).message());
        }
        emit changed();
    });
    DiagWorker& worker = m_app.diagWorker();
    connect(&worker, &DiagWorker::checkProgress, this, &TroubleshootingWizard::onEnvProgress);
    connect(&worker, &DiagWorker::checksFinished, this, &TroubleshootingWizard::onEnvFinished);
    // Review 0009 F7: a request replaced by another one (the start-up check) runs again.
    connect(&worker, &DiagWorker::requestDropped, this, [this](quint64 generation) {
        if (generation != 0 && generation == m_generation) {
            m_generation = 0;
            runCurrent();
        }
    });
    connect(&m_app, &AppViewModel::languageChanged, this, &Wizard::changed);
    // ADR-0291: the firmware actions follow the tabs and the update's state.
    connect(&m_app, &AppViewModel::probeUsageChanged, this, &Wizard::changed);
    if (ProbeFirmwareViewModel* update = m_app.probeFirmware(); update != nullptr) {
        connect(update, &ProbeFirmwareViewModel::changed, this, &Wizard::changed);
    }
}

TroubleshootingWizard::~TroubleshootingWizard() {
    if (m_generation != 0) {
        m_app.diagWorker().cancel(m_generation);
    }
}

QString TroubleshootingWizard::title() const { return tr("Troubleshooting"); }

// ---- start and flow -----------------------------------------------------------------------------

void TroubleshootingWizard::start(const ErrorInfo& context, DeviceTabViewModel* tab) {
    m_tab = tab;
    m_context = context;
    m_runner->setTab(tab);
    if (!m_tab.isNull()) {
        connect(m_tab, &DeviceTabViewModel::sessionChanged, this, &Wizard::changed);
        connect(m_tab, &DeviceTabViewModel::busyChanged, this, &Wizard::changed);
    }
    const Topic* topic = nullptr;
    if (m_context.valid()) {
        if (const std::optional<ErrorCode> code = errorCodeFromId(toStdString(m_context.code()));
            code) {
            topic = topicFor(*code);
            m_wrongFamily = *code == ErrorCode::TargetWrongFamily;
        }
    }
    if (topic == nullptr) {
        m_context = ErrorInfo(); // an error without a topic: the topic list
    }
    setTopic(topic);
    enter();
}

void TroubleshootingWizard::setTopic(const Topic* topic) {
    m_topic = topic;
    m_flow.clear();
    if (topic == nullptr) {
        m_flow.push_back(Flow{.kind = Kind::Choice});
    } else {
        if (m_context.valid()) {
            m_flow.push_back(Flow{.kind = Kind::Intro});
        } else {
            m_flow.push_back(Flow{.kind = Kind::Choice});
        }
        m_bleTab = !m_tab.isNull() && m_tab->isBle();
        bool pickerInserted = false;
        for (std::size_t i = 0; i < topic->steps.size(); ++i) {
            // ADR-0299: a tab without a probe chooses one before the first Diagnose step.
            if (topic->steps[i].kind == TopicStepKind::AutoDiagnose && !pickerInserted &&
                !hasProbe() && !m_bleTab) {
                m_flow.push_back(Flow{.kind = Kind::Picker});
                pickerInserted = true;
            }
            m_flow.push_back(Flow{.kind = Kind::TopicStep, .topicStep = i});
        }
        m_flow.push_back(Flow{.kind = Kind::End});
    }
    m_index = 0;
}

bool TroubleshootingWizard::hasProbe() const {
    return !m_tab.isNull() && !m_tab->choosing() && !m_tab->isBle() &&
           m_tab->probeSerial().has_value();
}

const device::DeviceProfile* TroubleshootingWizard::profile() const {
    if (!m_tab.isNull()) {
        if (const device::DeviceProfile* found =
                device::ProfileRegistry::find(toStdString(m_tab->profileId()));
            found != nullptr) {
            return found;
        }
    }
    const auto all = device::ProfileRegistry::all();
    return all.empty() ? nullptr : all.front();
}

QString TroubleshootingWizard::targetId() const {
    if (!m_tab.isNull() && !m_tab->consoleTargetId().isEmpty()) {
        return m_tab->consoleTargetId();
    }
    const device::DeviceProfile* deviceProfile = profile();
    return deviceProfile != nullptr ? toQString(device::consoleTargetId(*deviceProfile))
                                    : QStringLiteral("app");
}

const TopicStep* TroubleshootingWizard::topicStep() const {
    if (m_topic == nullptr || m_index >= m_flow.size() ||
        m_flow.at(m_index).kind != Kind::TopicStep) {
        return nullptr;
    }
    return &m_topic->steps[m_flow.at(m_index).topicStep];
}

QString TroubleshootingWizard::stepId() const {
    if (m_index >= m_flow.size()) {
        return {};
    }
    const Flow& flow = m_flow.at(m_index);
    switch (flow.kind) {
    case Kind::Choice:
        return QStringLiteral("choice");
    case Kind::Intro:
        return QStringLiteral("intro");
    case Kind::Picker:
        return QStringLiteral("probe");
    case Kind::TopicStep:
        return QStringLiteral("step-%1").arg(flow.topicStep + 1);
    case Kind::End:
        return QStringLiteral("end");
    }
    return {};
}

void TroubleshootingWizard::enter() {
    if (m_picker != nullptr) {
        m_picker->setActive(m_index < m_flow.size() && m_flow.at(m_index).kind == Kind::Picker);
    }
    if (m_index < m_flow.size() && m_flow.at(m_index).kind == Kind::Picker && m_picker == nullptr) {
        // Qt parent ownership: this wizard.
        m_picker = m_app.makeProbePicker(this);
        connect(m_picker, &ProbePickerViewModel::selectedRowChanged, this, &Wizard::changed);
        connect(m_picker, &ProbePickerViewModel::countChanged, this, &Wizard::changed);
        m_picker->setActive(true);
    }
    m_checks->reset({}, {});
    runCurrent();
    emit changed();
}

void TroubleshootingWizard::runCurrent() {
    const TopicStep* step = topicStep();
    if (step == nullptr) {
        return;
    }
    if (step->kind == TopicStepKind::AutoEnvironment) {
        runEnvironment(*step);
    } else if (step->kind == TopicStepKind::AutoDiagnose) {
        runDiagnose(*step);
    }
}

void TroubleshootingWizard::runEnvironment(const TopicStep& step) {
    if (m_generation != 0) {
        return; // one run at a time
    }
    std::vector<std::string> ids;
    for (const std::string_view id : step.checkIds) {
#ifndef Q_OS_LINUX
        if (id == "usb-access") {
            continue; // Linux only (plan 0009)
        }
#endif
        ids.emplace_back(id);
    }
    diag::EnvironmentContext context = m_app.diagEnvironment();
    std::vector<QString> rowIds;
    std::vector<QString> titles;
    for (const diag::Check& check : diag::environmentChecks(context)) {
        if (std::ranges::find(ids, check.id) != ids.end()) {
            rowIds.push_back(toQString(check.id));
            titles.push_back(QCoreApplication::translate("hwio::diag", check.titleSourceText));
        }
    }
    m_checks->reset(rowIds, titles);
    // Owned by the request: written by the worker while it runs, read after checksFinished.
    m_pendingFacts = std::make_shared<diag::EnvironmentFacts>();
    context.facts = m_pendingFacts;
    m_generation = m_app.diagWorker().runChecks(
        DiagWorker::ChecksRequest{.context = std::move(context), .ids = std::move(ids)});
}

void TroubleshootingWizard::onEnvProgress(quint64 generation, const diag::CheckResult& result) {
    if (generation == 0 || generation != m_generation) {
        return;
    }
    m_checks->update(result);
}

void TroubleshootingWizard::onEnvFinished(quint64 generation,
                                          const std::vector<diag::CheckResult>& results) {
    if (generation == 0 || generation != m_generation) {
        return;
    }
    m_generation = 0;
    for (const diag::CheckResult& result : results) {
        m_checks->update(result); // dependencies (e.g. jlink-software) are not rows: ignored
    }
    m_facts = std::move(m_pendingFacts);
    m_pendingFacts.reset();
    emit changed();
}

void TroubleshootingWizard::runDiagnose(const TopicStep& step) {
    std::vector<std::string> rows;
    for (const std::string_view id : step.checkIds) {
        rows.emplace_back(id);
    }
    if (m_bleTab || m_tab.isNull()) {
        // ADR-0299: a Bluetooth tab has no J-Link: the rows are not checked.
        std::vector<std::string> ids = rows;
        if (ids.empty()) {
            ids.assign(device::diagnoseStepIds.begin(), device::diagnoseStepIds.end());
        }
        std::vector<QString> rowIds;
        std::vector<QString> titles;
        const device::DeviceProfile* deviceProfile = profile();
        const device::TargetProfile* target =
            deviceProfile != nullptr ? device::findTarget(*deviceProfile, toStdString(targetId()))
                                     : nullptr;
        for (const std::string& id : ids) {
            QString title = toQString(id);
            if (target != nullptr) {
                for (const device::DiagnoseStepInfo& info : device::diagnoseSteps(*target)) {
                    if (info.id == id) {
                        title = toQString(info.title);
                    }
                }
            }
            rowIds.push_back(toQString(id));
            titles.push_back(title);
        }
        m_checks->reset(rowIds, titles);
        for (const std::string& id : ids) {
            m_checks->update(
                device::DiagnoseStep{.id = id,
                                     .status = diag::CheckStatus::Skipped,
                                     .result = tr("Not checked: no J-Link chosen").toStdString(),
                                     .error = std::nullopt});
        }
        return;
    }
    (void)m_runner->start(rows, m_checks);
}

bool TroubleshootingWizard::running() const { return m_generation != 0 || m_runner->running(); }

bool TroubleshootingWizard::lteConnector() const {
    const std::array ids{QStringLiteral("connect"), QStringLiteral("identify")};
    return std::ranges::any_of(ids, [this](const QString& id) {
        const diag::CheckResult* row = m_checks->row(id);
        return row != nullptr && row->error &&
               (row->error->code == ErrorCode::TargetWrongFamily ||
                row->error->code == ErrorCode::TargetOtherConnector);
    });
}

// ---- steps --------------------------------------------------------------------------------------

std::vector<WizardStep> TroubleshootingWizard::plannedSteps() const {
    std::vector<WizardStep> steps;
    for (const Flow& flow : m_flow) {
        WizardStep step;
        switch (flow.kind) {
        case Kind::Choice:
            step.id = QStringLiteral("choice");
            step.title = tr("Problem");
            step.showInStepList = false;
            break;
        case Kind::Intro:
            step.id = QStringLiteral("intro");
            step.title = tr("Error");
            step.showInStepList = false;
            break;
        case Kind::Picker:
            step.id = QStringLiteral("probe");
            step.title = tr("Probe");
            break;
        case Kind::TopicStep:
            step.id = QStringLiteral("step-%1").arg(flow.topicStep + 1);
            step.title = tt(m_topic->steps[flow.topicStep].titleSourceText);
            break;
        case Kind::End:
            step.id = QStringLiteral("end");
            step.title = tr("Support");
            break;
        }
        steps.push_back(step);
    }
    return steps;
}

void TroubleshootingWizard::fillTopicContent(WizardStep& step, const TopicStep& topicStep) const {
    const bool ownBody = !std::string_view(topicStep.bodySourceText).empty();
    step.heading = tt(topicStep.headingSourceText);
    if (ownBody) {
        step.body = tt(topicStep.bodySourceText);
    }
    const device::DeviceProfile* deviceProfile = profile();
    const device::TargetProfile* target =
        deviceProfile != nullptr ? device::findTarget(*deviceProfile, toStdString(targetId()))
                                 : nullptr;
    if (topicStep.illustrationSlot == guideCable) {
        if (target != nullptr && !target->connectionGuide.empty()) {
            const device::ConnectionGuideStep& guide = target->connectionGuide.front();
            step.illustration = toQString(guide.illustrationSlot);
            if (step.body.isEmpty()) {
                step.body = device::guideStepBody(guide);
            }
        }
        if (m_wrongFamily && !ownBody && topicStep.kind == TopicStepKind::Manual) {
            // 0035 § C (no-connect): the J-Link answered from the LTE MCU.
            step.body = tr("The J-Link is on the LTE connector. Move it to the APP connector.");
        }
    } else if (topicStep.illustrationSlot == guidePower) {
        step.illustration = QStringLiteral("chester-power");
        if (step.body.isEmpty() && deviceProfile != nullptr) {
            step.body = device::powerHint(*deviceProfile);
        }
    } else {
        step.illustration = toQString(topicStep.illustrationSlot);
    }
    if (topicStep.learnMore && m_topic != nullptr && !m_topic->docsTopic.empty()) {
        step.linkText = tr("Learn more");
        step.linkUrl = m_app.docsLink(toQString(m_topic->docsTopic));
    }
}

void TroubleshootingWizard::fillTopicStep(WizardStep& step, const TopicStep& topicStep) const {
    fillTopicContent(step, topicStep);
    step.primaryText = tr("Next");
    switch (topicStep.kind) {
    case TopicStepKind::AutoEnvironment: {
        step.kind = StepKind::Checks;
        if (m_bleTab) {
            break;
        }
        QVariantList actions;
        if (const std::optional<ErrorInfo> error = setupError(); error) {
            actions.append(button(setupAction, tr("Setup wizard…")));
        }
        if (topicStep.action == actionConnect && !running() && m_checks->count() > 0 &&
            m_checks->allPassed() && m_app.wizardRegistry().has(connectWizardId)) {
            step.heading = tr("Found — connect now");
            actions.append(button(stepAction, tt(topicStep.actionSourceText)));
        }
        actions.append(button(checkAgainAction, tr("Check again"), !running()));
        step.actions = actions;
        break;
    }
    case TopicStepKind::AutoDiagnose:
        step.kind = StepKind::Checks;
        if (m_bleTab) {
            if (m_app.wizardRegistry().has(connectWizardId)) {
                step.actions = QVariantList{button(connectWizardId, tr("Connect a device"))};
            }
        } else {
            step.actions = QVariantList{button(checkAgainAction, tr("Check again"), !running())};
        }
        break;
    case TopicStepKind::Manual:
        step.kind = StepKind::Info;
        if (!topicStep.action.empty() && !m_bleTab) {
            const bool resetting = m_runner->resetting();
            step.actions = QVariantList{
                button(stepAction, tt(topicStep.actionSourceText), !resetting && !m_tab.isNull())};
        }
        break;
    case TopicStepKind::Commands:
        step.kind = StepKind::Commands;
        step.commands = firmwareCommands();
        step.actions = firmwareActions();
        break;
    case TopicStepKind::HandOff: {
        step.kind = StepKind::Info;
        bool available = true;
        if (topicStep.action == handOffLteUpdate) {
            available = m_app.wizardRegistry().has(lteUpdateId);
        }
        if (available && !m_tab.isNull()) {
            step.actions = QVariantList{button(stepAction, tt(topicStep.actionSourceText))};
        }
        break;
    }
    }
}

WizardStep TroubleshootingWizard::current() const {
    WizardStep step;
    step.id = stepId();
    if (m_index >= m_flow.size()) {
        return step;
    }
    for (const WizardStep& planned : plannedSteps()) {
        if (planned.id == step.id) {
            step.title = planned.title;
            step.showInStepList = planned.showInStepList;
        }
    }
    switch (m_flow.at(m_index).kind) {
    case Kind::Choice: {
        step.kind = StepKind::Choice;
        step.heading = tr("What is the problem?");
        QVariantList choices;
        for (const Topic& topic : troubleshootingTopics()) {
            if (topic.listed) {
                const QString id = toQString(topic.id);
                choices.append(QVariantMap{{QStringLiteral("id"), id},
                                           {QStringLiteral("title"), tt(topic.titleSourceText)},
                                           {QStringLiteral("selected"), id == m_choice}});
            }
        }
        step.choices = choices;
        step.primaryText = tr("Next");
        break;
    }
    case Kind::Intro:
        // The headline is runtime text (plain text heading); the cause is shown by the
        // `wizard-error` control (ADR-0184: no runtime data in the body).
        step.kind = StepKind::Info;
        step.heading = m_context.message();
        step.body = tr("Let's find the cause.");
        step.embedded = QStringLiteral("wizard-error");
        step.embeddedOptions = QVariantMap{{QStringLiteral("showMessage"), false}};
        step.primaryText = tr("Next");
        break;
    case Kind::Picker:
        step.kind = StepKind::Embedded;
        step.embedded = probePickerStepName;
        step.heading = tr("Choose the J-Link");
        step.body = tr("The checks need a J-Link. Choose the one connected to the device.");
        step.primaryText = tr("Next");
        break;
    case Kind::TopicStep:
        fillTopicStep(step, *topicStep());
        break;
    case Kind::End:
        step.kind = StepKind::Done;
        step.heading = tr("Still not working?");
        step.body = tr("Create a diagnostic report and send it to HARDWARIO support.");
        step.actions = QVariantList{button(reportAction, tr("Create diagnostic report…"))};
        break;
    }
    return step;
}

CheckListModel* TroubleshootingWizard::checks() {
    const TopicStep* step = topicStep();
    if (step != nullptr && (step->kind == TopicStepKind::AutoEnvironment ||
                            step->kind == TopicStepKind::AutoDiagnose)) {
        return m_checks;
    }
    return nullptr;
}

QObject* TroubleshootingWizard::embeddedViewModel() {
    if (m_index < m_flow.size() && m_flow.at(m_index).kind == Kind::Picker) {
        return m_picker;
    }
    return this;
}

// ---- buttons ------------------------------------------------------------------------------------

bool TroubleshootingWizard::canPrimary() const {
    if (m_index >= m_flow.size()) {
        return false;
    }
    switch (m_flow.at(m_index).kind) {
    case Kind::Choice:
        return topicById(toStdString(m_choice)) != nullptr;
    case Kind::Intro:
        return true;
    case Kind::Picker:
        return m_picker != nullptr && m_picker->selectedSerial().has_value() && !m_tab.isNull();
    case Kind::TopicStep:
        return !running();
    case Kind::End:
        return false;
    }
    return false;
}

void TroubleshootingWizard::primary() {
    if (!canPrimary()) {
        return;
    }
    switch (m_flow.at(m_index).kind) {
    case Kind::Choice: {
        setTopic(topicById(toStdString(m_choice)));
        m_index = 1; // the first step of the topic (the choice stays in front for Back)
        enter();
        return;
    }
    case Kind::Picker: {
        const std::optional<std::uint32_t> serial = m_picker->selectedSerial();
        const QString product =
            m_picker
                ->data(m_picker->index(m_picker->selectedRow()), ProbePickerViewModel::ProductRole)
                .toString();
        if (!serial ||
            !m_tab->configure(m_picker->profileId(), m_picker->targetId(), *serial, product)) {
            return;
        }
        break;
    }
    default:
        break;
    }
    if (m_index + 1 < m_flow.size()) {
        ++m_index;
        // Review 0009 F4: the probe step only while the tab has no probe (after Back and Next
        // the tab has the one chosen there; the wizard picker would show it as in use).
        if (m_flow.at(m_index).kind == Kind::Picker && hasProbe() && m_index + 1 < m_flow.size()) {
            ++m_index;
        }
        enter();
    }
}

bool TroubleshootingWizard::canBack() const {
    // Back never returns to the probe step (back() skips it: the tab has its probe now).
    return m_index > 0 && !running();
}

void TroubleshootingWizard::back() {
    if (!canBack()) {
        return;
    }
    --m_index;
    if (m_flow.at(m_index).kind == Kind::Picker && m_index > 0) {
        --m_index;
    }
    if (m_flow.at(m_index).kind == Kind::Choice) {
        m_choice = m_topic != nullptr ? toQString(m_topic->id) : m_choice;
    }
    enter();
}

void TroubleshootingWizard::choose(const QString& choiceId) {
    if (m_index < m_flow.size() && m_flow.at(m_index).kind == Kind::Choice) {
        m_choice = choiceId;
        emit changed();
    }
}

std::optional<ErrorInfo> TroubleshootingWizard::setupError() const {
    // A failed (or Warning) check whose error names the setup wizard: USB access, nRF Util
    // (0035 § C: probe-busy → setup step 3, modem-failed → setup step 4).
    if (running() || !m_app.wizardRegistry().has(setupWizardId)) {
        return std::nullopt;
    }
    for (int i = 0; i < m_checks->count(); ++i) {
        const QString id = m_checks->data(m_checks->index(i), CheckListModel::IdRole).toString();
        const diag::CheckResult* row = m_checks->row(id);
        if (row != nullptr && row->error &&
            (row->status == diag::CheckStatus::Failed ||
             row->status == diag::CheckStatus::Warning)) {
            ErrorInfo info(*row->error);
            if (info.wizard() == setupWizardId) {
                return info;
            }
        }
    }
    return std::nullopt;
}

void TroubleshootingWizard::action(const QString& actionId) {
    if (actionId == checkAgainAction) {
        runCurrent();
        emit changed();
        return;
    }
    if (actionId == reportAction) {
        // Help → Create diagnostic report… (0007); this wizard closes first.
        handOffAndClose({});
        m_app.report()->show();
        return;
    }
    if (actionId == setupAction) {
        if (const std::optional<ErrorInfo> error = setupError(); error) {
            errorHandOffAndClose(*error); // ADR-0297: the setup wizard at that step
        }
        return;
    }
    if (actionId == connectWizardId) {
        handOffAndClose(openWizardPrefix + connectWizardId);
        return;
    }
    if (actionId.startsWith(firmwarePrefix)) {
        if (ProbeFirmwareViewModel* update = m_app.probeFirmware(); update != nullptr) {
            update->requestForSerial(actionId.mid(firmwarePrefix.size()));
        }
        return;
    }
    const TopicStep* step = topicStep();
    if (actionId != stepAction || step == nullptr || m_tab.isNull()) {
        return;
    }
    const std::string_view act = step->action;
    if (act == actionReset) {
        (void)m_runner->reset();
    } else if (act == actionSpeed1000) {
        useSpeed1000();
    } else if (act == actionConnect) {
        handOffAndClose(openWizardPrefix + connectWizardId);
    } else if (act == handOffLteUpdate) {
        handOffAndClose(openWizardPrefix + lteUpdateId);
    } else if (act == handOffFlashWizard) {
        // 0035 § C: the flash wizard when registered, else the Flash page.
        handOffAndClose(m_app.wizardRegistry().has(flashWizardId)
                            ? openWizardPrefix + flashWizardId
                            : QStringLiteral("open-flash-page"));
    } else if (act == handOffRecover) {
        // Plan 0033 (ADR-0236): [Recover…] shown once for this MCU.
        handOffAndClose(toQString(handOffRecover) + QLatin1Char(':') + targetId());
    }
}

void TroubleshootingWizard::useSpeed1000() {
    // ADR-0298: the per-probe speed (ADR-0052) is stored; it applies when a session opens, so the
    // probe-only session of this wizard is closed (the next Diagnose opens a new one) and a
    // connected tab connects again.
    ConnectionViewModel* connection = m_tab->connection();
    if (connection == nullptr) {
        return;
    }
    connection->setAutoSpeed(false);
    connection->setSpeedKhz(1000);
    m_tab->showToast(tr("J-Link speed set to 1000 kHz for this probe."));
    const device::DeviceSession* session = m_tab->session();
    if (session == nullptr || m_tab->isBusy()) {
        return;
    }
    // The wizards' probe-only session (ADR-0300) is closed; the next Diagnose opens a new one.
    // Any other session is the tab's connection: it connects again.
    const bool reconnect = !m_tab->wizardOwnsSession() &&
                           session->connection().state != device::ConnectionState::ProbeOpen;
    m_tab->disconnectDevice();
    if (reconnect) {
        m_tab->connectDevice();
    }
}

QVariantList TroubleshootingWizard::firmwareActions() const {
    // ADR-0291 pattern (ADR-0255): one action per probe with outdated firmware (from the facts of
    // the last check), disabled with the reason when a tab uses it (ADR-0246).
    QVariantList actions;
    ProbeFirmwareViewModel* update = m_app.probeFirmware();
    if (update == nullptr || !m_facts) {
        return actions;
    }
    for (const diag::EnvironmentFacts::Probe& probe : m_facts->probes) {
        if (probe.firmware != jlink::FirmwareStatus::Outdated) {
            continue;
        }
        const QString serial = QString::number(probe.info.serialNumber);
        const QString reason = update->blockedReason(probe.info.serialNumber);
        actions.append(reason.isEmpty() ? button(firmwarePrefix + serial,
                                                 tr("Update J-Link firmware %1…").arg(serial))
                                        : button(firmwarePrefix + serial, reason, false));
    }
    return actions;
}

QStringList TroubleshootingWizard::firmwareCommands() const {
    QStringList commands;
    if (m_facts) {
        for (const diag::EnvironmentFacts::Probe& probe : m_facts->probes) {
            if (probe.firmware == jlink::FirmwareStatus::Outdated) {
                commands.append(QStringLiteral("hwio jlink update-firmware -n %1")
                                    .arg(probe.info.serialNumber));
            }
        }
    }
    if (commands.isEmpty()) {
        commands.append(QStringLiteral("hwio jlink update-firmware"));
    }
    return commands;
}

void TroubleshootingWizard::handOffAndClose(const QString& action) {
    if (!action.isEmpty()) {
        emit handOff(action);
    }
    emit finished(false);
}

void TroubleshootingWizard::errorHandOffAndClose(const ErrorInfo& error) {
    emit handOffError(error);
    emit finished(false);
}

QString TroubleshootingWizard::busyCancelText() const {
    return m_runner->running() ? tr("A check is running. Cancel it and close the wizard?")
                               : QString();
}

void TroubleshootingWizard::cancelOperation() {
    if (m_generation != 0) {
        m_app.diagWorker().cancel(m_generation);
        m_generation = 0;
    }
    m_runner->cancel();
}

// ---- registration -------------------------------------------------------------------------------

void registerConnectAndTroubleshootingWizards(AppViewModel& app) {
    (void)app.wizardRegistry().add(WizardDescriptor{
        .id = WizardId::ConnectDevice,
        .perTab = true,
        // Qt parent ownership: the WizardViewModel.
        .create = [&app](QObject* parent) -> Wizard* {
            return new ConnectWizard(app, parent); // NOLINT(cppcoreguidelines-owning-memory)
        }});
    (void)app.wizardRegistry().add(
        WizardDescriptor{.id = WizardId::Troubleshooting,
                         .perTab = true,
                         .create = [&app](QObject* parent) -> Wizard* {
                             return new TroubleshootingWizard(
                                 app, parent); // NOLINT(cppcoreguidelines-owning-memory)
                         }});
    (void)app.wizardRegistry().addEmbedded(
        QStringLiteral("wizard-error"),
        QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/wizard/WizardErrorStep.qml")));
}

} // namespace hwio::gui
