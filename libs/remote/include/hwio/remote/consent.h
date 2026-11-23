#pragma once

// ConsentProvider - how the host's user answers the questions of a remote support session
// (plan/features/0044, ADR-0335 join approval, ADR-0324 operation confirmation): the GUI dialog
// (RemoteShareViewModel), the CLI prompt on stdin, or AutoDenyConsent (no tty, --deny-operations).
//
// RemoteHost owns the time limits (2 min for a join, 60 s for an operation) and withdraws a
// question it no longer needs (timeout, session end, a newer join): the provider closes its dialog
// and never answers a withdrawn question. Every call and every answer happens on the owner thread;
// an answer may come synchronously from inside the ask call.
//
// The question texts are built here (translated, context "hwio::remote") so that the GUI and the
// CLI ask the same thing.

#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"

#include <QString>

#include <functional>
#include <string>
#include <string_view>

namespace hwio::remote {

struct JoinQuestion {
    SessionCode code = SessionCode::generate();
};

struct OperationQuestion {
    tunnel::OpKind kind = tunnel::OpKind::Reset;
    tunnel::OpTarget target = tunnel::OpTarget::App;
    std::string firmwareId;      // flash only
    std::string firmwareName;    // flash: from the catalog page ("" when unknown); lteUpdate: the
                                 // recommended LTE firmware ("HARDWARIO LTE firmware")
    std::string firmwareVersion; // flash, lteUpdate
};

class ConsentProvider {
public:
    using Answer = std::function<void(bool allowed)>;
    using Withdraw = std::function<void()>;

    ConsentProvider() = default;
    ConsentProvider(const ConsentProvider&) = delete;
    ConsentProvider& operator=(const ConsentProvider&) = delete;
    ConsentProvider(ConsentProvider&&) = delete;
    ConsentProvider& operator=(ConsentProvider&&) = delete;
    virtual ~ConsentProvider() = default;

    // Shows the join approval ([Allow] [End session], End session is the default). The returned
    // function withdraws it (empty = nothing to withdraw).
    [[nodiscard]] virtual Withdraw askJoin(const JoinQuestion& question, Answer answer) = 0;
    // Shows the operation confirmation ([Allow] [Deny], Deny is the default).
    [[nodiscard]] virtual Withdraw askOperation(const OperationQuestion& question,
                                                Answer answer) = 0;
};

// Denies every question at once (CLI without a tty, --deny-operations; ADR-0335: a host without
// a tty never approves a join).
class AutoDenyConsent final : public ConsentProvider {
public:
    [[nodiscard]] Withdraw askJoin(const JoinQuestion& question, Answer answer) override;
    [[nodiscard]] Withdraw askOperation(const OperationQuestion& question, Answer answer) override;
};

// "Someone joined session 482 913. Allow them to send commands to the device? Allow only if
// HARDWARIO Support has just told you they are joining."
[[nodiscard]] QString joinQuestionText(const JoinQuestion& question);
// "Support asks to flash firmware CHESTER-Clime 3.2.1 (ID abcd…) to the application core. The
// device restarts." and the other kinds.
[[nodiscard]] QString operationQuestionText(const OperationQuestion& question);
// "Flash firmware CHESTER-Clime 3.2.1", "Erase the application core", ... (audit lines, titles)
[[nodiscard]] QString operationSummary(const OperationQuestion& question);
// The joiner's text of an op-progress phase: the steps of the guided LTE update (ADR-0363) and
// "download" translated ("waiting for the J-Link on the LTE connector"), other phases as sent.
[[nodiscard]] QString operationPhaseText(std::string_view phase);
// ADR-0368: support's question of a remote LTE update (the joiner's text; also the host's audit
// line): "The application on the customer's CHESTER will be erased. … Continue?", "The
// application could not be copied. Continue without a copy?"
[[nodiscard]] QString operationQuestionKindText(tunnel::OpQuestionKind kind);
// ADR-0369: support's result of a remote operation, named, with the local time `time` ("14:02"):
// "Application firmware flashed on the customer's device (14:02).", "Customer's device reset
// (14:02).", "LTE firmware updated, application written back (14:02).", ... A failure keeps the
// error message: "Resetting the customer's device failed (14:02): <message>".
[[nodiscard]] QString operationResultText(tunnel::OpKind kind, tunnel::OpTarget target,
                                          const QString& time);
// ADR-0368 (confirmed 2026-11-12): support answered No to a question of the remote LTE update:
// support's result "LTE firmware update cancelled by you (14:02). Nothing was changed.", the
// customer's side "Cancelled by support. Nothing was changed."
[[nodiscard]] QString lteUpdateDeclinedText(const QString& time);
[[nodiscard]] QString cancelledBySupportText();
// No answer from support in time, or the session ended: "Support did not answer. Nothing was
// changed." (the customer's side and the audit).
[[nodiscard]] QString supportDidNotAnswerText();
[[nodiscard]] QString operationFailedText(tunnel::OpKind kind, tunnel::OpTarget target,
                                          const QString& time, const QString& message);

} // namespace hwio::remote
