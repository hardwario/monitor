#pragma once

// RelayClient - one participant's connection to the HARDWARIO Terminal relay (plan/features/0044,
// ADR-0321, ADR-0334): Engine.IO v4 over a QWebSocket (wss://<relay>/socket.io/?EIO=4&transport=
// websocket), the Socket.IO v5 namespace /<code> (no auth payload), events as JSON. The relay emits
// newClient (null) to every socket of the namespace when one connects, and passes at, sync and
// write to every other socket; RelayClient reports those four as typed signals and ignores any
// other event (logged).
//
// Threading (plan 0044 § Threading): the socket, the Engine.IO / Socket.IO handling and the JSON
// parsing run on a worker QThread owned by RelayClient (stopped and joined by stop() and the
// destructor). RelayClient itself is a QObject facade on the owner thread; every member function
// is called there and every signal is emitted there (queued, in the worker's order).
//
// Limits (plan 0044 § Limits): incoming event 64 KiB (bigger: dropped, warning; the
// maxOversizedEvents-th one ends the connection with remote.protocol_error; so do
// maxProtocolErrors malformed packets); the outgoing queue holds at most outgoingQueueBytes
// (UTF-8 bytes of the encoded packets, counting what the socket has not written yet): when it is
// full the oldest droppable packets (device output, at/read) are dropped and one at/read notice
// "… N lines dropped …" is queued in their place, a packet that is not droppable is never dropped
// (the queue may exceed the bound by the non-droppable packets only). Reconnect after a lost
// connection: 1 s, doubling up to 30 s, for at most 5 min after the loss, then failed()
// (remote.relay_unreachable). The first connect is not retried: it fails within connectTimeout.
// The system proxy (QNetworkProxyFactory) of the relay URL is used.

#include "hwio/core/error.h"
#include "hwio/device/session_events.h" // Q_DECLARE_METATYPE(hwio::Error)
#include "hwio/remote/session_code.h"

#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <memory>

class QThread;

namespace hwio::remote {

struct RelayLimits {
    std::chrono::milliseconds connectTimeout{15000};
    std::chrono::milliseconds reconnectFirstDelay{1000};
    std::chrono::milliseconds reconnectMaxDelay{30000};
    std::chrono::milliseconds reconnectBudget{300000};
    std::size_t outgoingQueueBytes = 1024ULL * 1024;
    int maxOversizedEvents = 3;
    int maxProtocolErrors = 10;
};

struct RelayConfig {
    QUrl relay; // validated by parseRelayUrl()
    SessionCode code = SessionCode::generate();
    RelayLimits limits;
};

namespace detail {
class RelayWorker;
} // namespace detail

class RelayClient : public QObject {
    Q_OBJECT

public:
    explicit RelayClient(RelayConfig config, QObject* parent = nullptr);
    ~RelayClient() override; // stop()
    RelayClient(const RelayClient&) = delete;
    RelayClient& operator=(const RelayClient&) = delete;
    RelayClient(RelayClient&&) = delete;
    RelayClient& operator=(RelayClient&&) = delete;

    // Starts the worker thread and connects. Once only.
    void start();
    // Queues an event (sent in order once the namespace is connected). `droppable`: device output
    // that may be dropped when the outgoing queue is full.
    void emitEvent(const QString& name, const QJsonValue& payload, bool droppable = false);
    // Sends a Socket.IO DISCONNECT, closes the socket and joins the worker thread. Idempotent; no
    // signal is emitted after it returns.
    void stop();

    [[nodiscard]] const RelayConfig& config() const noexcept { return m_config; }
    [[nodiscard]] bool isConnected() const noexcept { return m_connected; }

signals:
    // The namespace is connected; `reconnected` = after a lost connection (the relay sends
    // newClient for it again).
    void connected(bool reconnected);
    // The connection was lost; the client reconnects (connected(true) or failed()).
    void connectionLost(const hwio::Error& error);
    // Final: the first connect failed, the reconnect budget ran out, or the relay refused the
    // namespace or sent too much garbage. The client is stopped.
    void failed(const hwio::Error& error);
    void newClient();
    void atReceived(const QJsonValue& payload);
    void syncReceived(const QJsonValue& payload);
    void writeReceived(const QJsonValue& payload);
    // A dropped incoming packet (remote.protocol_error) or dropped outgoing lines.
    void warning(const hwio::Error& error);

private:
    void onEvent(const QString& name, const QJsonValue& payload);

    RelayConfig m_config;
    std::unique_ptr<QThread> m_thread;             // owned; joined by stop()
    std::unique_ptr<detail::RelayWorker> m_worker; // lives on m_thread; deleted after the join
    bool m_started = false;
    bool m_stopped = false;
    bool m_connected = false;
};

} // namespace hwio::remote
