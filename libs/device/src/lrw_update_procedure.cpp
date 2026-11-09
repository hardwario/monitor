#include "hwio/device/lrw_update_procedure.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QtLogging>

#include <expected>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace hwio::device {

namespace {

// The board labels of the two connectors (Phase 0: the module's connector is "LoRaWAN").
QString appConnector() { return QStringLiteral("APP"); }
QString lrwConnector() { return QStringLiteral("LoRaWAN"); }

} // namespace

LrwUpdateProcedure::LrwUpdateProcedure(DeviceSession& session, Options options, QObject* parent)
    : QObject(parent), m_session(&session), m_options(options) {
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &LrwUpdateProcedure::poll);
    connect(&session, &DeviceSession::operationProgress, this, &LrwUpdateProcedure::onProgress);
    connect(&session, &DeviceSession::operationFinished, this, &LrwUpdateProcedure::onFinished);
    connect(&session, &QObject::destroyed, this, [this] {
        if (m_step != Step::Idle && m_step != Step::Done && m_step != Step::Failed &&
            m_step != Step::Cancelled) {
            m_operation.reset();
            fail(makeError(ErrorCode::TargetNotConnected, "the session was closed"));
        }
    });
}

LrwUpdateProcedure::~LrwUpdateProcedure() {
    m_pollTimer.stop();
    // An identify is cancelled; any other operation runs to its end in the session.
    if (m_operation && !m_session.isNull() &&
        (m_step == Step::ConnectApp || m_step == Step::ConnectLrw ||
         m_step == Step::ConnectAppAgain)) {
        cancelRunning();
    }
    m_hold.release();
}

std::string_view LrwUpdateProcedure::stepId(Step step) noexcept {
    switch (step) {
    case Step::Idle:
        return "idle";
    case Step::ConnectApp:
        return "connect-app";
    case Step::HoldApp:
        return "hold-app";
    case Step::ConnectLrw:
        return "connect-lrw";
    case Step::NeedsUnlockConfirmation:
        return "needs-unlock-confirmation";
    case Step::Unlock:
        return "unlock";
    case Step::Flash:
        return "flash";
    case Step::Erase:
        return "erase";
    case Step::ResetModule:
        return "reset-module";
    case Step::ConnectAppAgain:
        return "connect-app-again";
    case Step::ResetApp:
        return "reset-app";
    case Step::Done:
        return "done";
    case Step::Failed:
        return "failed";
    case Step::Cancelled:
        return "cancelled";
    }
    return "idle";
}

void LrwUpdateProcedure::cancelRunning() {
    if (m_operation && !m_session.isNull()) {
        if (Status cancelled = m_session->cancel(*m_operation); !cancelled) {
            qCInfo(log::lcFlash).noquote()
                << "LoRaWAN module: cancel refused:" << toQString(cancelled.error().detail);
        }
    }
}

LrwUpdateProcedure::Step LrwUpdateProcedure::connectorStepBefore(Step step) noexcept {
    switch (step) {
    case Step::HoldApp:
        return Step::ConnectApp;
    case Step::Unlock:
    case Step::Flash:
    case Step::Erase:
    case Step::ResetModule:
        return Step::ConnectLrw;
    default:
        return Step::ConnectAppAgain;
    }
}

bool LrwUpdateProcedure::operationRunning() const noexcept {
    switch (m_step) {
    case Step::HoldApp:
    case Step::Unlock:
    case Step::Flash:
    case Step::Erase:
    case Step::ResetModule:
    case Step::ResetApp:
        return true;
    default:
        return false;
    }
}

// ---- control ----------------------------------------------------------------------------------

