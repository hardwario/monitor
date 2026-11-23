#pragma once

// SetupWizard - the first-run setup wizard "first-run-setup" (plan/features/0008 as amended by
// 0035; ADR-0071, ADR-0072, ADR-0185, ADR-0252, ADR-0254, ADR-0255, ADR-0290, ADR-0291).
//
// Steps (global wizard, no tab):
//   welcome  Info + control "setup-step": language combo (ADR-0254), the user mode cards while the
//            mode is not chosen (plan 0056, ADR-0392), "Don't show at start" in the
//            "J-Link Software is needed" variant (ADR-0071); [Skip setup]
//   jlink    Checks `jlink-software`; download link, "It is installed elsewhere…" (file dialog,
//            validated by the check with the chosen path before it is saved to jlink/libraryPath),
//            [Use automatic detection], [Continue anyway]
//   usb      Linux only. Checks `usb-access`; usb.access_denied / usb.world_accessible show the
//            `uaccess` rule commands of ADR-0185 (diag::jlinkUdevCommands)
//   nrfutil  Checks `nrfutil` (optional): install link and commands, "It is installed elsewhere…"
//            (nrfutil/path), [Skip]
//   probe    Embedded `probe-picker` read-only (embeddedOptions {readOnly: true}, ADR-0290) with
//   the
//            checks `probes`, `probe-firmware`, `probe-holders`; one [Update J-Link firmware %1…]
//            action per probe with outdated firmware (ADR-0291)
//   done     Done + control "setup-step": the summary; [Connect a device] (connect-device, when
//            registered and not started from an error)
// Started from an error, the wizard begins at the step for its code (startStepFor) and has no
// Welcome step. Done and Skip setup write setup/completedVersion (not with read-only settings).
//
// The checks run on the application's DiagWorker; a newer request replaces the step's pending one
// (results of older generations are dropped). Each request has its own EnvironmentFacts, written
// only by the worker while the request runs and read here (GUI thread) after checksFinished.
//
// Ownership: created by the WizardRegistry factory (or StartupCheck) with the WizardViewModel as Qt
// parent; the check models and the probe picker are children. GUI thread only.

#include "hwio/core/error.h"
#include "hwio/diag/environment.h"
#include "viewmodels/error_info.h"
#include "wizard/wizard.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hwio::gui {

class AppViewModel;
class CheckListModel;
class ProbePickerViewModel;
class SettingsStore;

class SetupWizard : public Wizard {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the wizard registry")

    // The id of the current step ("welcome", "jlink", "usb", "nrfutil", "probe", "done").
    Q_PROPERTY(QString stepId READ stepId NOTIFY changed)
    // Welcome (ADR-0254): [{code, name}] ("System default", English, Čeština, ...), the current
    // one.
    Q_PROPERTY(QVariantList languages READ languages NOTIFY changed)
    Q_PROPERTY(int languageIndex READ languageIndex NOTIFY changed)
    // Welcome in the "J-Link Software is needed" variant: the "Don't show at start" checkbox.
    Q_PROPERTY(bool showAtStartChoice READ showAtStartChoice NOTIFY changed)
    Q_PROPERTY(bool showAtStart READ showAtStart WRITE setShowAtStart NOTIFY changed)
    Q_PROPERTY(bool settingsReadOnly READ settingsReadOnly CONSTANT)
    // Plain text with runtime data (versions, paths) below the checks ("" = none).
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    // A path chosen with "It is installed elsewhere…" that could not be saved ("" = none).
    Q_PROPERTY(QString pathError READ pathError NOTIFY changed)
    Q_PROPERTY(QStringList commands READ commands NOTIFY changed)
    // Buttons of the step's control: [{id, text, file}]; `file`: opens the file dialog and then
    // calls choosePath(), else WizardViewModel::action(id).
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)
    Q_PROPERTY(QString fileDialogTitle READ fileDialogTitle NOTIFY changed)
    Q_PROPERTY(QStringList fileNameFilters READ fileNameFilters NOTIFY changed)
    // The step's checks run (the default button is disabled meanwhile).
    Q_PROPERTY(bool running READ running NOTIFY changed)
    // Done: one row per step (J-Link Software, USB access, nRF Util, J-Link).
    Q_PROPERTY(hwio::gui::CheckListModel* summary READ summary CONSTANT)
    // Plan 0056 (ADR-0392): Welcome asks the user mode (plan 0045) while it was not chosen when the
    // wizard started: [{mode, name, description}] and the selected card (UiMode value; the
    // preselection at the start). The selection is applied (UiModeController::chooseMode) when
    // Welcome is left in any way: [Next], [Skip setup], ×, Esc, Cancel.
    Q_PROPERTY(bool modeChoice READ modeChoice NOTIFY changed)
    Q_PROPERTY(QVariantList modes READ modes NOTIFY changed)
    Q_PROPERTY(int selectedMode READ selectedMode NOTIFY changed)

