#pragma once

// FlashWizard - the guided flash of application firmware (plan/features/0010-m4-flash-wizard.md
// with the 0035 amendments; ADR-0074, ADR-0245, ADR-0252, ADR-0253; wizard framework of plan
// 0007, WizardId::FlashFirmware "flash-firmware", per tab).
//
// Steps: Probe · Cable · Firmware · Confirm · Flash · Done.
//   - Probe: the Embedded `probe-picker` step (wizard mode) with the checks `probes`,
//     `usb-access` (Linux) and `probe-holders`; skipped when the tab already has a J-Link probe.
//     [Next] configures the tab with the selected probe (DeviceTabViewModel::configure, never
//     connects).
//   - Cable: the console target's connection guide (ADR-0077, ADR-0233) and the Diagnose steps
//     `probe` … `identify` (the RTT steps are not needed for flashing: Diagnose stops after
//     `identify`, ADR-0296). A J-Link answering from
//     the other MCU (target.wrong_family / target.other_connector) asks to move the cable or
//     offers [Update LTE firmware…]; target.protected is shown as information with
//     [Open Flash page] (Recover there, ADR-0236). The step is skipped when the first Diagnose of
//     a wizard opened on a tab with a probe passes at once.
//   - Firmware: the Embedded `firmware-source` step: the Flash page's source card bound to the
//     wizard's own FirmwareSourceViewModel (Application kind fixed), "Allow writing UICR" when
//     the image has UICR records, the start-point explanation of a FlashFirmware error and the
//     link [Update LTE firmware…] (closes this wizard and opens `lte-update`, ADR-0234).
//   - Confirm: the Embedded `flash-summary` step (plain text: it names files and serial numbers,
//     ADR-0184) with [Flash].
//   - Flash: the Embedded `phase-progress` step: the PhaseStepper of the session's flash
//     operation. The tab is busy (setBusy("flash-firmware", …)); Cancel asks first.
//   - Done: success (Done step: [Open console] · [Flash another] · Close), failure (the
//     `phase-progress` step with the ErrorPanel: [Try again] and, when the error names another
//     registered wizard, [Help me fix this]) or cancelled ([Flash again]).
//
// Sessions: the tab's session when it has one; otherwise ensureSession(connectTarget = false)
// opens a probe-only session for Diagnose and the flash (operations run on a probe-only session,
// ADR-0069; ADR-0295: not promoted before flashing). [Open console] hands off "open-console", which
// promotes that session with RTT (AppViewModel::onHandOff → connectDevice). A probe-only session
// the wizard opened is released when the wizard closes without a hand-off.
//
// Hand-offs to other wizards are "open-wizard:<id>" (AppViewModel opens it on the same tab after
// this one has closed).
//
// Ownership: created by the WizardRegistry factory with the WizardViewModel as Qt parent; keeps
// the tab only as QPointer (wizard contract). GUI thread only. Bounds: one operation at a time
// (Diagnose or flash), one probe picker, one source (plan 0006 bounds).

#include "hwio/core/error.h"
#include "hwio/device/operation.h"
#include "hwio/firmware/program_progress.h"
#include "viewmodels/error_info.h"
#include "wizard/wizard.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hwio::device {
class DeviceSession;
struct DeviceProfile;
struct TargetProfile;
} // namespace hwio::device

namespace hwio::gui {

class AppViewModel;
class CheckListModel;
class DeviceTabViewModel;
class FirmwareServices;
class FirmwareSourceViewModel;
class ProbePickerViewModel;

class FlashWizard : public Wizard {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the wizard registry")

