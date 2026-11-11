#include "hwio/remote/relay_client.h"

#include "hwio/core/bounded_queue.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/remote/protocol.h"
#include "hwio/remote/session_code.h"
#include "relay_worker.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QStringView>
#include <QThread>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketProtocol>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <latch>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace hwio::remote {

namespace {

// QWebSocket refuses bigger frames (the relay's own maxPayload is 1 MB); smaller ones over
// maxIncomingEventBytes are dropped by the worker with a warning.
constexpr qint64 maxSocketMessageBytes = 1024LL * 1024;

// shutdown(): a CONNECT the relay refuses (no ^/\d+$ namespace) with a CONNECT_ERROR, without
// side effects; its answer tells that the relay has read (and dispatched) every packet before.
constexpr QStringView flushNamespace = u"/hwio-end";

Error unreachable(std::string detail) {
    return makeError(ErrorCode::RemoteRelayUnreachable, std::move(detail));
}

Error protocol(std::string detail) {
    return makeError(ErrorCode::RemoteProtocolError, std::move(detail));
}

} // namespace

namespace detail {

RelayWorker::RelayWorker(RelayConfig config, RelayEventQueue& events, std::function<void()> wake,
                         RelaySendQueue& sends)
    : m_config(std::move(config)), m_events(events), m_wake(std::move(wake)), m_sends(sends),
      m_nsp(m_config.code.socketNamespace()) {
    m_connectTimer.setSingleShot(true);
    m_pingTimer.setSingleShot(true);
    m_retryTimer.setSingleShot(true);
    m_closeTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this,
            [this] { lost(unreachable("no connection to the relay within the connect timeout")); });
    connect(&m_pingTimer, &QTimer::timeout, this,
            [this] { lost(unreachable("no Engine.IO ping from the relay")); });
    connect(&m_retryTimer, &QTimer::timeout, this, [this] { openSocket(); });
    connect(&m_closeTimer, &QTimer::timeout, this, &RelayWorker::finishShutdown);
}

RelayWorker::~RelayWorker() { destroySocket(); }

void RelayWorker::post(RelayEvent event) {
    // The only producer is this thread and the owner only removes entries, so the size check
    // cannot be overtaken. The queue's capacity is the bound + 1: the last slot is reserved for
    // the RelayFailed that ends the connection on overflow.
    const std::size_t bound = m_events.capacity() - 1;
    if (m_events.size() >= bound && !std::holds_alternative<RelayFailed>(event)) {
        if (std::holds_alternative<RelayWarning>(event)) {
            qCWarning(log::lcRemote) << "Relay warning dropped: the event queue is full";
            return;
        }
        fail(protocol(std::format("more than {} relay events waiting to be handled", bound)));
        return;
    }
    if (m_events.push(std::move(event))) {
        m_wake();
    }
}

void RelayWorker::start() {
    if (m_phase == Phase::Idle) {
        openSocket();
    }
}

void RelayWorker::openSocket() {
    destroySocket();
    m_phase = Phase::Opening;
    m_engineOpen = false;
    m_socket = std::make_unique<QWebSocket>(QString(), QWebSocketProtocol::VersionLatest);
    m_socket->setMaxAllowedIncomingMessageSize(maxSocketMessageBytes);
    m_socket->setMaxAllowedIncomingFrameSize(maxSocketMessageBytes);
    const QUrl url = engineIoUrl(m_config.relay);
    const QList<QNetworkProxy> proxies =
        QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(m_config.relay));
    if (!proxies.isEmpty()) {
        m_socket->setProxy(proxies.front());
    }
    connect(m_socket.get(), &QWebSocket::textMessageReceived, this, &RelayWorker::onTextMessage);
    connect(m_socket.get(), &QWebSocket::binaryMessageReceived, this,
            &RelayWorker::onBinaryMessage);
    connect(m_socket.get(), &QWebSocket::disconnected, this, &RelayWorker::onSocketClosed);
    connect(m_socket.get(), &QWebSocket::bytesWritten, this, &RelayWorker::onBytesWritten);
    connect(m_socket.get(), &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError /*error*/) {
                if (m_socket) {
                    qCInfo(log::lcRemote).noquote()
                        << "Relay socket error:" << m_socket->errorString();
                }
            });
    m_connectTimer.start(m_config.limits.connectTimeout);
    qCInfo(log::lcRemote).noquote()
        << "Connecting to the relay" << m_config.relay.host() << "namespace" << m_nsp;
    m_socket->open(url);
}

