#pragma once

// RemoteHost - shares one open console (J-Link RTT or BLE) through the HARDWARIO Terminal relay
// (plan/features/0044, ADR-0334..0336). Owner thread only; the relay socket runs on the
// RelayClient's worker thread, the device on the session's own worker (both reached through
// queued signals only).
//
// Console mapping (web Terminal compatible): every device line (shell and log) → at {event:
// "read"}, every line sent to the device (local or remote, its echo) → at {event: "write"},
// connection events → at {event: "state", data: "connected" | "connection" | "disconnected"}.
// Commands with secret arguments and secret output values are sent redacted (ADR-0186 rules, as the
// console file). The lines also go to a bounded Transcript (the sync snapshot).
//
// Code and collision (ADR-0334): a fresh CSPRNG code; any `at` or `sync` received within
// collisionWindow after connecting = another host uses the code: a new code, at most maxCodeTries
// codes in all (then remote.code_collision). sharingStarted() is emitted only after the window.
// An `at` / `sync` later ends the session (remote.code_collision).
//
// Join approval (ADR-0335): the first newClient after each own connect is the host's own and is
// ignored; every other newClient opens the join question (ConsentProvider). While it is open, no
// remote `write` reaches the device (dropped and counted, never queued; an audit line tells how
// many), and no sync is sent. Allow → sync (the transcript snapshot) + the host's hello, commands
// pass. End session / no answer within joinApprovalTimeout → the session ends
// (remote.join_not_allowed). A newClient while the question is open asks again (fresh timeout).
// Implementation note (plan 0044): after the host's own reconnect the approval is asked again too,
// because a participant that joined while the host was away was never announced to it.
//
// Remote commands: an allowed `write` string of at most maxRemoteLineBytes without control
// characters, at most remoteLinesPerSecond (excess dropped with one audit line per second) →
// audit marker "command …" + ConsoleSession::sendRemoteLine(). A `write` object is a
// tunnel message (ADR-0336): hello (the joiner's app) and op-request.
//
// Operations (ADR-0322..0324, ADR-0336): only with HostOperations (J-Link) and after the hello was
// sent (it lists HostOperations::kinds() as opKinds; a kind not in it is refused with
// remote.operation_unsupported_peer; lteUpdate, ADR-0363, asks with the recommended LTE firmware's
// name and runs the guided update); one at a time, a request during an operation, an operation
// question or a join question is denied with internal.busy. The request is validated, a flash's
// firmware looked up in the catalog (remote.firmware_not_in_catalog), then the operation question
// (Deny after operationTimeout, remote.operation_denied). Allow → op-accepted, op-progress (at most
// progressInterval apart), op-result (a new phase at once, ADR-0363).
//
// Support's questions (ADR-0368): the hello says opQuestions when the host runs lteUpdate. An
// lteUpdate request with `questions` gets an AskSupport for its run: a question is sent as
// op-question (a random qid; one open at a time) and answered by an op-answer of an allowed
// participant (no join question open) for that operation and qid, once; any other answer is
// dropped. No answer within operationTimeout, or the session ending, answers NoAnswer (the update
// is then cancelled before anything changed). Audited: asked, answered, not answered.
//
// Autocomplete (ADR-0364): setCommandTree() gives the host's learned command tree (the GUI tab's
// ConsoleViewModel, the CLI host's learner, on every change); after a join is allowed, and on
// every change while allowed (coalesced), it is sent as tunnel `commands` with its help texts (a
// joiner accepts events up to joinerIncomingEventBytes, ADR-0365; should it not fit, it goes
// without help texts, logged, as a safety net). A tunnel `learn`
// of an allowed participant for a known path, at most learnPerSecond and learnPerSession, emits
// learnRequested() (the owner runs its silent `<path> -h` query and sets the grown tree);
// others are dropped. Not audited one by one; their count is in the audit line at the end.
//
// Limits (ADR-0328, plan 0044 § Limits): idle (no console traffic in either direction) warns at
// idleTimeout - idleWarningBefore (idleWarning()), keepSharing() restarts the idle time; idle or
// maxLifetime end the session. ConsoleSession closed → the session ends.
//
// Audit (ADR-0333): ConsoleSession::recordRemoteEvent() for the start and end, every join, allow,
// the peer's app, remote commands (redacted), dropped commands, and every operation request with
// its outcome. The texts are translated (the host's language), context "hwio::remote".