    Q_PROPERTY(int stage READ stage NOTIFY changed)
    Q_PROPERTY(hwio::gui::FirmwareSourceViewModel* source READ source CONSTANT)
    // Firmware step: the explanation of the start-point error or of an image that does not fit
    // the application MCU ("" = none), and its buttons.
    Q_PROPERTY(QString explanation READ explanation NOTIFY changed)
    Q_PROPERTY(bool explanationOffersLteUpdate READ explanationOffersLteUpdate NOTIFY changed)
    Q_PROPERTY(bool explanationOffersCatalog READ explanationOffersCatalog NOTIFY changed)
    // `lte-update` is registered: the link "Updating the LTE firmware? [Update LTE firmware…]".
    Q_PROPERTY(bool lteUpdateAvailable READ lteUpdateAvailable CONSTANT)
    // Confirm step.
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY changed)
    Q_PROPERTY(QString summaryNote READ summaryNote NOTIFY changed)
    Q_PROPERTY(QString cannotFlashReason READ cannotFlashReason NOTIFY changed)
    // Flash step and the failure.
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY changed)
    Q_PROPERTY(QVariantList phases READ phases NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged) // 0..1, -1 = indeterminate
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(bool cancelAvailable READ cancelAvailable NOTIFY changed)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY changed)
    Q_PROPERTY(QString resultText READ resultText NOTIFY changed)
    Q_PROPERTY(hwio::gui::ErrorInfo error READ error NOTIFY changed)

public:
    // The steps (plannedSteps() order).
    enum Step : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        Probe,
        Cable,
        Firmware,
        Confirm,
        Flash,
        Done,
    };
    Q_ENUM(Step)

    // What the embedded QML shows (Stage): the step, and at Done the outcome.
    enum Stage : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        ProbeStage,
        CableStage,
        FirmwareStage,
        ConfirmStage,
        Flashing,
        Succeeded,
        Failed,
        Cancelled,
    };
    Q_ENUM(Stage)

    static inline const QString wizardId = QStringLiteral("flash-firmware");
    // The Embedded steps of this wizard (registered by registerFlashWizard).
    static inline const QString firmwareSourceStep = QStringLiteral("firmware-source");
    static inline const QString summaryStep = QStringLiteral("flash-summary");
    static inline const QString progressStep = QStringLiteral("phase-progress");

    FlashWizard(AppViewModel& app, FirmwareServices& services, QObject* parent);
    ~FlashWizard() override;
    FlashWizard(const FlashWizard&) = delete;
    FlashWizard& operator=(const FlashWizard&) = delete;
    FlashWizard(FlashWizard&&) = delete;
    FlashWizard& operator=(FlashWizard&&) = delete;

    // The step a FlashFirmware error opens at (plan 0010 § FlashFirmware errors; the coverage
    // test of plan 0035 § D); nullopt for other codes.
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

    [[nodiscard]] Step step() const noexcept { return m_step; }
    [[nodiscard]] int stage() const;
    [[nodiscard]] FirmwareSourceViewModel* source() const { return m_source; }
    [[nodiscard]] ProbePickerViewModel* picker() const { return m_picker; }
    [[nodiscard]] QString explanation() const;
    [[nodiscard]] bool explanationOffersLteUpdate() const;
    [[nodiscard]] bool explanationOffersCatalog() const;
    [[nodiscard]] bool lteUpdateAvailable() const;
    [[nodiscard]] QString summaryText() const;
    [[nodiscard]] QString summaryNote() const;
    [[nodiscard]] QString cannotFlashReason() const;
    [[nodiscard]] QString operationTitle() const;
    [[nodiscard]] QVariantList phases() const;
    [[nodiscard]] qreal progress() const;
    [[nodiscard]] QString progressText() const;
    [[nodiscard]] bool cancelAvailable() const;
    [[nodiscard]] bool cancelling() const noexcept { return m_cancelRequested && m_flash; }
    [[nodiscard]] QString resultText() const;
    [[nodiscard]] ErrorInfo error() const;
    // The Cable step's result (tests and current()).
    [[nodiscard]] bool cableRunning() const;
    [[nodiscard]] bool cablePassed() const;
    [[nodiscard]] bool wrongConnector() const;
    [[nodiscard]] bool targetProtected() const;

    // [Update LTE firmware…]: closes this wizard and opens `lte-update` on the same tab.
    Q_INVOKABLE void updateLteFirmware();
    // [Open firmware catalog ↗] of firmware.not_found_in_catalog.
    Q_INVOKABLE void openCatalog();
    // The PhaseStepper's Cancel (no confirmation: the user pressed it in the step).
    Q_INVOKABLE void cancelFlash();
    // The ErrorPanel's primary action of the failure (ErrorAction): ChooseFile -> Firmware,
    // OpenSettings -> the Settings section (hand-off), else back to Confirm (Try again).
    Q_INVOKABLE void triggerErrorAction(int action);

