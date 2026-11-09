#pragma once

// ConnectWizard - guides a user from nothing to a connected tab (plan/features/0009-m4-connect-
// and-troubleshooting.md with the 0035 amendments; ADR-0249: J-Link only, application MCU only,
// no Device step; WizardId::ConnectDevice "connect-device", per tab).
//
// Steps: Probe · Cable (one step per entry of the console target's connection guide, ADR-0077) ·
// Power · Check · Done.
//   - Probe: the Embedded `probe-picker` step (wizard mode, ADR-0252) with the checks `probes`,
//     `usb-access` (Linux) and `probe-holders` when no probe is found; [Next] sets the tab to the
//     selected probe (DeviceTabViewModel::configure, never connects).
//   - Cable / Power: Info steps from the device profile (guide entry, power hint).
//   - Check: Diagnose (all six steps) on the tab's session or a probe-only one (DiagnoseRunner).
//     All passed → [Connect]. A failed step offers [Show me how] back to its step (no power →
//     Power; target.connect_failed → Cable); a J-Link answering from the LTE MCU
//     (target.wrong_family / target.other_connector) → "Move it to the APP connector",
//     [Show me how] and [Update LTE firmware…] (lte-update); target.protected → [Open Flash page]
//     (open-flash-page:recover:app, ADR-0236), Connect still possible; no console (RTT) →
//     [Flash firmware…] and [Connect anyway]; another failure whose error names another wizard
//     (e.g. probe.open_failed → troubleshooting `probe-busy`) → [Help me fix this] (ADR-0297).
//   - Done: [Connect] promotes the probe-only session (connectDevice: startConnection with RTT, the
//     probe lock stays held); "Connected to CHESTER app · <SN>." with [Open console]; a failed
//     connection shows the tab's error with [Check again].
//
// Start: an error opens at its step (startStepFor: probe.not_found → Probe, target.no_power →
// Power, target.connect_failed / target.wrong_family → Cable, target.not_connected → Check); a
// connected tab is a pure check (Check); a tab with a probe starts at Cable; a tab without one at
// Probe. Entering Probe on a tab that has a probe returns the tab to its picker first
// (chooseAnotherProbe) so the probe is free in the wizard's list; its serial is preselected.
//
// Ownership: created by the WizardRegistry factory with the WizardViewModel as Qt parent; keeps the
// tab only as QPointer (wizard contract). GUI thread only. Bounds: one Diagnose at a time, one
// probe picker, one probe-only session (the tab's rule).

#include "hwio/core/error.h"
#include "viewmodels/error_info.h"
#include "wizard/wizard.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration>

#include <cstdint>
#include <optional>
#include <vector>

namespace hwio::device {
struct DeviceProfile;
struct TargetProfile;
} // namespace hwio::device

namespace hwio::gui {

class AppViewModel;
class CheckListModel;
class DeviceTabViewModel;
class DiagnoseRunner;
class ProbePickerViewModel;

class ConnectWizard : public Wizard {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the wizard registry")

    // The error shown by the `wizard-error` control (a failed connection on Done).
    Q_PROPERTY(hwio::gui::ErrorInfo shownError READ shownError NOTIFY changed)
    // The current step (QML: ConnectWizard.Probe, …).
    Q_PROPERTY(hwio::gui::ConnectWizard::Step step READ step NOTIFY changed)

public:
    enum Step : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        Probe,
        Cable,
        Power,
        Check,
        Done,
    };
    Q_ENUM(Step)

    // What the Check step found (tests and current()).
    enum class Outcome : std::uint8_t {
        Running,
        Passed,
        Protected,
        NoConsole,
        WrongConnector,
        NoPower,
        ConnectFailed,
        OtherFailed,
    };

    static inline const QString wizardId = QStringLiteral("connect-device");

    ConnectWizard(AppViewModel& app, QObject* parent);
    ~ConnectWizard() override;
    ConnectWizard(const ConnectWizard&) = delete;
    ConnectWizard& operator=(const ConnectWizard&) = delete;
    ConnectWizard(ConnectWizard&&) = delete;
    ConnectWizard& operator=(ConnectWizard&&) = delete;

    // The step a ConnectDevice error opens at (0035 § D; the coverage test); nullopt otherwise.
    [[nodiscard]] static std::optional<Step> startStepFor(ErrorCode code);

    [[nodiscard]] QString id() const override { return wizardId; }
    [[nodiscard]] QString title() const override;
    void start(const ErrorInfo& context, DeviceTabViewModel* tab) override;
    [[nodiscard]] std::vector<WizardStep> plannedSteps() const override;
    [[nodiscard]] WizardStep current() const override;
    void primary() override;
    void back() override;
    [[nodiscard]] bool canBack() const override;
    [[nodiscard]] bool canPrimary() const override;
    [[nodiscard]] QString busyCancelText() const override;
    void cancelOperation() override;
    [[nodiscard]] CheckListModel* checks() override;
    [[nodiscard]] QObject* embeddedViewModel() override;
    void action(const QString& actionId) override;
    // Plan 0009 review (decision B): a tab that the wizard returned to its picker and on which
    // nothing else was chosen gets its original probe back.
    void aboutToClose() override;

    [[nodiscard]] Step step() const;
    [[nodiscard]] Outcome outcome() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] ErrorInfo shownError() const;
    [[nodiscard]] ProbePickerViewModel* picker() const { return m_picker; }
    [[nodiscard]] bool connectFailed() const;

private:
    struct Flow {
        Step step = Probe;
        std::size_t guide = 0; // Cable: the connection guide entry
    };

    void buildFlow();
    void goTo(std::size_t index);
    void goToStep(Step step);
    void enterProbe();
    void runProbeChecks();
    void preselect();
    void runCheck();
    void connectTab();
    void handOffAndClose(const QString& action);
    [[nodiscard]] bool hasProbe() const;
    [[nodiscard]] const device::DeviceProfile* profile() const;
    [[nodiscard]] const device::TargetProfile* target() const;
    [[nodiscard]] QString targetId() const;
    [[nodiscard]] std::optional<ErrorInfo> failedRowError() const;
    [[nodiscard]] QString otherWizardFor(const ErrorInfo& error) const;
    void fillCheck(WizardStep& step) const;
    void fillDone(WizardStep& step) const;

    AppViewModel& m_app;
    QPointer<DeviceTabViewModel> m_tab;
    std::vector<Flow> m_flow; // Probe, Cable × guide entries, Power, Check, Done
    std::size_t m_index = 0;
    CheckListModel* m_probeChecks = nullptr;     // child
    CheckListModel* m_checks = nullptr;          // child (the Check step)
    DiagnoseRunner* m_runner = nullptr;          // child
    ProbePickerViewModel* m_picker = nullptr;    // child (created for the Probe step)
    std::uint64_t m_probeCheckRun = 0;           // DiagWorker generation (0 = none)
    std::optional<std::uint32_t> m_preferSerial; // preselected on the Probe step
    struct OriginalProbe {
        QString profileId;
        QString targetId;
        std::uint32_t serial = 0;
        QString product;
    };
    std::optional<OriginalProbe> m_original; // the tab's probe when the wizard started
    bool m_probeStepShown = false;
    bool m_pureCheck = false; // the tab was connected at the start (steps 1–4 skipped)
    bool m_connectRequested = false;
};

} // namespace hwio::gui