#include "hwio/core/error.h"
#include "hwio/device/command_tree.h"
#include "hwio/device/console_line.h"
#include "hwio/device/session_events.h"
#include "hwio/remote/consent.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/relay_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/transcript.h"
#include "hwio/remote/tunnel.h"

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace hwio::device {
class ConsoleSession;
} // namespace hwio::device

namespace hwio::remote {

struct HostLimits {
    std::chrono::milliseconds joinApprovalTimeout{120000};
    std::chrono::milliseconds operationTimeout{60000};
    std::chrono::milliseconds idleTimeout{30LL * 60 * 1000};
    std::chrono::milliseconds idleWarningBefore{2LL * 60 * 1000};
    std::chrono::milliseconds maxLifetime{8LL * 60 * 60 * 1000};
    std::chrono::milliseconds collisionWindow{2000};
    int maxCodeTries = 3;
    std::size_t maxRemoteLineBytes = 512;
    int remoteLinesPerSecond = 20;
    std::chrono::milliseconds progressInterval{250}; // op-progress at most 4/s
    std::chrono::milliseconds helloWait{5000};       // then the peer counts as web Terminal
    // ADR-0364: tunnel `learn` requests of the participants.
    int learnPerSecond = 2;
    int learnPerSession = 200;
    std::chrono::milliseconds commandsDelay{250}; // tree changes are sent at most this often
    TranscriptLimits transcript;
    RelayLimits relay;
};

struct HostOptions {
    QUrl relay;
    HostLimits limits;
    std::string appVersion; // the host's hello
    // A fixed first code (tests); nullopt = SessionCode::generate().
    std::optional<SessionCode> code;
};

enum class HostState : std::uint8_t {
    Connecting, // to the relay (also while the collision window runs)
    Sharing,    // the code is confirmed
    Ended,
};

class RemoteHost : public QObject {
    Q_OBJECT

public:
    // `session`, `consent` and `operations` (nullptr = no operations, hello ops: false) must
    // outlive the host, or the session may close first (the host then ends).
    RemoteHost(device::ConsoleSession& session, ConsentProvider& consent,
               HostOperations* operations, HostOptions options, QObject* parent = nullptr);
    ~RemoteHost() override; // end() without signals
    RemoteHost(const RemoteHost&) = delete;
    RemoteHost& operator=(const RemoteHost&) = delete;
    RemoteHost(RemoteHost&&) = delete;
    RemoteHost& operator=(RemoteHost&&) = delete;

    void start();
    // End sharing (the user). Idempotent.
    void end();
    // Keep sharing after the idle warning (any CLI input).
    void keepSharing();
    // ADR-0364: the host's command tree (nullptr = none known).
    void setCommandTree(std::shared_ptr<const device::CommandTree> tree);

    [[nodiscard]] HostState state() const noexcept { return m_state; }
    [[nodiscard]] const SessionCode& code() const noexcept { return m_code; }
    [[nodiscard]] QUrl link() const;
    [[nodiscard]] const QUrl& relay() const noexcept { return m_options.relay; }
    // At least one participant is allowed and no join question is open.
    [[nodiscard]] bool commandsAllowed() const noexcept { return m_allowed && !m_joinPending; }
    [[nodiscard]] bool joinQuestionOpen() const noexcept { return m_joinPending; }
    [[nodiscard]] bool operationRunning() const noexcept { return m_operation.has_value(); }
    // The peer's hello app ("HARDWARIO Monitor 2.1.0"), "" when none was received.
    [[nodiscard]] const std::string& peerApp() const noexcept { return m_peerApp; }
    [[nodiscard]] const std::optional<Error>& endError() const noexcept { return m_endError; }

signals:
    // The code is confirmed (after the collision window); code(), link().
    void sharingStarted();
    // The relay connection is lost / back (the code stays).
    void connectionLost();
    void connectionRestored();
    // Join question opened / answered (allowed) / remote participants changed.
    void participantsChanged();
    // The idle time ends in `remaining`.
    void idleWarning(std::chrono::seconds remaining);
    // A remote operation started (allowed) / progressed / finished.
    void operationStarted(const QString& summary);
    void operationProgress(const hwio::remote::OperationUpdate& update);
    void operationFinished(bool ok, const QString& message);
    // The session ended: by end() (no error) or for `error`'s reason. Once.
    void ended();
    void warning(const hwio::Error& error);
    // ADR-0364: a participant's autocomplete needs the subcommands of `path` (command words).
    void learnRequested(const QStringList& path);

private:
    struct PendingOperation {
        tunnel::OpRequest request;
        OperationQuestion question;
        bool accepted = false;
        ConsentProvider::Withdraw withdraw;
    };