Status LrwUpdateProcedure::start(Request request) {
    if (m_step != Step::Idle) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         "the LoRaWAN module operation was started already"));
    }
    if (m_session.isNull() || findTarget(m_session->profile(), appTargetId) == nullptr ||
        findTarget(m_session->profile(), lrwTargetId) == nullptr) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         "the device profile has no `app` and `lrw` targets"));
    }
    if (request.action == Action::Flash && (!request.image || request.image->empty())) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "no firmware for the LoRaWAN module"));
    }
    auto hold = m_session->hold();
    if (!hold) {
        return std::unexpected(std::move(hold).error());
    }
    m_hold = std::move(*hold);
    m_request = std::move(request);
    m_request.options.halt = false;
    QString what = QStringLiteral("unlock");
    if (m_request.action == Action::Flash) {
        what = QStringLiteral("flash ") + toQString(m_request.name);
    } else if (m_request.action == Action::Erase) {
        what = QStringLiteral("erase");
    } else if (m_request.action == Action::Reset) {
        what = QStringLiteral("reset");
    }
    qCInfo(log::lcFlash).noquote() << "LoRaWAN module operation started:" << what;
    startConnectorStep(Step::ConnectApp);
    return {};
}

Status LrwUpdateProcedure::confirmUnlock() {
    if (m_step != Step::NeedsUnlockConfirmation) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         "the LoRaWAN module is not waiting for an unlock"));
    }
    qCInfo(log::lcFlash) << "LoRaWAN module: unlock confirmed";
    startUnlock();
    return {};
}

bool LrwUpdateProcedure::canCancel() const noexcept {
    switch (m_step) {
    case Step::ConnectApp:
    case Step::ConnectLrw:
    case Step::NeedsUnlockConfirmation:
    case Step::ConnectAppAgain:
        return true;
    case Step::Failed:
        return canRetry(); // a retryable failure is still in the procedure
    case Step::Idle:
    case Step::HoldApp:
    case Step::Unlock:
    case Step::Flash:
    case Step::Erase:
    case Step::ResetModule:
    case Step::ResetApp:
    case Step::Done:
    case Step::Cancelled:
        break;
    }
    return false;
}

bool LrwUpdateProcedure::canRetry() const noexcept {
    switch (m_failedStep) {
    case Step::HoldApp:
    case Step::Unlock:
    case Step::Flash:
    case Step::Erase:
    case Step::ResetModule:
    case Step::ResetApp:
        return m_step == Step::Failed && !m_session.isNull();
    default:
        return false;
    }
}

bool LrwUpdateProcedure::cancel() {
    if (!canCancel()) {
        return false;
    }
    m_pollTimer.stop();
    qCInfo(log::lcFlash).noquote()
        << "LoRaWAN module operation cancelled in" << toQString(stepId(m_step))
        << (m_appHeld ? "(the application MCU is halted)" : "");
    if (m_operation) {
        cancelRunning(); // an identify: the session ends it soon; its result is ignored
    }
    m_operation.reset();
    m_error.reset();
    finishWith(Step::Cancelled);
    return true;
}

void LrwUpdateProcedure::retry() {
    if (!canRetry()) {
        return;
    }
    m_error.reset();
    startConnectorStep(connectorStepBefore(m_failedStep));
}

// ---- steps ------------------------------------------------------------------------------------

void LrwUpdateProcedure::setStep(Step step) {
    if (step != m_step) {
        qCInfo(log::lcFlash).noquote() << "LoRaWAN module step:" << toQString(stepId(step));
    }
    m_step = step;
    changed(); // signal
}

void LrwUpdateProcedure::finishWith(Step step) {
    setStep(step);
    // Releasing the hold reconnects the console on the application MCU (ADR-0234 D4).
    m_hold.release();
    finished(); // signal
}

void LrwUpdateProcedure::fail(Error error) {
    m_pollTimer.stop();
    qCWarning(log::lcFlash).noquote()
        << "LoRaWAN module operation failed in" << toQString(stepId(m_step)) << ":"
        << toQString(idOf(error.code)) << toQString(error.detail);
    m_failedStep = m_step;
    m_error = std::move(error);
    setStep(Step::Failed);
    if (!canRetry()) {
        m_hold.release();
    }
    finished(); // signal
}

bool LrwUpdateProcedure::startOperation(const Result<OperationId>& id) {
    if (!id) {
        fail(id.error());
        return false;
    }
    m_operation = *id;
    m_progress = OperationProgress{};
    progressChanged(); // signal
    return true;
}