void RelayWorker::destroySocket() {
    m_connectTimer.stop();
    m_pingTimer.stop();
    if (m_socket) {
        // Often called from one of the socket's own signals: never deleted synchronously. A
        // deleteLater() of the worker thread runs at the latest when the thread finishes.
        QWebSocket* socket = m_socket.release();
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_inFlightBytes = 0;
    m_engineOpen = false;
}

void RelayWorker::wakeForSends() {
    if (!m_sendWakePending.exchange(true)) {
        QMetaObject::invokeMethod(this, "drainSends", Qt::QueuedConnection);
    }
}

void RelayWorker::drainSends() {
    m_sendWakePending.store(false);
    const std::uint64_t dropped = m_sends.droppedCount();
    if (dropped != m_reportedSendDrops && m_phase != Phase::Ended) {
        const std::uint64_t count = dropped - m_reportedSendDrops;
        m_reportedSendDrops = dropped;
        post(RelayWarning{makeError(
            ErrorCode::InternalQueueFull,
            std::format("{} events to the relay dropped: too many waiting to be sent", count))});
    }
    const std::size_t budget = m_sends.capacity();
    for (std::size_t i = 0; i < budget; ++i) {
        auto item = m_sends.pop({}, std::chrono::milliseconds(0));
        if (!item) {
            return;
        }
        send(item->name, item->payload, item->droppable);
    }
    if (m_sends.size() > 0) {
        wakeForSends();
    }
}

void RelayWorker::send(const QString& name, const QJsonValue& payload, bool droppable) {
    if (m_phase == Phase::Ended) {
        return;
    }
    QString text = socketio::encodeEvent(m_nsp, name, payload);
    const auto bytes = static_cast<std::size_t>(text.toUtf8().size());
    enqueue(Outgoing{.text = std::move(text), .bytes = bytes, .droppable = droppable});
    flush();
}

void RelayWorker::enqueue(Outgoing packet) {
    const std::size_t limit = m_config.limits.outgoingQueueBytes;
    std::size_t dropped = 0;
    while (m_queuedBytes + m_inFlightBytes + packet.bytes > limit) {
        const auto it =
            std::ranges::find_if(m_queue, [](const Outgoing& o) { return o.droppable; });
        if (it == m_queue.end()) {
            break; // nothing droppable is left; only non-droppable packets exceed the bound
        }
        m_queuedBytes -= it->bytes;
        m_queue.erase(it);
        ++dropped;
    }
    if (packet.droppable && m_queuedBytes + m_inFlightBytes + packet.bytes > limit) {
        ++dropped; // the new line itself does not fit
    } else {
        m_queuedBytes += packet.bytes;
        m_queue.push_back(std::move(packet));
    }
    if (dropped == 0) {
        return;
    }
    m_droppedLines += dropped;
    QJsonObject notice;
    notice.insert(QStringLiteral("event"), QStringLiteral("read"));
    notice.insert(QStringLiteral("data"),
                  QCoreApplication::translate("hwio::remote", "… %n line(s) dropped …", nullptr,
                                              static_cast<int>(dropped)));
    QString text = socketio::encodeEvent(m_nsp, QStringLiteral("at"), notice);
    const auto bytes = static_cast<std::size_t>(text.toUtf8().size());
    m_queuedBytes += bytes;
    m_queue.push_back(Outgoing{.text = std::move(text), .bytes = bytes, .droppable = false});
    post(RelayWarning{makeError(
        ErrorCode::InternalQueueFull,
        std::format("{} device lines dropped: the connection to the relay is too slow", dropped))});
}

void RelayWorker::flush() {
    if (m_phase != Phase::Connected || !m_socket) {
        return;
    }
    const std::size_t limit = m_config.limits.outgoingQueueBytes;
    while (!m_queue.empty() &&
           (m_inFlightBytes == 0 || m_inFlightBytes + m_queue.front().bytes <= limit)) {
        Outgoing packet = std::move(m_queue.front());
        m_queue.pop_front();
        m_queuedBytes -= packet.bytes;
        m_inFlightBytes += packet.bytes;
        rawSend(packet.text);
    }
}

void RelayWorker::rawSend(const QString& text) {
    if (m_socket) {
        m_socket->sendTextMessage(text);
    }
}

void RelayWorker::onBytesWritten(qint64 bytes) {
    const auto written = static_cast<std::size_t>(std::max<qint64>(bytes, 0));
    m_inFlightBytes = written >= m_inFlightBytes ? 0 : m_inFlightBytes - written;
    flush();
}

void RelayWorker::onBinaryMessage() {
    protocolProblem(protocol("binary WebSocket message from the relay"), false);
}

void RelayWorker::onTextMessage(const QString& message) {
    if (m_closing) {
        // shutdown(): only the relay's answer to the flush CONNECT matters now.
        const auto eio = engineio::parsePacket(message);
        if (eio && eio->type == engineio::PacketType::Message) {
            const auto sio = socketio::parsePacket(eio->data);
            if (sio && sio->nsp == flushNamespace) {
                closeSocket();
            }
        }
        return;
    }
    if (m_phase == Phase::Ended) {
        return;
    }
    handleEngineIo(message);
}

void RelayWorker::handleEngineIo(const QString& message) {
    auto packet = engineio::parsePacket(message);
    if (!packet) {
        protocolProblem(std::move(packet).error(), false);
        return;
    }
    switch (packet->type) {
    case engineio::PacketType::Open: {
        auto open = engineio::parseOpen(packet->data);
        if (!open) {
            fail(std::move(open).error());
            return;
        }
        m_engineOpen = true;
        m_pingDeadline = open->pingInterval + open->pingTimeout;
        armPingTimer();
        rawSend(socketio::encodeConnect(m_nsp));
        return;
    }
    case engineio::PacketType::Ping:
        armPingTimer();
        rawSend(engineio::encode(engineio::PacketType::Pong));
        return;
    case engineio::PacketType::Close:
        lost(unreachable("the relay closed the Engine.IO session"));
        return;
    case engineio::PacketType::Message:
        if (!m_engineOpen) {
            protocolProblem(protocol("Engine.IO message before the open packet"), false);
            return;
        }
        handleSocketIo(packet->data);
        return;
    case engineio::PacketType::Pong:
    case engineio::PacketType::Upgrade:
    case engineio::PacketType::Noop:
        return;
    }
}

void RelayWorker::handleSocketIo(const QString& data) {
    if (std::cmp_greater(data.size(), maxIncomingEventBytes) ||
        std::cmp_greater(data.toUtf8().size(), maxIncomingEventBytes)) {
        protocolProblem(
            protocol(std::format("incoming event of {} characters dropped", data.size())), true);
        return;
    }
    auto packet = socketio::parsePacket(data);
    if (!packet) {
        protocolProblem(std::move(packet).error(), false);
        return;
    }
    if (packet->nsp != m_nsp) {
        protocolProblem(protocol("Socket.IO packet for another namespace"), false);
        return;
    }
    switch (packet->type) {
    case socketio::PacketType::Connect: {
        if (m_phase != Phase::Opening) {
            return;
        }
        m_connectTimer.stop();
        m_phase = Phase::Connected;
        const bool reconnected = m_everConnected;
        m_everConnected = true;
        m_lostAt.reset();
        m_lostError.reset();
        m_retryDelay = {};
        qCInfo(log::lcRemote).noquote() << "Relay namespace connected" << m_nsp;
        post(RelayConnected{reconnected});
        flush();
        return;
    }
    case socketio::PacketType::ConnectError: {
        const QString message = packet->data.toObject().value(QStringLiteral("message")).toString();
        fail(protocol(std::format("the relay refused the session: {}", toStdString(message))));
        return;
    }
    case socketio::PacketType::Disconnect:
        lost(unreachable("the relay disconnected the session"));
        return;
    case socketio::PacketType::Event: {
        if (m_phase != Phase::Connected) {
            return;
        }
        auto event = socketio::toEvent(*packet);
        if (!event) {
            protocolProblem(std::move(event).error(), false);
            return;
        }
        post(RelayIncomingEvent{.name = std::move(event->name),
                                .payload = std::move(event->payload)});
        return;
    }
    case socketio::PacketType::Ack:
    case socketio::PacketType::BinaryEvent:
    case socketio::PacketType::BinaryAck:
        return; // binary types are rejected by parsePacket(); acknowledgements are not used
    }
}

void RelayWorker::armPingTimer() { m_pingTimer.start(m_pingDeadline); }

void RelayWorker::onSocketClosed() {
    if (m_phase == Phase::Ended) {
        return;
    }
    const QString reason = m_socket ? m_socket->errorString() : QString();
    lost(unreachable(std::format("the connection to the relay closed: {}", toStdString(reason))));
}

void RelayWorker::lost(Error error) {
    if (m_phase == Phase::Ended || m_phase == Phase::WaitingRetry) {
        return;
    }
    destroySocket();
    if (!m_everConnected) {
        fail(error); // the first connect is not retried
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!m_lostAt) {
        m_lostAt = now;
        m_lostError = error;
        qCInfo(log::lcRemote).noquote() << "Relay connection lost:" << toQString(error.detail);
        post(RelayConnectionLost{error});
    }
    if (now - m_lostAt.value_or(now) >= m_config.limits.reconnectBudget) {
        fail(m_lostError.value_or(std::move(error)));
        return;
    }
    m_retryDelay = m_retryDelay.count() == 0
                       ? m_config.limits.reconnectFirstDelay
                       : std::min(m_retryDelay * 2, m_config.limits.reconnectMaxDelay);
    m_phase = Phase::WaitingRetry;
    m_retryTimer.start(m_retryDelay);
}

void RelayWorker::fail(const Error& error) {
    if (m_phase == Phase::Ended) {
        return;
    }
    destroySocket();
    m_retryTimer.stop();
    m_phase = Phase::Ended;
    m_queue.clear();
    m_queuedBytes = 0;
    qCWarning(log::lcRemote).noquote() << "Relay connection failed:" << toQString(error.detail);
    post(RelayFailed{error});
}

void RelayWorker::protocolProblem(const Error& error, bool oversized) {
    qCWarning(log::lcRemote).noquote() << "Relay protocol problem:" << toQString(error.detail);
    if (oversized ? ++m_oversized >= m_config.limits.maxOversizedEvents
                  : ++m_protocolErrors >= m_config.limits.maxProtocolErrors) {
        fail(error);
        return;
    }
    post(RelayWarning{error});
}

void RelayWorker::shutdown() {
    // emitEvent() calls made before stop() go out first, as with the queued calls before
    // (e.g. the tunnel `end`, ADR-0354).
    while (auto item = m_sends.pop({}, std::chrono::milliseconds(0))) {
        send(item->name, item->payload, item->droppable);
    }
    const bool connected = m_phase == Phase::Connected && m_socket;
    m_phase = Phase::Ended; // no more notifications, onSocketClosed() included
    m_retryTimer.stop();
    m_connectTimer.stop();
    m_pingTimer.stop();
    if (!connected) {
        finishShutdown();
        return;
    }
    // Packets still waiting behind in-flight bytes (e.g. the tunnel `end`, ADR-0354) go out
    // before the disconnect; droppable ones may be lost. The queue is bounded (enqueue).
    for (const Outgoing& packet : m_queue) {
        if (!packet.droppable) {
            rawSend(packet.text);
        }
    }
    m_queue.clear();
    m_queuedBytes = 0;
    // The relay (Socket.IO 4) dispatches a received event on the next tick and drops it when the
    // socket has disconnected (DISCONNECT or a closed WebSocket) by then: a close read together
    // with the `end` would lose the `end`. So the close waits for the answer to a CONNECT the
    // relay refuses (closeSocket()); the relay has dispatched every earlier packet by then. At
    // the latest after closeTimeout the socket is aborted (finishShutdown()).
    m_closing = true;
    rawSend(socketio::encodeConnect(flushNamespace));
    m_closeTimer.start(m_config.limits.closeTimeout);
}

void RelayWorker::closeSocket() {
    if (!m_closing || !m_socket) {
        return;
    }
    m_closing = false;
    rawSend(socketio::encodeDisconnect(m_nsp));
    // Not aborted before Qt has written everything and closed (disconnected).
    connect(m_socket.get(), &QWebSocket::disconnected, this, &RelayWorker::finishShutdown);
    m_socket->flush();
    m_socket->close(QWebSocketProtocol::CloseCodeNormal);
}

void RelayWorker::finishShutdown() {
    m_closing = false;
    m_closeTimer.stop();
    m_phase = Phase::Ended;
    destroySocket();
    emit shutdownFinished();
}

} // namespace detail

