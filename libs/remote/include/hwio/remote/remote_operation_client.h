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
    // remote.operation_unsupported_peer (web Terminal host, or ops: false), usage.invalid_argument
    // (flash without a firmware ID, a firmware ID for another kind), remote.host_ended.
    [[nodiscard]] Result<std::string> request(tunnel::OpKind kind, tunnel::OpTarget target,
                                              std::optional<std::string> firmwareId = std::nullopt);
    [[nodiscard]] bool busy() const noexcept { return m_pending.has_value(); }
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

private:
    void onMessage(const tunnel::HostMessage& message);
    void finish(const std::optional<Error>& error);

    QPointer<RemoteConsoleSession> m_session;
    RemoteOperationLimits m_limits;
    std::optional<std::string> m_pending;
    bool m_accepted = false;
    QTimer m_timer{this};
};

} // namespace hwio::remote
