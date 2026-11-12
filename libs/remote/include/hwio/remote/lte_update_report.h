#pragma once

// LteUpdateReporter - the progress and the result of a guided LTE update run for a remote lteUpdate
// request (ADR-0363), shared by the GUI runner (the LTE update wizard) and the CLI runner (the
// terminal flow of `hwio chester lte flash --guided`).
//
// Progress: the phase is the procedure's step (device::LteUpdateProcedure::stepId), with the
// bytes of a running read / erase / flash; download() reports the firmware download before it
// ("download"). The last update is sent again every heartbeat while nothing changes (the customer
// moves the cable), so that the joiner's inactivity timeout does not end the request.
//
// Result (once): Done → ok; Cancelled after the LTE MCU was updated (the application not written
// back, e.g. the guided update ended without a copy of the application) →
// remote.application_not_restored; Cancelled before → cancelled; Failed → the procedure's error.
// A failure the procedure can retry (the GUI wizard's [Try again]) is reported only by finish()
// (the wizard closes); every other end is reported when the procedure finishes.
//
// Support decides (ADR-0368, setAskSupport()): when the followed procedure reaches Confirm or
// BackupFailed, the reporter asks support once per step; on the next turn of the event loop yes
// runs confirm() / continueWithoutBackup(), no (also no answer, a closed session) cancel() - the
// customer is not asked (supportDecides() tells the GUI and the terminal flow).
//
// Owner thread of the procedure only. The procedure must outlive follow() or be followed by
// another one / abandon() first (it is kept as a QPointer anyway).

#include "hwio/core/result.h"
#include "hwio/device/lte_update_procedure.h"
#include "hwio/remote/host_operations.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

namespace hwio::remote {

class LteUpdateReporter : public QObject {
    Q_OBJECT

public:
    static constexpr std::chrono::seconds defaultHeartbeat{60};

    LteUpdateReporter(LteUpdateRunner::Progress progress, LteUpdateRunner::Finished finished,
                      std::chrono::milliseconds heartbeat = defaultHeartbeat,
                      QObject* parent = nullptr);

    // Follows `procedure` (started, or started right after this call).
    void follow(device::LteUpdateProcedure& procedure);
    // The firmware download before the procedure.
    void download(std::uint64_t done, std::uint64_t total);
    // Reports the end now (the wizard closed, the terminal flow ended): the followed procedure's
    // state, cancelled without one. Nothing when already reported.
    void finish();
    // Reports `status` (an error before the procedure: the download, nRF Util). Once.
    void finish(Status status);
    // No more callbacks.
    void abandon();
    // ADR-0368: support answers the update's questions (empty = the customer does).
    void setAskSupport(AskSupport ask) { m_ask = std::move(ask); }
    [[nodiscard]] bool supportDecides() const noexcept { return static_cast<bool>(m_ask); }
    // A question to support is open (asked, its answer not applied yet).
    [[nodiscard]] bool supportQuestionOpen() const noexcept { return m_questionOpen; }
    // Why support's answer cancelled the update before anything changed, for the customer:
    // cancelledBySupportText() (No), supportDidNotAnswerText() (no answer); "" otherwise.
    [[nodiscard]] QString cancelledText() const;
    [[nodiscard]] bool done() const noexcept { return m_done; }
    // The LTE MCU got its new firmware (the procedure passed its LTE update).
    [[nodiscard]] bool lteUpdated() const noexcept { return m_lteUpdated; }

signals:
    // supportQuestionOpen() or supportDecides() changed (asked, answered and applied, abandoned).
    void supportQuestionChanged();

private:
    void onChanged();
    void onProgress();
    void onFinished();
    void report(const OperationUpdate& update);
    void askSupport(device::LteUpdateProcedure::Step step);
    [[nodiscard]] Status result() const;

    LteUpdateRunner::Progress m_progress;
    LteUpdateRunner::Finished m_finished;
    QPointer<device::LteUpdateProcedure> m_procedure;
    OperationUpdate m_last;
    bool m_lteUpdated = false;
    bool m_done = false;
    QTimer m_heartbeat{this};
    AskSupport m_ask;
    bool m_askedContinue = false;
    bool m_askedCopy = false;
    bool m_questionOpen = false;
    std::optional<SupportAnswer> m_cancelledBy; // No or NoAnswer
};

} // namespace hwio::remote