signals:
    void progressChanged();

private:
    struct DiagnoseRun {
        device::OperationId id = 0;
        QPointer<device::DeviceSession> session;
        bool identifyReported = false;
    };
    struct FlashRun {
        device::OperationId id = 0;
        QPointer<device::DeviceSession> session;
    };

    void goTo(Step step);
    void enterProbe();
    void runProbeChecks();
    void startDiagnose();
    void stopDiagnose();
    void cancelDiagnose();
    void startFlash();
    void finishFlash(const Status& status, std::chrono::milliseconds duration,
                     const std::optional<device::ChipInfo>& chip);
    void watchSession();
    void onSessionChanged();
    void onDiagnoseStep(device::OperationId id, const device::DiagnoseStep& step);
    void onOperationProgress(device::OperationId id, const device::OperationProgress& progress);
    void onOperationFinished(device::OperationId id, const device::OperationResult& result);
    void onDiagnoseDone();
    void handOffAndClose(const QString& action);
    void releaseTab();
    void releaseProbeOnlySession();
    void retranslate();
    [[nodiscard]] bool hasProbe() const;
    [[nodiscard]] bool firmwareReady() const;
    [[nodiscard]] bool otherOperationRunning() const;
    [[nodiscard]] const device::DeviceProfile* profile() const;
    [[nodiscard]] const device::TargetProfile* target() const;
    [[nodiscard]] static QString targetId();
    // Where the loaded image fits: the application MCU, only the LTE MCU, neither, or no image.
    enum class ImageFit : std::uint8_t { NoImage, Application, Lte, Neither };
    [[nodiscard]] ImageFit imageFit() const;
    [[nodiscard]] QString helpWizard() const; // the failure's [Help me fix this] wizard or ""

    AppViewModel& m_app;
    FirmwareServices& m_services;
    QPointer<DeviceTabViewModel> m_tab;
    FirmwareSourceViewModel* m_source = nullptr; // child
    ProbePickerViewModel* m_picker = nullptr;    // child (created for the Probe step)
    CheckListModel* m_probeChecks = nullptr;     // child
    CheckListModel* m_cableChecks = nullptr;     // child
    ErrorInfo m_context;
    bool m_contextFitsLte = false; // firmware.out_of_range: the page's image fits the LTE MCU
    Step m_step = Probe;
    bool m_probeStepShown = false;     // the tab had no probe at start: Back from Cable re-chooses
    bool m_skipCableOnPass = false;    // the first Diagnose passes at once -> Firmware
    bool m_openedProbeOnly = false;    // the wizard opened the tab's probe-only session
    bool m_keepSession = false;        // a hand-off follows: the session stays
    std::uint64_t m_probeCheckRun = 0; // DiagWorker generation (0 = none)
    std::optional<DiagnoseRun> m_diagnose;
    std::optional<FlashRun> m_flash;
    std::optional<Error> m_diagnoseError; // Diagnose could not start
    std::vector<QMetaObject::Connection> m_sessionConnections;
    QPointer<device::DeviceSession> m_watched;
    // The flash.
    bool m_tabBusy = false;
    bool m_cancelRequested = false;
    QString m_flashedName;
    std::uint64_t m_flashedBytes = 0;
    std::span<const firmware::ProgramPhase> m_plannedPhases;
    int m_currentPhase = -1;
    std::uint64_t m_done = 0;
    std::uint64_t m_total = 0;
    std::chrono::milliseconds m_elapsed{};
    std::chrono::milliseconds m_duration{};
    std::optional<device::ChipInfo> m_chip;
    std::optional<Status> m_outcome; // Done: the flash's status
};

// Registers the wizard (WizardId::FlashFirmware) and its Embedded steps.
void registerFlashWizard(AppViewModel& app, FirmwareServices& services);

} // namespace hwio::gui
