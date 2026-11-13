#pragma once

// RemoteOperationClient - the joiner's side of a remote operation (plan/features/0044, ADR-0336):
// sends op-request through the RemoteConsoleSession and follows the host's answers for its own
// pending operation id only (op-accepted → op-progress … → op-result, or op-denied). One operation
// at a time. Owner thread only.
//
// Timeouts: responseTimeout until op-accepted / op-denied (the host's 60 s confirmation plus a
// catalog lookup), then inactivityTimeout between progress messages and the result. Results are
// the customer's Monitor's report: a failed op-result carries the host's error code, translated
// here (unknown codes: remote.protocol_error).
//
// ADR-0368: with setAnswersQuestions(true) an lteUpdate request to a host whose hello says
// opQuestions carries `questions`; the host's op-question for the pending operation emits
// question(), and answer() sends op-answer once (the question closes; the host decides the rest,
// e.g. no answer within its 60 s is no).

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/remote/tunnel.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <chrono>
#include <optional>
#include <string>

namespace hwio::remote {

class RemoteConsoleSession;

struct RemoteOperationLimits {
    std::chrono::milliseconds responseTimeout{90000};
    std::chrono::milliseconds inactivityTimeout{300000};
};

class RemoteOperationClient : public QObject {
    Q_OBJECT

public:
    // `session` may close first (the pending operation then ends with remote.host_ended).
    explicit RemoteOperationClient(RemoteConsoleSession& session, RemoteOperationLimits limits = {},
                                   QObject* parent = nullptr);

    // Sends the request; the id is new and random. Errors: internal.busy (one is pending),
    // remote.operation_unsupported_peer (web Terminal host, or ops: false, or lteUpdate not in the
    // hello's opKinds, ADR-0363), usage.invalid_argument
    // (flash without a firmware ID, a firmware ID for another kind), remote.host_ended.
    [[nodiscard]] Result<std::string> request(tunnel::OpKind kind, tunnel::OpTarget target,
                                              std::optional<std::string> firmwareId = std::nullopt);
    [[nodiscard]] bool busy() const noexcept { return m_pending.has_value(); }
    // ADR-0368: this joiner answers the questions of a remote LTE update (a person is there).
    void setAnswersQuestions(bool answers) noexcept { m_answers = answers; }
    // The open question of the pending operation (nullopt when none).
    [[nodiscard]] std::optional<tunnel::OpQuestionKind> openQuestion() const noexcept {
        return m_question ? std::optional(m_question->question) : std::nullopt;
    }
    // Answers the open question. Errors: usage.invalid_argument (none open), those of sending.
    [[nodiscard]] Status answer(bool yes);
    // ADR-0368: this joiner answered No to a question of the current (or just finished) request;
    // its cancelled result is then "cancelled by you". Reset by request().
    [[nodiscard]] bool declined() const noexcept { return m_declined; }
    [[nodiscard]] bool isAccepted() const noexcept { return m_accepted; }
    // Forgets the pending operation (no more signals for it).
    void abandon();

signals:
    // The customer allowed it; it runs on their side.
    void accepted();
    void progress(const hwio::remote::tunnel::OpProgress& progress);
    // Once per request: the host's result, a denial (remote.operation_denied, internal.busy,
    // remote.firmware_not_in_catalog, ...), timeout, or remote.host_ended.
    void finished(bool ok, const hwio::Error& error);
    // ADR-0368: the host asks support (openQuestion()); questionClosed() after answer() or the end.
    void question(hwio::remote::tunnel::OpQuestionKind kind);
    void questionClosed();

private:
    void onMessage(const tunnel::HostMessage& message);
    void onQuestion(const tunnel::OpQuestion& question);
    void finish(const std::optional<Error>& error);

    QPointer<RemoteConsoleSession> m_session;
    RemoteOperationLimits m_limits;
    std::optional<std::string> m_pending;
    bool m_accepted = false;
    bool m_answers = false;
    bool m_declined = false;
    std::optional<tunnel::OpQuestion> m_question;
    QTimer m_timer{this};
};

} // namespace hwio::remote
