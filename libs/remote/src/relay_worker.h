#pragma once

// The worker-thread half of RelayClient (relay_client.h): the QWebSocket, Engine.IO (open, ping /
// pong, timeouts), the Socket.IO namespace, reconnect and the bounded outgoing queue. Lives on the
// RelayClient's std::jthread (a Qt-adopted thread with its own QEventLoop); every slot runs there.

#include "hwio/core/bounded_queue.h"
#include "hwio/core/error.h"
#include "hwio/remote/relay_client.h"

#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtTypes>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

class QWebSocket;

namespace hwio::remote::detail {

// Worker -> RelayClient notifications (ADR-0357). They travel through a BoundedQueue owned by
// RelayClient (std::mutex, visible to ThreadSanitizer) followed by a payload-free queued wake-up,
// so no QString / QJsonValue / Error crosses threads inside Qt's queued-call machinery.
struct RelayConnected {
    bool reconnected = false;
};
struct RelayConnectionLost {
    Error error;
};
struct RelayFailed {
    Error error;
};
struct RelayWarning {
    Error error;
};
struct RelayIncomingEvent {
    QString name;
    QJsonValue payload;
};
using RelayEvent = std::variant<RelayConnected, RelayConnectionLost, RelayFailed, RelayWarning,
                                RelayIncomingEvent>;
struct RelayEventQueue : BoundedQueue<RelayEvent> {
    using BoundedQueue::BoundedQueue;
};

// RelayClient -> worker: events to send (ADR-0357, same hand-off in the other direction). The
// worker moves them into its byte-bounded outgoing queue (enqueue()) on every drain.
struct RelaySend {
    QString name;
    QJsonValue payload;
    bool droppable = false;
};
struct RelaySendQueue : BoundedQueue<RelaySend> {
    using BoundedQueue::BoundedQueue;
};

class RelayWorker : public QObject {
    Q_OBJECT

public:
    // `events` is owned by the RelayClient and outlives the worker (the owner deletes the worker
    // after joining its thread); `wake` is called on the worker thread after every push.
    RelayWorker(RelayConfig config, RelayEventQueue& events, std::function<void()> wake,
                RelaySendQueue& sends);
    ~RelayWorker() override;
    RelayWorker(const RelayWorker&) = delete;
    RelayWorker& operator=(const RelayWorker&) = delete;
    RelayWorker(RelayWorker&&) = delete;
    RelayWorker& operator=(RelayWorker&&) = delete;

    Q_INVOKABLE void start();
    // Worker thread: invoked (queued, by name, coalesced) by RelayClient::emitEvent(); sends what
    // waits in the send queue.
    Q_INVOKABLE void drainSends();
    // Owner thread: one coalesced, payload-free queued drainSends().
    void wakeForSends();
    // The remaining packets, then (when connected) a CONNECT the relay refuses; its answer
    // (closeSocket()) means the relay has dispatched the packets before, the tunnel `end`
    // (ADR-0354) included, and only then DISCONNECT and the WebSocket close follow. No more
    // notifications. Queued by the owner's stop(). shutdownFinished() follows when the socket has
    // closed, at the latest after closeTimeout (RelayLimits; then the socket is aborted).
    Q_INVOKABLE void shutdown();

signals:
    // The worker is done; the thread body quits its event loop.
    void shutdownFinished();

private:
    enum class Phase : std::uint8_t {
        Idle,
        Opening,      // WebSocket + Engine.IO open + namespace connect
        Connected,    // namespace connected
        WaitingRetry, // lost; the reconnect timer runs
        Ended,        // failed or shut down
    };
    struct Outgoing {
        QString text; // the Engine.IO message packet
        std::size_t bytes = 0;
        bool droppable = false;
    };

    // Pushes to the owner's event queue and wakes it. Overflow (ADR-0357): warnings are dropped
    // when the queue is full; any other event ends the connection with remote.protocol_error
    // (the queue keeps one slot in reserve for that final RelayFailed).
    void post(RelayEvent event);
    void send(const QString& name, const QJsonValue& payload, bool droppable);
    void openSocket();
    void destroySocket();
    void onTextMessage(const QString& message);
    void onBinaryMessage();
    void onSocketClosed();
    void onBytesWritten(qint64 bytes);
    void handleEngineIo(const QString& message);
    void handleSocketIo(const QString& data);
    void lost(Error error);
    void fail(const Error& error);
    void protocolProblem(const Error& error, bool oversized);
    void enqueue(Outgoing packet);
    void flush();
    void rawSend(const QString& text);
    void armPingTimer();
    void closeSocket();
    void finishShutdown();

    RelayConfig m_config;
    RelayEventQueue& m_events; // owned by RelayClient
    std::function<void()> m_wake;
    RelaySendQueue& m_sends; // owned by RelayClient
    std::atomic<bool> m_sendWakePending{false};
    std::uint64_t m_reportedSendDrops = 0;
    QString m_nsp;
    Phase m_phase = Phase::Idle;
    bool m_everConnected = false;
    bool m_engineOpen = false;
    bool m_closing = false; // shutdown(): waiting for the answer to the flush CONNECT
    std::unique_ptr<QWebSocket> m_socket;
    // Children of the worker, so that moveToThread() moves them along.
    QTimer m_connectTimer{this}; // the open phase
    QTimer m_pingTimer{this};    // pingInterval + pingTimeout without a ping: lost
    QTimer m_retryTimer{this};
    QTimer m_closeTimer{this}; // shutdown(): the relay's side of the close
    std::chrono::milliseconds m_pingDeadline{45000};
    std::chrono::milliseconds m_retryDelay{};
    std::optional<std::chrono::steady_clock::time_point> m_lostAt;
    std::optional<Error> m_lostError;
    std::deque<Outgoing> m_queue; // waiting for the namespace or the socket's buffer
    std::size_t m_queuedBytes = 0;
    std::size_t m_inFlightBytes = 0; // handed to the socket, not written yet (approximate)
    std::uint64_t m_droppedLines = 0;
    int m_oversized = 0;
    int m_protocolErrors = 0;
};

} // namespace hwio::remote::detail