void LrwUpdateProcedure::startConnectorStep(Step step) {
    setStep(step);
    poll(); // the first check at once
}

void LrwUpdateProcedure::poll() {
    if (m_operation || m_session.isNull()) {
        return; // one identify in flight
    }
    std::string_view target;
    switch (m_step) {
    case Step::ConnectApp:
    case Step::ConnectAppAgain:
        target = appTargetId;
        break;
    case Step::ConnectLrw:
        target = lrwTargetId;
        break;
    default:
        return;
    }
    auto id = m_session->identifyTarget(target);
    if (!id) {
        // Another operation of the session (internal.busy) or a full queue: try again later.
        qCInfo(log::lcFlash).noquote()
            << "LoRaWAN module: identify not started:" << toQString(id.error().detail);
        m_pollTimer.start(m_options.pollInterval);
        return;
    }
    m_operation = *id;
}

void LrwUpdateProcedure::afterConnector(const OperationResult& result) {
    const std::string_view wanted = m_step == Step::ConnectLrw ? lrwTargetId : appTargetId;
    if (!result.status || !result.familyMatch || result.familyMatch->foundTargetId != wanted) {
        m_pollTimer.start(m_options.pollInterval); // not there (yet): ask again
        return;
    }
    switch (m_step) {
    case Step::ConnectApp:
        startHold();
        return;
    case Step::ConnectLrw:
        m_module = result.chip;
        qCInfo(log::lcFlash).noquote()
            << "LoRaWAN module found, RDP level" << (m_module ? m_module->rdpLevel : 0);
        if (m_module && m_module->rdpLevel == 1 &&
            (m_request.action == Action::Flash || m_request.action == Action::Erase)) {
            // ADR-0276: the unlock erases the module (e.g. the factory Murata firmware); the
            // owner must confirm it. A reset works on a protected module.
            setStep(Step::NeedsUnlockConfirmation);
            return;
        }
        if (m_request.action == Action::Unlock) {
            startUnlock();
        } else {
            startAction();
        }
        return;
    case Step::ConnectAppAgain:
        startResetApp();
        return;
    default:
        return;
    }
}

void LrwUpdateProcedure::startHold() {
    setStep(Step::HoldApp);
    (void)startOperation(m_session->reset(appTargetId, ResetMode::Halt));
}

void LrwUpdateProcedure::startUnlock() {
    setStep(Step::Unlock);
    (void)startOperation(m_session->unlock(lrwTargetId));
}

void LrwUpdateProcedure::startAction() {
    switch (m_request.action) {
    case Action::Flash:
        setStep(Step::Flash);
        (void)startOperation(m_session->flash(lrwTargetId, m_request.image, m_request.options));
        return;
    case Action::Erase:
        setStep(Step::Erase);
        (void)startOperation(m_session->erase(lrwTargetId, EraseScope::All));
        return;
    case Action::Unlock:
        startUnlock();
        return;
    case Action::Reset:
        setStep(Step::ResetModule);
        (void)startOperation(m_session->reset(lrwTargetId, ResetMode::Go));
        return;
    }
}

void LrwUpdateProcedure::startResetApp() {
    setStep(Step::ResetApp);
    (void)startOperation(m_session->reset(appTargetId, ResetMode::Go));
}

// ---- session events ---------------------------------------------------------------------------

void LrwUpdateProcedure::onProgress(OperationId id, const OperationProgress& progress) {
    if (!m_operation || id != *m_operation || !operationRunning()) {
        return;
    }
    m_progress = progress;
    progressChanged(); // signal
}

