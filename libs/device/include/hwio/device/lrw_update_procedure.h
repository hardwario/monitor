#pragma once

// LrwUpdateProcedure - the guided operations on the CHESTER LoRaWAN module (plan/features/
// 0040-chester-lrw-module.md A5, ADR-0283). The module (Murata Type ABZ, STM32L072CZ, target `lrw`)
// answers on its own SWD connector only while the application MCU (target `app`) is held in reset
// + halt: the running application drives the module's reset / power. A headless state machine on
// the owner thread of a J-Link DeviceSession, modelled on LteUpdateProcedure; the GUI LoRaWAN card
// and `hwio chester lrw flash|erase|unlock` drive it and only show what it says (texts translated
// here, ADR-0027).
//
//   ConnectApp        the probe must reach the application MCU: identifyTarget(app) every
//                     pollInterval, one in flight, until it matches (or cancel)
//   HoldApp           reset(app, Halt): the application MCU stops; nothing is erased
//   ConnectLrw        as ConnectApp for the module (the user moves the cable to its connector)
//   NeedsUnlockConfirmation
//                     a flash or erase found the module read-out protected (RDP level 1, e.g. the
//                     factory Murata firmware): waits for confirmUnlock() (ADR-0276: the unlock
//                     mass-erases the module) or cancel()
//   Unlock            unlock(lrw) - the action Unlock, or before Flash / Erase after the
//                     confirmation
//   Flash | Erase     flash(lrw, image) (programs, verifies) | erase(lrw, All)
//   ConnectAppAgain   as ConnectApp (the user moves the cable back)
//   ResetApp          reset(app, Go): the application runs again (and restarts the module)
//   Done | Failed | Cancelled
// The session is held (SessionHold, ADR-0234 D4) from start() until Done or Cancelled (and while
// this object lives): RTT and auto-reconnect are paused; releasing the hold reconnects the
// console. A failed HoldApp, Unlock, Flash, Erase or ResetApp can be retried (back to the
// connector step before it); HoldApp and ResetApp that find the other MCU (target.wrong_family:
// the cable moved after the check) go back to their connector step by themselves. cancel() is
// refused while an operation other than an identify runs (a flash, erase or unlock is never
// interrupted here).
//
// The targets are named (ADR-0283): the session's console target `app` and the profile's target
// `lrw`; the connector labels are "APP" and "LoRaWAN" (the module's connector on the board).
//
// Ownership: a QObject on the session's owner thread; it keeps the session as a QPointer (a closed
// session ends the procedure as Failed). Bounds: one session operation at a time.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/hw/jlink/programmer.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::device {

struct TargetProfile;

class LrwUpdateProcedure : public QObject {
    Q_OBJECT

public:
    enum class Step : std::uint8_t {
        Idle,
        ConnectApp,
        HoldApp,
        ConnectLrw,
        NeedsUnlockConfirmation,
        Unlock,
        Flash,
        Erase,
        ConnectAppAgain,
        ResetApp,
        Done,
        Failed,
        Cancelled,
    };

    enum class Action : std::uint8_t { Flash, Erase, Unlock };

    struct Request {
        Action action = Action::Flash;
        std::shared_ptr<const firmware::FirmwareImage> image; // Flash: the file or the prepared one
        std::string name; // Flash: file name or "lora-modem for CHESTER 1.4.1" (logs, texts)
        // Flash: allowUicr (= data EEPROM records, ADR-0275) and eraseAll; `halt` is ignored (the
        // application MCU's reset at the end restarts the module).
        FlashOptions options;
    };

    struct Options {
        std::chrono::milliseconds pollInterval{1000};
    };

    static constexpr std::string_view appTargetId = "app";
    static constexpr std::string_view lrwTargetId = "lrw";

