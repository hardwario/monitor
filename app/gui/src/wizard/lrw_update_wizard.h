#pragma once

// LrwUpdateWizard - the guided operations on the CHESTER LoRaWAN module of a J-Link tab
// (plan/features/0040-chester-lrw-module.md B1, B2; ADR-0283; wizard framework of plan 0007,
// WizardId::LrwUpdate "lrw-update").
//
// A thin adapter: the Flash page chooses what to do (the LoRaWAN card's Flash: the designated
// lora-modem firmware or an own file; the `lrw` maintenance actions Erase chip, Unlock) and
// opens this wizard with a pending request (FlashViewModel::takeLrwRequest()). start() runs
// device::LrwUpdateProcedure on the tab's session at once; the wizard only shows the procedure's
// step, prompt, progress and error. Steps:
//   1 APP connector · 2 Stop the application · 3 LoRaWAN connector · 4 Flash | Erase | Unlock
//   (with the unlock confirmation of a read-out protected module) · 5 APP connector · 6 Done
// Every step but Done is an Embedded step "lrw-update" (LrwUpdateStep.qml) bound to this object.
// While the procedure runs the tab is busy ("Paused for the LoRaWAN module"; the session is held
// by the procedure). Cancel: not while an operation runs (canCancel); while the application MCU
// is held it asks first.
//
// Ownership: created by the WizardRegistry factory with the WizardViewModel as Qt parent; keeps
// the tab as QPointer (wizard contract). GUI thread only.

#include "hwio/core/error.h"
#include "hwio/device/lrw_update_procedure.h"
#include "viewmodels/error_info.h"
#include "wizard/wizard.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration>

#include <memory>
#include <optional>
#include <vector>

namespace hwio::gui {

class AppViewModel;
class DeviceTabViewModel;
class FirmwareServices;

class LrwUpdateWizard : public Wizard {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the wizard registry")

    // Which part LrwUpdateStep.qml shows (Stage).
    Q_PROPERTY(int stage READ stage NOTIFY changed)
    // The text of the running step (plain text: it may name a file).
    Q_PROPERTY(QString promptText READ promptText NOTIFY changed)
    Q_PROPERTY(QString illustration READ illustration NOTIFY changed) // slot id or ""
    // Progress of the running operation: [{title, state}], 0..1 (-1 = indeterminate).
    Q_PROPERTY(QVariantList phases READ phases NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(hwio::gui::ErrorInfo error READ error NOTIFY changed)

public:
    // Unscoped: QML compares LrwUpdateWizard.Connector etc. with the int property.
    enum Stage : quint8 {   // NOLINT(cppcoreguidelines-use-enum-class)
        Connector,          // steps 1, 3, 5: picture + prompt, advances by itself
        Working,            // stop the application, the module operation, start the application
        UnlockConfirmation, // step 4: a read-out protected module before a flash or erase
        Failed,             // the error (+ [Try again] when the procedure can retry)
        Summary,            // step 6
    };
    Q_ENUM(Stage)

    LrwUpdateWizard(FirmwareServices& services, QObject* parent);
    ~LrwUpdateWizard() override;
    LrwUpdateWizard(const LrwUpdateWizard&) = delete;
    LrwUpdateWizard& operator=(const LrwUpdateWizard&) = delete;
    LrwUpdateWizard(LrwUpdateWizard&&) = delete;
    LrwUpdateWizard& operator=(LrwUpdateWizard&&) = delete;

    [[nodiscard]] QString id() const override { return QStringLiteral("lrw-update"); }
    [[nodiscard]] QString title() const override;
    // Takes the Flash page's request and starts the procedure (no request: Failed).
    void start(const ErrorInfo& context, DeviceTabViewModel* tab) override;
    [[nodiscard]] std::vector<WizardStep> plannedSteps() const override;
    [[nodiscard]] WizardStep current() const override;
    void primary() override;
    void back() override {}
    [[nodiscard]] bool canBack() const override { return false; } // the procedure only moves on
    [[nodiscard]] bool canPrimary() const override;
    [[nodiscard]] bool canCancel() const override;
    [[nodiscard]] QString busyCancelText() const override;
    [[nodiscard]] QString cancelAcceptText() const override;
    [[nodiscard]] QString cancelRejectText() const override;
    void cancelOperation() override;
    [[nodiscard]] QObject* embeddedViewModel() override { return this; }
    void action(const QString& actionId) override;

    [[nodiscard]] int stage() const;
    [[nodiscard]] QString promptText() const;
    [[nodiscard]] QString illustration() const;
    [[nodiscard]] QVariantList phases() const;
    [[nodiscard]] qreal progress() const;
    [[nodiscard]] ErrorInfo error() const;

    // The procedure (tests).
    [[nodiscard]] device::LrwUpdateProcedure* procedure() const { return m_procedure.get(); }

signals:
    void progressChanged();

private:
    // The step index of plannedSteps() for the procedure's state.
    [[nodiscard]] int stepIndex() const;
    [[nodiscard]] device::LrwUpdateProcedure::Action requestAction() const;
    [[nodiscard]] QString workingHeading() const;
    [[nodiscard]] WizardStep doneStep(WizardStep step) const;
    void startProcedure(device::LrwUpdateProcedure::Request request);
    void onProcedureFinished();
    void releaseTab();

    QPointer<DeviceTabViewModel> m_tab;
    std::unique_ptr<device::LrwUpdateProcedure> m_procedure;
    std::optional<device::LrwUpdateProcedure::Action> m_action; // of the request
    std::optional<Error> m_startError;                          // the procedure could not start
    bool m_tabBusy = false;
};

void registerLrwUpdateWizard(AppViewModel& app, FirmwareServices& services);

} // namespace hwio::gui
