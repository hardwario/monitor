#pragma once

// LteUpdateWizard - the guided LTE firmware update of a J-Link tab (plan/features/0033-rc2-feedback
// .md C, ADR-0234; wizard framework of plan 0007, WizardId::LteUpdate "lte-update").
//
// A thin adapter: step 1 chooses the LTE firmware (an LTE-kind FirmwareSourceViewModel, the same
// source part as the Flash page: the designated HARDWARIO LTE firmware or an own .hex / bundle /
// modem zip, nRF Util prerequisite inline); [Next] starts device::LteUpdateProcedure on the tab's
// session, and the wizard only shows the procedure's step, prompt, progress and error. Once the APP
// connector answers it waits for [Continue] (Confirm, "Ready to update the LTE MCU": the
// application will be erased, the cable moves to LTE and back; ADR-0366). Steps:
//   1 Firmware · 2 APP connector · 3 Stop the application · 4 LTE connector · 5 Update the LTE MCU
//   · 6 APP connector · 7 Application (the copy, or firmware from the catalog / a file) · 8 Done
// Every step but Done is an Embedded step "lte-update" (LteUpdateStep.qml) bound to this object.
// While the procedure runs the tab is busy ("Paused for the LTE firmware update"; the session is
// held by the procedure, D4). Cancel: not while a flash or the erase runs (canCancel); after the
// erase it asks first ("The application MCU is erased…", with the path of the copy).
//
// Plan 0044 (ADR-0363): opened for a remote lteUpdate request (RemoteShareViewModel's
// takeLteUpdateRequest() at start()), the wizard selects the recommended (designated) firmware and
// starts by itself once it is downloaded and nRF Util was found; a download or nRF Util problem is
// the request's result (the wizard stays open on step 1). The request follows the procedure
// (LteUpdateReporter) and ends when the wizard closes. Plan 0046 (ADR-0366): like a local update
// it stops at Confirm until the customer clicks [Continue] (the reporter's heartbeat keeps the
// joiner's request alive meanwhile). ADR-0368: when support answers (the joiner said so), Confirm
// and BackupFailed show no button (support's answer moves on, a no cancels), and the copy is always
// written back; without a copy support is told at once.
//
// Ownership: created by the WizardRegistry factory with the WizardViewModel as Qt parent; keeps
// the tab as QPointer (wizard contract). GUI thread only.

#include "hwio/device/lte_update_procedure.h"
#include "hwio/device/operation.h"
#include "viewmodels/error_info.h"
#include "wizard/wizard.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration>

#include <memory>
#include <vector>

namespace hwio::remote {
class LteUpdateReporter;
} // namespace hwio::remote

namespace hwio::gui {

class DeviceTabViewModel;
class FirmwareServices;
class FirmwareSourceViewModel;

class LteUpdateWizard : public Wizard {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the wizard registry")

    // Which part LteUpdateStep.qml shows (Stage).
    Q_PROPERTY(int stage READ stage NOTIFY changed)
    Q_PROPERTY(hwio::gui::FirmwareSourceViewModel* lteSource READ lteSource CONSTANT)
    Q_PROPERTY(hwio::gui::FirmwareSourceViewModel* appSource READ appSource CONSTANT)
    Q_PROPERTY(QString prerequisiteText READ prerequisiteText NOTIFY changed)
    Q_PROPERTY(QString nrfUtilDownloadUrl READ nrfUtilDownloadUrl CONSTANT) // plan 0038 A0
    // The text of the running step (plain text: it may name connectors and paths).
    Q_PROPERTY(QString promptText READ promptText NOTIFY changed)
    Q_PROPERTY(QString illustration READ illustration NOTIFY changed) // slot id or ""
    Q_PROPERTY(bool wrongConnector READ wrongConnector NOTIFY changed)
    // Progress of the running operation: [{title, state}], step text, 0..1 (-1 = indeterminate).
    Q_PROPERTY(QVariantList phases READ phases NOTIFY progressChanged)
    Q_PROPERTY(QString stepText READ stepText NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    // Step 7: write back the copy (true) or flash appSource.
    Q_PROPERTY(bool useBackup READ useBackup WRITE setUseBackup NOTIFY changed)
    Q_PROPERTY(bool hasBackup READ hasBackup NOTIFY changed)
    Q_PROPERTY(QString backupText READ backupText NOTIFY changed)
    Q_PROPERTY(hwio::gui::ErrorInfo error READ error NOTIFY changed)

public:
    // Unscoped: QML compares LteUpdateWizard.Firmware etc. with the int property.
    enum Stage : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        Firmware,         // step 1: the LTE source
        Connector,        // steps 2, 4, 6: picture + prompt, advances by itself
        Confirm,          // step 2 answered: what will happen + [Continue] (plan 0046, ADR-0366)
        Working,          // backup, erase, LTE update, restore: prompt + progress
        BackupFailed,     // the error + [Continue without a copy]
        Application,      // step 7: the copy or another firmware
        Failed,           // the error (+ [Try again] when the procedure can retry)
        Summary,          // step 8
    };
    Q_ENUM(Stage)