RelayClient::RelayClient(RelayConfig config, QObject* parent)
    : QObject(parent), m_config(std::move(config)),
      m_events(std::make_unique<detail::RelayEventQueue>(
          std::max<std::size_t>(m_config.limits.incomingEventQueue, 1) + 1,
          OverflowPolicy::Reject)),
      m_sends(std::make_unique<detail::RelaySendQueue>(
          std::max<std::size_t>(m_config.limits.outgoingSendQueue, 1),
          [](const detail::RelaySend& item) { return item.droppable; })) {}

RelayClient::~RelayClient() { stop(); }

void RelayClient::start() {
    if (m_started) {
        return;
    }
    m_started = true;
    std::latch ready(1);
    m_thread = std::jthread([this, &ready] {
        // A thread Qt adopts: QThread::currentThread() creates its QThread object; the QEventLoop
        // creates the thread's event dispatcher, before the worker creates its QWebSocket and
        // timers here, on this thread. Every QObject of the thread is destroyed in this body.
        QThread::currentThread()->setObjectName(QStringLiteral("hwio-remote-relay"));
        QEventLoop loop;
        detail::RelayWorker worker(m_config, *m_events, [this] { wakeUp(); }, *m_sends);
        QObject::connect(&worker, &detail::RelayWorker::shutdownFinished, &loop, &QEventLoop::quit);
        m_worker = &worker;
        ready.count_down(); // m_worker is published to the owner
        worker.start();
        loop.exec();
        // The sockets destroySocket() handed to deleteLater().
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    ready.wait();
}

void RelayClient::wakeUp() {
    // Worker thread. One coalesced queued call by name, without a payload (ADR-0357, as
    // DeviceSession::wakeUp(), ADR-0031).
    if (!m_wakePending.exchange(true)) {
        QMetaObject::invokeMethod(this, "drainEvents", Qt::QueuedConnection);
    }
}

void RelayClient::drainEvents() {
    m_wakePending.store(false);
    // Bounded per call; anything left is drained by the next (rescheduled) call.
    const std::size_t budget = m_events->capacity();
    for (std::size_t i = 0; i < budget && !m_stopped; ++i) {
        auto event = m_events->pop({}, std::chrono::milliseconds(0));
        if (!event) {
            break;
        }
        std::visit(
            [this]<class E>(E& item) {
                if constexpr (std::is_same_v<E, detail::RelayConnected>) {
                    m_connected = true;
                    emit connected(item.reconnected);
                } else if constexpr (std::is_same_v<E, detail::RelayConnectionLost>) {
                    m_connected = false;
                    emit connectionLost(item.error);
                } else if constexpr (std::is_same_v<E, detail::RelayFailed>) {
                    m_connected = false;
                    emit failed(item.error);
                } else if constexpr (std::is_same_v<E, detail::RelayWarning>) {
                    emit warning(item.error);
                } else {
                    onEvent(item.name, item.payload);
                }
            },
            *event);
    }
    if (!m_stopped && m_events->size() > 0) {
        wakeUp();
    }
}

void RelayClient::onEvent(const QString& name, const QJsonValue& payload) {
    if (m_stopped) {
        return;
    }
    if (name == QStringLiteral("newClient")) {
        emit newClient();
    } else if (name == QStringLiteral("at")) {
        emit atReceived(payload);
    } else if (name == QStringLiteral("sync")) {
        emit syncReceived(payload);
    } else if (name == QStringLiteral("write")) {
        emit writeReceived(payload);
    } else {
        qCDebug(log::lcRemote).noquote() << "Ignored relay event" << name.left(64);
    }
}

void RelayClient::emitEvent(const QString& name, const QJsonValue& payload, bool droppable) {
    if (!m_started || m_stopped) {
        return;
    }
    if (m_sends->push(
            detail::RelaySend{.name = name, .payload = payload, .droppable = droppable})) {
        m_worker->wakeForSends();
    }
}

void RelayClient::stop() {
    if (m_stopped) {
        return;
    }
    m_stopped = true;
    m_connected = false;
    if (!m_started) {
        return;
    }
    // On the relay thread: the remaining packets and the DISCONNECT, the graceful close (at most
    // closeTimeout), then the end of its event loop (shutdownFinished). join() is the
    // happens-before edge ThreadSanitizer sees.
    QMetaObject::invokeMethod(m_worker, "shutdown", Qt::QueuedConnection);
    m_thread.join();
    m_worker = nullptr;
}

} // namespace hwio::remote