public:
    enum class Platform : std::uint8_t { Linux, MacOS, Windows };
    // Normal: first start or Help → Setup wizard…; JLinkNeeded: opened at a later start because the
    // J-Link Software check failed (ADR-0071).
    enum class Mode : std::uint8_t { Normal, JLinkNeeded };

    struct Options {
        Platform platform = currentPlatform();
        Mode mode = Mode::Normal;
        // The environment of the checks; empty = AppViewModel::diagEnvironment() (tests: their own
        // USB sysfs tree, process source, ...).
        std::function<diag::EnvironmentContext()> environment;
    };

    // The vendor download pages (one table, plan 0008 § Risks).
    static inline const QString jlinkDownloadUrl =
        QStringLiteral("https://www.segger.com/downloads/jlink/");
    static inline const QString nrfUtilInstallUrl =
        QStringLiteral("https://www.nordicsemi.com/Products/Development-tools/nRF-Util");
    static inline const QString connectWizardId = QStringLiteral("connect-device");

    SetupWizard(AppViewModel& app, Options options, QObject* parent);
    ~SetupWizard() override;
    SetupWizard(const SetupWizard&) = delete;
    SetupWizard& operator=(const SetupWizard&) = delete;
    SetupWizard(SetupWizard&&) = delete;
    SetupWizard& operator=(SetupWizard&&) = delete;

    [[nodiscard]] static Platform currentPlatform();
    // The step that handles `code` ("jlink", "usb", "nrfutil"), "" for other codes.
    [[nodiscard]] static QString startStepFor(ErrorCode code);

    [[nodiscard]] QString id() const override { return QStringLiteral("first-run-setup"); }
    [[nodiscard]] QString title() const override;
    void start(const ErrorInfo& context, DeviceTabViewModel* tab) override;
    [[nodiscard]] std::vector<WizardStep> plannedSteps() const override;
    [[nodiscard]] WizardStep current() const override;
    void primary() override;
    void back() override;
    [[nodiscard]] bool canBack() const override;
    [[nodiscard]] bool canPrimary() const override;
    void cancelOperation() override;
    [[nodiscard]] CheckListModel* checks() override;
    [[nodiscard]] QObject* embeddedViewModel() override;
    void action(const QString& actionId) override;

    [[nodiscard]] QString stepId() const;
    [[nodiscard]] QVariantList languages() const;
    [[nodiscard]] int languageIndex() const;
    Q_INVOKABLE void selectLanguage(int index);
    [[nodiscard]] bool showAtStartChoice() const;
    [[nodiscard]] bool showAtStart() const;
    void setShowAtStart(bool show);
    [[nodiscard]] bool settingsReadOnly() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString pathError() const { return m_pathError; }
    [[nodiscard]] QStringList commands() const;
    [[nodiscard]] QVariantList buttons() const;
    [[nodiscard]] QString fileDialogTitle() const;
    [[nodiscard]] QStringList fileNameFilters() const;
    [[nodiscard]] CheckListModel* summary() const { return m_summary; }
    [[nodiscard]] bool modeChoice() const;
    [[nodiscard]] static QVariantList modes();
    [[nodiscard]] int selectedMode() const noexcept { return m_selectedMode; }
    Q_INVOKABLE void selectMode(int mode);
    // ×, Esc, Cancel on Welcome store the selected mode (D-4).
    void aboutToClose() override;
    // "It is installed elsewhere…": the J-Link library (step jlink) or nrfutil (step nrfutil). The
    // path is checked first; only a passing check saves it.
    Q_INVOKABLE void choosePath(const QUrl& file);

    [[nodiscard]] bool running() const { return m_generation != 0; }
    // Tests.
    [[nodiscard]] ProbePickerViewModel* picker() const { return m_picker; }
    [[nodiscard]] bool startedFromError() const noexcept { return m_fromError; }

