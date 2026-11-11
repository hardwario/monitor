#pragma once

// RelayClient - one participant's connection to the HARDWARIO Terminal relay (plan/features/0044,
// ADR-0321, ADR-0334): Engine.IO v4 over a QWebSocket (wss://<relay>/socket.io/?EIO=4&transport=
// websocket), the Socket.IO v5 namespace /<code> (no auth payload), events as JSON. The relay emits
// newClient (null) to every socket of the namespace when one connects, and passes at, sync and
// write to every other socket; RelayClient reports those four as typed signals and ignores any
// other event (logged).
//
// Threading (plan 0044 § Threading): the socket, the Engine.IO / Socket.IO handling and the JSON
// parsing run on a worker std::jthread owned by RelayClient (stopped and joined by stop() and the
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
// The system proxy (QNetworkProxyFactory) of the relay URL is used. Worker -> owner notifications
// (events, connection changes, warnings) wait in a queue of at most incomingEventQueue entries
// (ADR-0357): when it is full a new warning is dropped (logged) and any other notification ends
// the connection with failed() (remote.protocol_error).

#include "hwio/core/error.h"
#include "hwio/device/session_events.h" // Q_DECLARE_METATYPE(hwio::Error)
#include "hwio/remote/session_code.h"

#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtTypes>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>

namespace hwio::remote {

struct RelayLimits {
    std::chrono::milliseconds connectTimeout{15000};
    std::chrono::milliseconds reconnectFirstDelay{1000};
    std::chrono::milliseconds reconnectMaxDelay{30000};
    std::chrono::milliseconds reconnectBudget{300000};
    std::size_t outgoingQueueBytes = 1024ULL * 1024;
    int maxOversizedEvents = 3;
    int maxProtocolErrors = 10;
    // Notifications of the worker not yet handled on the owner thread (ADR-0357). Values < 1
    // count as 1.
    std::size_t incomingEventQueue = 4096;
    // emitEvent() calls not yet taken over by the worker (ADR-0357). When full, the oldest
    // droppable one is dropped (the oldest of any kind when none is droppable), reported by a
    // warning. Values < 1 count as 1.
    std::size_t outgoingSendQueue = 4096;
    // stop(): how long the relay may take to confirm it has read the last packets (the tunnel
    // `end`) and the socket to close, before it is aborted (stop() blocks at most this long).
    std::chrono::milliseconds closeTimeout{2000};
};

struct RelayConfig {
    QUrl relay; // validated by parseRelayUrl()
    SessionCode code = SessionCode::generate();
    RelayLimits limits;
};

namespace detail {
class RelayWorker;
struct RelayEventQueue; // relay_worker.h
struct RelaySendQueue;  // relay_worker.h
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
    // Owner thread: invoked (queued, by name) by wakeUp(); emits the queued notifications.
    Q_INVOKABLE void drainEvents();
    // Worker thread: one coalesced, payload-free queued drainEvents() (ADR-0357).
    void wakeUp();

    RelayConfig m_config;
    // Owned here, shared with the worker by reference (it is deleted after the worker).
    std::unique_ptr<detail::RelayEventQueue> m_events;
    std::unique_ptr<detail::RelaySendQueue> m_sends; // the same, other direction
    std::atomic<bool> m_wakePending{false};
    // Non-owning: the worker lives on m_thread's stack, valid from start()'s
    // return until stop()'s join(); the owner only posts queued calls to it.
    detail::RelayWorker* m_worker = nullptr;
    std::jthread m_thread; // owned; joined by stop() (also from the destructor)
    bool m_started = false;
    bool m_stopped = false;
    bool m_connected = false;
};

} // namespace hwio::remote