    LteUpdateWizard(FirmwareServices& services, QObject* parent);
    ~LteUpdateWizard() override;
    LteUpdateWizard(const LteUpdateWizard&) = delete;
    LteUpdateWizard& operator=(const LteUpdateWizard&) = delete;
    LteUpdateWizard(LteUpdateWizard&&) = delete;
    LteUpdateWizard& operator=(LteUpdateWizard&&) = delete;

    [[nodiscard]] QString id() const override { return QStringLiteral("lte-update"); }
    [[nodiscard]] QString title() const override;
    void start(const ErrorInfo& context, DeviceTabViewModel* tab) override;
    [[nodiscard]] std::vector<WizardStep> plannedSteps() const override;
    [[nodiscard]] WizardStep current() const override;
    void primary() override;
    void back() override;
    [[nodiscard]] bool canBack() const override;
    [[nodiscard]] bool canPrimary() const override;
    [[nodiscard]] bool canCancel() const override;
    [[nodiscard]] QString busyCancelText() const override;
    [[nodiscard]] QString cancelAcceptText() const override;
    [[nodiscard]] QString cancelRejectText() const override;
    void cancelOperation() override;
    [[nodiscard]] QObject* embeddedViewModel() override { return this; }
    void action(const QString& actionId) override;

    [[nodiscard]] int stage() const;
    [[nodiscard]] FirmwareSourceViewModel* lteSource() const { return m_lteSource; }
    [[nodiscard]] FirmwareSourceViewModel* appSource() const { return m_appSource; }
    [[nodiscard]] QString prerequisiteText() const;
    [[nodiscard]] static QString nrfUtilDownloadUrl();
    [[nodiscard]] QString promptText() const;
    [[nodiscard]] QString illustration() const;
    [[nodiscard]] bool wrongConnector() const;
    [[nodiscard]] QVariantList phases() const;
    [[nodiscard]] QString stepText() const;
    [[nodiscard]] qreal progress() const;
    [[nodiscard]] bool useBackup() const noexcept { return m_useBackup; }
    void setUseBackup(bool use);
    [[nodiscard]] bool hasBackup() const;
    [[nodiscard]] QString backupText() const;
    [[nodiscard]] ErrorInfo error() const;

    // The procedure (tests).
    [[nodiscard]] device::LteUpdateProcedure* procedure() const { return m_procedure.get(); }
    // ADR-0368: a remote update whose questions HARDWARIO Support answers (no customer button).
    [[nodiscard]] bool supportDecides() const;

signals:
    void progressChanged();

private:
    // The step index of plannedSteps() for the procedure's state.
    [[nodiscard]] int stepIndex() const;
    [[nodiscard]] bool needsNrfUtil() const;
    [[nodiscard]] bool firmwareReady() const;
    void startProcedure();
    void onProcedureChanged();
    void onProcedureFinished();
    void releaseTab();
    // Plan 0044 (ADR-0363): the remote request's automatic start.
    void checkRemoteStart();

    FirmwareServices& m_services;
    QPointer<DeviceTabViewModel> m_tab;
    FirmwareSourceViewModel* m_lteSource = nullptr; // child
    FirmwareSourceViewModel* m_appSource = nullptr; // child
    std::unique_ptr<device::LteUpdateProcedure> m_procedure;
    std::optional<Error> m_startError; // the procedure could not start
    bool m_useBackup = true;
    bool m_tabBusy = false;
    QPointer<remote::LteUpdateReporter> m_remote; // owned by the tab's RemoteShareViewModel
    bool m_remoteStarted = false;
};

void registerLteUpdateWizard(class AppViewModel& app, FirmwareServices& services);

} // namespace hwio::gui