private:
    // What a step found (the Done summary); bounded: one per step.
    struct StepRecord {
        QString step;
        diag::CheckStatus status = diag::CheckStatus::Pending;
        QString text;
    };

    [[nodiscard]] std::vector<QString> stepIds() const;
    [[nodiscard]] WizardStep stepFor(const QString& stepId) const;
    [[nodiscard]] static std::vector<std::string> checkIdsFor(const QString& stepId);
    void fillWelcome(WizardStep& step) const;
    void fillJLink(WizardStep& step) const;
    void fillUsb(WizardStep& step) const;
    void fillNrfUtil(WizardStep& step) const;
    [[nodiscard]] QString installerHint() const;
    [[nodiscard]] diag::CheckStatus status(const char* checkId) const;
    [[nodiscard]] std::optional<ErrorCode> errorCode(const char* checkId) const;
    [[nodiscard]] bool stepPassed() const;
    [[nodiscard]] QVariantList firmwareActions() const;
    [[nodiscard]] bool customJLinkPath() const;
    void goTo(int index);
    void enterStep();
    void runChecks(std::optional<std::filesystem::path> candidate = std::nullopt);
    void onProgress(quint64 generation, const diag::CheckResult& result);
    void onFinished(quint64 generation, const std::vector<diag::CheckResult>& results);
    void onProbesChanged();
    void savePath(const QString& key, const QString& path);
    void complete(); // setup/completedVersion (not with read-only settings)
    void record();   // the current step's results into m_records
    void fillSummary();
    void connectDevice();
    void applyMode(); // plan 0056: the selected mode, when Welcome asks it

    AppViewModel& m_app;
    SettingsStore& m_settings;
    Options m_options;
    CheckListModel* m_checks;                 // child
    CheckListModel* m_summary;                // child
    ProbePickerViewModel* m_picker = nullptr; // child, created for the probe step
    std::vector<QString> m_steps;             // ids in order (bounded: 6)
    int m_index = 0;
    bool m_fromError = false;
    bool m_askMode = false;         // plan 0056: the mode was not chosen at the start (snapshot)
    int m_selectedMode = 0;         // UiMode value
    std::uint64_t m_requester = 0;  // DiagWorker::newRequester (ADR-0394)
    std::uint64_t m_generation = 0; // the running request (0 = none)
    std::shared_ptr<diag::EnvironmentFacts> m_pendingFacts;
    std::shared_ptr<const diag::EnvironmentFacts> m_facts; // of the last finished request
    std::optional<std::filesystem::path> m_candidate;      // the path being validated
    QString m_candidateStep;                               // its step
    QString m_pathError;
    std::vector<std::uint32_t> m_probeSerials; // of the picker, to re-check on a change
    std::vector<StepRecord> m_records;
};

// Registers the wizard (WizardId::FirstRunSetup, global), the control "setup-step" and the setting
// keys setup/completedVersion and setup/showAtStart.
void registerSetupWizard(AppViewModel& app);

} // namespace hwio::gui