    LrwUpdateProcedure(DeviceSession& session, Options options, QObject* parent = nullptr);
    ~LrwUpdateProcedure() override; // cancels a running identify; releases the hold
    LrwUpdateProcedure(const LrwUpdateProcedure&) = delete;
    LrwUpdateProcedure& operator=(const LrwUpdateProcedure&) = delete;
    LrwUpdateProcedure(LrwUpdateProcedure&&) = delete;
    LrwUpdateProcedure& operator=(LrwUpdateProcedure&&) = delete;

    // Holds the session and waits for the application MCU. Errors: usage.invalid_argument
    // (already started, Flash without an image or with an empty one, a profile without the `app`
    // and `lrw` targets), those of hold().
    [[nodiscard]] Status start(Request request);
    // NeedsUnlockConfirmation: unlock (the module mass-erases), then the action. Errors:
    // usage.invalid_argument (not waiting for it).
    [[nodiscard]] Status confirmUnlock();
    // Refused (false) while HoldApp, Unlock, Flash, Erase or ResetApp runs and after the end.
    bool cancel();
    // Failed after HoldApp, Unlock, Flash, Erase or ResetApp: back to that step's connector step.
    void retry();

    [[nodiscard]] Step step() const noexcept { return m_step; }
    // Failed: the step that failed (Idle otherwise).
    [[nodiscard]] Step failedStep() const noexcept {
        return m_step == Step::Failed ? m_failedStep : Step::Idle;
    }
    [[nodiscard]] bool canCancel() const noexcept;
    [[nodiscard]] bool canRetry() const noexcept;
    [[nodiscard]] const Request& request() const noexcept { return m_request; }
    // The application MCU was halted by this procedure and not started again yet.
    [[nodiscard]] bool applicationHeld() const noexcept { return m_appHeld; }
    // The module as ConnectLrw found it (name, DEV_ID, RDP level); nullopt before.
    [[nodiscard]] const std::optional<jlink::ChipInfo>& module() const noexcept { return m_module; }
    // The module was unlocked (mass-erased) by this procedure.
    [[nodiscard]] bool unlocked() const noexcept { return m_unlocked; }
    // The text of the current step (translated): the connector prompt, "Flashing the LoRaWAN
    // module…", the result, ...
    [[nodiscard]] QString promptText() const;
    // The connector label of a connector step ("APP", "LoRaWAN"); "" otherwise.
    [[nodiscard]] QString connectorLabel() const;
    // Failed: what went wrong.
    [[nodiscard]] const std::optional<Error>& error() const noexcept { return m_error; }
    // The last progress of the running operation (phases, bytes).
    [[nodiscard]] const OperationProgress& progress() const noexcept { return m_progress; }

    [[nodiscard]] static std::string_view stepId(Step step) noexcept; // "connect-app", ... (logs)

signals:
    void changed();         // step, prompt, error
    void progressChanged(); // progress()
    void finished();        // Done, Failed or Cancelled was reached

private:
    void setStep(Step step);
    void fail(Error error);
    void finishWith(Step step);
    void startConnectorStep(Step step);
    void poll();
    void afterConnector(const OperationResult& result);
    void startHold();
    void startUnlock();
    void startAction();
    void startResetApp();
    void cancelRunning();
    [[nodiscard]] static Step connectorStepBefore(Step step) noexcept;
    [[nodiscard]] bool startOperation(const Result<OperationId>& id);
    void onProgress(OperationId id, const OperationProgress& progress);
    void onFinished(OperationId id, const OperationResult& result);
    [[nodiscard]] bool operationRunning() const noexcept;
    [[nodiscard]] QString actionDoneText() const;

    QPointer<DeviceSession> m_session;
    Options m_options;
    SessionHold m_hold;
    Request m_request;
    Step m_step = Step::Idle;
    Step m_failedStep = Step::Idle;
    std::optional<OperationId> m_operation; // the one in flight
    bool m_appHeld = false;
    bool m_unlocked = false;
    std::optional<jlink::ChipInfo> m_module;
    std::optional<Error> m_error;
    OperationProgress m_progress;
    QTimer m_pollTimer;
};

} // namespace hwio::device
