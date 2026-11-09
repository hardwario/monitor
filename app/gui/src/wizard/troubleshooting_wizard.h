#pragma once

// TroubleshootingWizard - one topic per error code (plan/features/0009-m4-connect-and-
// troubleshooting.md with the 0035 amendments § C; WizardId::Troubleshooting "troubleshooting",
// per tab; topics in troubleshooting_topics.*).
//
// - With an error ("Help me fix this", ADR-0297 hand-offs): an introduction (the error's headline
//   and cause, "Let's find the cause."), then the steps of topicFor(code).
// - Without an error (Help → Troubleshoot connection…): the Choice "What is the problem?" with the
//   listed topics, then the chosen topic.
// - Every topic ends with "Still not working?" (Done): [Create diagnostic report…] and Close.
//
// Steps of a topic: automatic environment checks (DiagWorker; `usb-access` only on Linux; a failed
// check whose error names the setup wizard offers [Setup wizard…], ADR-0297), automatic Diagnose
// steps (DiagnoseRunner: the tab's session or a probe-only one; ADR-0296 stop point), manual
// instructions ([Reset], [Use 1000 kHz] per ADR-0298, [Update J-Link firmware …] per ADR-0291 /
// ADR-0255) and hand-offs (lte-update, the flash wizard or the Flash page, Recover on the Flash
// page). Entering an automatic step runs it; [Check again] runs it again.
//
// ADR-0299: on a tab without a J-Link probe, the Embedded `probe-picker` step is inserted before
// the topic's first Diagnose step and sets the tab to the chosen probe; on a Bluetooth tab the
// Diagnose rows read "Not checked: no J-Link chosen" with [Connect a device].
//
// Ownership: created by the WizardRegistry factory with the WizardViewModel as Qt parent; keeps the
// tab only as QPointer (wizard contract). GUI thread only. Bounds: one check run at a time, the
// topic table's bounds (troubleshooting_topics.h).

#include "hwio/core/error.h"
#include "hwio/diag/environment.h"
#include "viewmodels/error_info.h"
#include "wizard/troubleshooting_topics.h"
#include "wizard/wizard.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hwio::device {
struct DeviceProfile;
} // namespace hwio::device

namespace hwio::gui {

class AppViewModel;
class CheckListModel;
class DeviceTabViewModel;
class DiagnoseRunner;
class ProbePickerViewModel;

class TroubleshootingWizard : public Wizard {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the wizard registry")

    // The error shown by the `wizard-error` control (the introduction).
    Q_PROPERTY(hwio::gui::ErrorInfo shownError READ shownError NOTIFY changed)

public:
    static inline const QString wizardId = QStringLiteral("troubleshooting");

    TroubleshootingWizard(AppViewModel& app, QObject* parent);
    ~TroubleshootingWizard() override;
    TroubleshootingWizard(const TroubleshootingWizard&) = delete;
    TroubleshootingWizard& operator=(const TroubleshootingWizard&) = delete;
    TroubleshootingWizard(TroubleshootingWizard&&) = delete;
    TroubleshootingWizard& operator=(TroubleshootingWizard&&) = delete;

    [[nodiscard]] QString id() const override { return wizardId; }
    [[nodiscard]] QString title() const override;
    void start(const ErrorInfo& context, DeviceTabViewModel* tab) override;
    [[nodiscard]] std::vector<WizardStep> plannedSteps() const override;
    [[nodiscard]] WizardStep current() const override;
    void primary() override;
    void back() override;
    void choose(const QString& choiceId) override;
    [[nodiscard]] bool canBack() const override;
    [[nodiscard]] bool canPrimary() const override;
    [[nodiscard]] QString busyCancelText() const override;
    void cancelOperation() override;
    [[nodiscard]] CheckListModel* checks() override;
    [[nodiscard]] QObject* embeddedViewModel() override;
    void action(const QString& actionId) override;

    [[nodiscard]] ErrorInfo shownError() const { return m_context; }
    // Tests.
    [[nodiscard]] const Topic* topic() const noexcept { return m_topic; }
    // The current topic step (nullptr on the introduction, the choice, the probe step and the end).
    [[nodiscard]] const TopicStep* topicStep() const;
    [[nodiscard]] QString stepId() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] ProbePickerViewModel* picker() const { return m_picker; }

private:
    enum class Kind : std::uint8_t { Choice, Intro, Picker, TopicStep, End };
    struct Flow {
        Kind kind = Kind::Choice;
        std::size_t topicStep = 0; // Kind::TopicStep
    };

    void setTopic(const Topic* topic);
    void enter();
    void runCurrent();
    void runEnvironment(const TopicStep& step);
    void runDiagnose(const TopicStep& step);
    void onEnvProgress(quint64 generation, const diag::CheckResult& result);
    void onEnvFinished(quint64 generation, const std::vector<diag::CheckResult>& results);
    void useSpeed1000();
    void handOffAndClose(const QString& action);
    void errorHandOffAndClose(const ErrorInfo& error);
    [[nodiscard]] bool hasProbe() const;
    [[nodiscard]] const device::DeviceProfile* profile() const;
    [[nodiscard]] QString targetId() const;
    [[nodiscard]] bool lteConnector() const;
    [[nodiscard]] std::optional<ErrorInfo> setupError() const;
    [[nodiscard]] QVariantList firmwareActions() const;
    [[nodiscard]] QStringList firmwareCommands() const;
    void fillTopicContent(WizardStep& step, const TopicStep& topicStep) const;
    void fillTopicStep(WizardStep& step, const TopicStep& topicStep) const;

    AppViewModel& m_app;
    QPointer<DeviceTabViewModel> m_tab;
    ErrorInfo m_context;
    const Topic* m_topic = nullptr;
    QString m_choice;         // the selected topic id on the Choice step
    std::vector<Flow> m_flow; // the steps of this run (bounded: maxTopicSteps + 4)
    std::size_t m_index = 0;
    CheckListModel* m_checks = nullptr;       // child
    DiagnoseRunner* m_runner = nullptr;       // child
    ProbePickerViewModel* m_picker = nullptr; // child (created for the probe step)
    std::uint64_t m_generation = 0;           // DiagWorker run (0 = none)
    std::shared_ptr<diag::EnvironmentFacts> m_pendingFacts;
    std::shared_ptr<diag::EnvironmentFacts> m_facts; // of the last finished check run
    bool m_bleTab = false;                           // Diagnose cannot run (ADR-0299)
    bool m_wrongFamily = false; // a Diagnose answered from the LTE MCU (or the error said so)
};

// Registers the Connect and Troubleshooting wizards (plan 0009) and the `wizard-error` control.
void registerConnectAndTroubleshootingWizards(AppViewModel& app);

} // namespace hwio::gui