void LrwUpdateProcedure::onFinished(OperationId id, const OperationResult& result) {
    if (!m_operation || id != *m_operation) {
        return;
    }
    m_operation.reset();
    // The cable moved after the connector check (the reset found the other MCU before it did
    // anything): back to waiting for the APP connector.
    if (!result.status && result.status.error().code == ErrorCode::TargetWrongFamily &&
        (m_step == Step::HoldApp || m_step == Step::ResetApp)) {
        qCInfo(log::lcFlash) << "LoRaWAN module: the J-Link is on another connector again";
        startConnectorStep(connectorStepBefore(m_step));
        return;
    }
    switch (m_step) {
    case Step::ConnectApp:
    case Step::ConnectLrw:
    case Step::ConnectAppAgain:
        afterConnector(result);
        return;
    case Step::HoldApp:
        if (!result.status) {
            fail(result.status.error());
            return;
        }
        m_appHeld = true;
        startConnectorStep(Step::ConnectLrw);
        return;
    case Step::Unlock:
        if (!result.status) {
            fail(result.status.error());
            return;
        }
        m_unlocked = true;
        if (result.chip) {
            m_module = result.chip;
        }
        if (m_request.action == Action::Unlock) {
            startConnectorStep(Step::ConnectAppAgain);
        } else {
            startAction();
        }
        return;
    case Step::Flash:
    case Step::Erase:
    case Step::ResetModule:
        if (!result.status) {
            fail(result.status.error());
            return;
        }
        startConnectorStep(Step::ConnectAppAgain);
        return;
    case Step::ResetApp:
        if (!result.status) {
            fail(result.status.error());
            return;
        }
        m_appHeld = false;
        finishWith(Step::Done);
        return;
    case Step::Idle:
    case Step::NeedsUnlockConfirmation:
    case Step::Done:
    case Step::Failed:
    case Step::Cancelled:
        return;
    }
}

// ---- texts ------------------------------------------------------------------------------------

QString LrwUpdateProcedure::connectorLabel() const {
    switch (m_step) {
    case Step::ConnectApp:
    case Step::ConnectAppAgain:
        return appConnector();
    case Step::ConnectLrw:
        return lrwConnector();
    default:
        return {};
    }
}

QString LrwUpdateProcedure::actionDoneText() const {
    switch (m_request.action) {
    case Action::Flash:
        return tr("The LoRaWAN module was flashed with %1 and the application is running again.")
            .arg(toQString(m_request.name));
    case Action::Erase:
        return tr("The LoRaWAN module was erased and the application is running again.");
    case Action::Unlock:
        return tr("The LoRaWAN module was unlocked and the application is running again.");
    case Action::Reset:
        return tr("The LoRaWAN module was reset and the application is running again.");
    }
    return {};
}

QString LrwUpdateProcedure::promptText() const {
    switch (m_step) {
    case Step::Idle:
        return {};
    case Step::ConnectApp:
        return tr("Connect the J-Link cable to the %1 connector.").arg(appConnector());
    case Step::HoldApp:
        return tr("Stopping the application MCU…");
    case Step::ConnectLrw:
        return tr("The application MCU is stopped. Move the J-Link cable to the %1 connector of "
                  "the LoRaWAN module.")
            .arg(lrwConnector());
    case Step::NeedsUnlockConfirmation:
        return tr("The LoRaWAN module is read-out protected. Unlocking erases it completely; the "
                  "module's original Murata firmware is removed.");
    case Step::Unlock:
        return tr("Unlocking the LoRaWAN module…");
    case Step::Flash:
        return tr("Flashing the LoRaWAN module…");
    case Step::Erase:
        return tr("Erasing the LoRaWAN module…");
    case Step::ResetModule:
        return tr("Resetting the LoRaWAN module…");
    case Step::ConnectAppAgain:
        return tr("Move the J-Link cable back to the %1 connector.").arg(appConnector());
    case Step::ResetApp:
        return tr("Starting the application MCU…");
    case Step::Done:
        return actionDoneText();
    case Step::Failed:
        return m_error ? toQString(m_error->message) : QString();
    case Step::Cancelled:
        return m_appHeld ? tr("The LoRaWAN module operation was cancelled. The application MCU "
                              "stays stopped until CHESTER is reset or powered off and on.")
                         : tr("The LoRaWAN module operation was cancelled.");
    }
    return {};
}

} // namespace hwio::device
