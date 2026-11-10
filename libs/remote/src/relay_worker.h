#pragma once

// The worker-thread half of RelayClient (relay_client.h): the QWebSocket, Engine.IO (open, ping /
// pong, timeouts), the Socket.IO namespace, reconnect and the bounded outgoing queue. Lives on the
// RelayClient's QThread; every slot runs there.

#include "hwio/core/error.h"
#include "hwio/remote/relay_client.h"

#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtTypes>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

class QWebSocket;

namespace hwio::remote::detail {

class RelayWorker : public QObject {
    Q_OBJECT

public:
    explicit RelayWorker(RelayConfig config);
    ~RelayWorker() override;
    RelayWorker(const RelayWorker&) = delete;
    RelayWorker& operator=(const RelayWorker&) = delete;
    RelayWorker(RelayWorker&&) = delete;
    RelayWorker& operator=(RelayWorker&&) = delete;

    Q_INVOKABLE void start();
    Q_INVOKABLE void send(const QString& name, const QJsonValue& payload, bool droppable);
    // DISCONNECT (when connected), close, no more signals. Called blocking from the owner.
    Q_INVOKABLE void shutdown();
    // Blocks until shutdown() has finished on the worker thread. Called by the owner after the
    // blocking shutdown() call and before deleting the worker: the BlockingQueuedConnection and
    // QThread::wait() synchronize through futexes inside uninstrumented libQt6Core, which
    // ThreadSanitizer cannot see; this std::mutex / std::condition_variable hand-off gives it a
    // visible happens-before edge between shutdown() and the destructor on the owner's thread.
    void waitUntilShutDown();

signals:
    void connected(bool reconnected);
    void connectionLost(const hwio::Error& error);
    void failed(const hwio::Error& error);
    void eventReceived(const QString& name, const QJsonValue& payload);
    void warning(const hwio::Error& error);

private:
    std::mutex m_shutdownMutex;           // guards m_shutdownDone
    std::condition_variable m_shutdownCv; // notified when shutdown() has finished
    bool m_shutdownDone = false;

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

    RelayConfig m_config;
    QString m_nsp;
    Phase m_phase = Phase::Idle;
    bool m_everConnected = false;
    bool m_engineOpen = false;
    std::unique_ptr<QWebSocket> m_socket;
    // Children of the worker, so that moveToThread() moves them along.
    QTimer m_connectTimer{this}; // the open phase
    QTimer m_pingTimer{this};    // pingInterval + pingTimeout without a ping: lost
    QTimer m_retryTimer{this};
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