    void connectRelay();
    void onRelayConnected(bool reconnected);
    void onNewClient();
    void onAtOrSync();
    void onWrite(const QJsonValue& payload);
    void onRemoteLine(const QString& text);
    void onTunnel(const QJsonValue& payload);
    void onOperationRequest(const tunnel::OpRequest& request);
    void onLearn(const tunnel::Learn& learn);
    void sendCommands();
    void askOperation(PendingOperation pending);
    void denyOperation(const std::string& id, ErrorCode code, const QString& audit);
    void startOperation();
    void finishOperation(const Status& status);
    // ADR-0368
    void askSupport(std::uint64_t token, tunnel::OpQuestionKind kind, SupportAnswerFn answer);
    void onAnswer(const tunnel::OpAnswer& answer);
    void answerSupportQuestion(bool yes);
    // Closes the open question (stops its timer); its answer callback, empty when none.
    [[nodiscard]] SupportAnswerFn takeQuestion();
    void askJoin();
    void answerJoin(bool allowed);
    void sendSync();
    void sendHello();
    void sendAt(const QString& event, const QJsonValue& data, bool droppable);
    void sendHost(const tunnel::HostMessage& message);
    void onLines(const QList<device::ConsoleLine>& lines);
    void onConnection(const device::ConnectionEvent& event);
    void activity();
    void audit(const QString& text);
    void finish(std::optional<Error> error, bool notify = true);
    [[nodiscard]] QString redactedCommand(const std::string& line) const;
    [[nodiscard]] QString redactedOutput(const std::string& line) const;

    QPointer<device::ConsoleSession> m_session;
    ConsentProvider* m_consent;   // non-owning
    HostOperations* m_operations; // non-owning, may be nullptr
    HostOptions m_options;
    SessionCode m_code;
    std::unique_ptr<RelayClient> m_relay;
    Transcript m_transcript;
    HostState m_state = HostState::Connecting;
    int m_codeTries = 0;
    bool m_inCollisionWindow = false;
    bool m_ownNewClientPending = false;
    bool m_everShared = false;
    bool m_allowed = false;
    bool m_joinPending = false;
    bool m_helloSent = false;
    ConsentProvider::Withdraw m_joinWithdraw;
    std::uint64_t m_joinToken = 0; // answers of withdrawn questions are ignored
    std::uint64_t m_droppedWhilePending = 0;
    std::string m_peerApp;
    std::optional<PendingOperation> m_operation;
    // ADR-0368: the open question to support (of m_operation).
    struct SupportQuestion {
        std::string qid;
        tunnel::OpQuestionKind kind = tunnel::OpQuestionKind::Continue;
        SupportAnswerFn answer;
    };
    std::optional<SupportQuestion> m_question;
    QTimer m_questionTimer{this};
    std::uint64_t m_operationToken = 0;
    std::chrono::steady_clock::time_point m_lastProgress;
    std::string m_lastProgressPhase;
    std::deque<std::chrono::steady_clock::time_point> m_recentLines; // <= remoteLinesPerSecond
    std::chrono::steady_clock::time_point m_lastRateAudit;
    std::optional<Error> m_endError;
    QString m_deviceState = QStringLiteral("disconnected");
    QTimer m_collisionTimer{this};
    QTimer m_joinTimer{this};
    QTimer m_operationTimer{this};
    QTimer m_idleWarningTimer{this};
    QTimer m_idleTimer{this};
    QTimer m_lifetimeTimer{this};
    QTimer m_helloTimer{this};
    // ADR-0364
    std::shared_ptr<const device::CommandTree> m_commandTree;
    std::shared_ptr<const device::CommandTree> m_sentTree;
    std::deque<std::chrono::steady_clock::time_point> m_recentLearn; // <= learnPerSecond
    int m_learnCount = 0;
    QTimer m_commandsTimer{this};
};

} // namespace hwio::remote
