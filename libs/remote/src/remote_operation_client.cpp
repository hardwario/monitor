#include "hwio/remote/remote_operation_client.h"

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/tunnel.h"

#include <QCoreApplication>
#include <QObject>
#include <QRandomGenerator>
#include <QTimer>
#include <QtCore/qtmetamacros.h>

#include <expected>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace hwio::remote {

namespace {

// The host's error code as an Error of this process (translated here).
Error fromHostCode(const std::string& code, bool reportedResult) {
    const std::optional<ErrorCode> known = errorCodeFromId(code);
    Error error = makeError(known.value_or(ErrorCode::RemoteProtocolError), "host: " + code);
    if (reportedResult) {
        error.message =
            QCoreApplication::translate("hwio::remote", "Reported by the customer's Monitor: %1")
                .arg(QString::fromStdString(error.message))
                .toStdString();
    }
    return error;
}

} // namespace

RemoteOperationClient::RemoteOperationClient(RemoteConsoleSession& session,
                                             RemoteOperationLimits limits, QObject* parent)
    : QObject(parent), m_session(&session), m_limits(limits) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        Error error = makeError(ErrorCode::Timeout, "no answer from the customer's Monitor");
        error.message = QCoreApplication::translate(
                            "hwio::remote", "The customer's Monitor did not answer in time.")
                            .toStdString();
        finish(std::move(error));
    });
    connect(&session, &RemoteConsoleSession::hostMessage, this, &RemoteOperationClient::onMessage);
    connect(&session, &RemoteConsoleSession::joinStateChanged, this, [this] {
        if (m_session && m_session->joinState() == JoinState::Ended) {
            finish(makeError(ErrorCode::RemoteHostEnded));
        }
    });
    connect(&session, &QObject::destroyed, this,
            [this] { finish(makeError(ErrorCode::RemoteHostEnded)); });
}

Result<std::string> RemoteOperationClient::request(tunnel::OpKind kind, tunnel::OpTarget target,
                                                   std::optional<std::string> firmwareId) {
    if (m_pending) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "a remote operation is pending"));
    }
    if (!m_session) {
        return std::unexpected(makeError(ErrorCode::RemoteHostEnded));
    }
    if ((kind == tunnel::OpKind::Flash) != firmwareId.has_value()) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         "a firmware ID goes with flash, and only with flash"));
    }
    std::string id = std::format("op-{:08x}{:08x}", QRandomGenerator::system()->generate(),
                                 QRandomGenerator::system()->generate());
    const tunnel::OpRequest request{
        .id = id, .kind = kind, .target = target, .firmwareId = std::move(firmwareId)};
    if (Status sent = m_session->sendTunnel(request); !sent) {
        return std::unexpected(std::move(sent).error());
    }
    m_pending = id;
    m_accepted = false;
    m_timer.start(m_limits.responseTimeout);
    return id;
}

void RemoteOperationClient::onMessage(const tunnel::HostMessage& message) {
    if (!m_pending) {
        return;
    }
    // ADR-0336: only the answers for our own pending id.
    std::visit(
        [this](const auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, tunnel::HostHello> ||
                          std::is_same_v<T, tunnel::HostEnd>) {
                return; // the session handles these; HostEnd ends it (joinStateChanged)
            } else {
                if (m.id != *m_pending) {
                    return;
                }
                if constexpr (std::is_same_v<T, tunnel::OpAccepted>) {
                    m_accepted = true;
                    m_timer.start(m_limits.inactivityTimeout);
                    emit accepted();
                } else if constexpr (std::is_same_v<T, tunnel::OpDenied>) {
                    finish(fromHostCode(m.code, false));
                } else if constexpr (std::is_same_v<T, tunnel::OpProgress>) {
                    if (m_accepted) {
                        m_timer.start(m_limits.inactivityTimeout);
                        emit progress(m);
                    }
                } else if constexpr (std::is_same_v<T, tunnel::OpResult>) {
                    if (m.ok) {
                        finish(std::nullopt);
                    } else {
                        finish(fromHostCode(m.code.value_or(""), true));
                    }
                }
            }
        },
        message);
}

void RemoteOperationClient::finish(const std::optional<Error>& error) {
    if (!m_pending) {
        return;
    }
    m_pending.reset();
    m_accepted = false;
    m_timer.stop();
    emit finished(!error.has_value(), error.value_or(Error{}));
}

void RemoteOperationClient::abandon() {
    m_pending.reset();
    m_accepted = false;
    m_timer.stop();
}

} // namespace hwio::remote
