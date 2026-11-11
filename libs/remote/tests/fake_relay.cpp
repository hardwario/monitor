#include "fake_relay.h"

#include "hwio/remote/protocol.h"

#include <QByteArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketProtocol>
#include <QWebSocketServer>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace hwio::remote::testing {

struct FakeRelay::Client {
    std::unique_ptr<QWebSocket> socket;
    QString sid;
    QString nsp; // empty until the namespace is connected
};

FakeRelay::FakeRelay() : FakeRelay(Options{}) {}

FakeRelay::FakeRelay(Options options)
    : m_options(options), m_server(std::make_unique<QWebSocketServer>(
                              QStringLiteral("fake-relay"), QWebSocketServer::NonSecureMode)) {
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        qFatal("FakeRelay: cannot listen");
    }
    connect(m_server.get(), &QWebSocketServer::newConnection, this, &FakeRelay::onNewConnection);
    m_pingTimer.setInterval(m_options.pingInterval);
    connect(&m_pingTimer, &QTimer::timeout, this, [this] {
        if (m_pingsPaused) {
            return;
        }
        for (const auto& client : m_clients) {
            client->socket->sendTextMessage(QStringLiteral("2"));
        }
    });
    m_pingTimer.start();
}

FakeRelay::~FakeRelay() {
    for (auto& client : m_clients) {
        client->socket->disconnect(this);
        client->socket->abort();
    }
    m_clients.clear();
    m_server->close();
}

QUrl FakeRelay::url() const {
    return {QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort())};
}

quint16 FakeRelay::port() const { return m_server->serverPort(); }

int FakeRelay::clientCount(const QString& nsp) const {
    return static_cast<int>(std::ranges::count_if(
        m_clients, [&nsp](const std::unique_ptr<Client>& client) { return client->nsp == nsp; }));
}

int FakeRelay::socketCount() const { return static_cast<int>(m_clients.size()); }

void FakeRelay::onNewConnection() {
    while (QWebSocket* raw = m_server->nextPendingConnection()) {
        auto client = std::make_unique<Client>();
        client->socket.reset(raw); // the server hands over ownership
        client->sid = QStringLiteral("sid%1").arg(m_nextSid++);
        Client* self = client.get();
        connect(raw, &QWebSocket::textMessageReceived, this, [this, raw](const QString& text) {
            if (Client* c = find(raw)) {
                onText(*c, text);
            }
        });
        connect(raw, &QWebSocket::disconnected, this, [this, raw] { remove(raw); });
        m_clients.push_back(std::move(client));
        // Engine.IO v4 open packet (websocket only: no upgrades).
        QJsonObject open;
        open.insert(QStringLiteral("sid"), self->sid);
        open.insert(QStringLiteral("upgrades"), QJsonValue(QJsonValue::Array));
        open.insert(QStringLiteral("pingInterval"),
                    static_cast<double>(m_options.pingInterval.count()));
        open.insert(QStringLiteral("pingTimeout"),
                    static_cast<double>(m_options.pingTimeout.count()));
        open.insert(QStringLiteral("maxPayload"), 1000000);
        raw->sendTextMessage(QStringLiteral("0") +
                             QString::fromUtf8(QJsonDocument(open).toJson(QJsonDocument::Compact)));
    }
}

FakeRelay::Client* FakeRelay::find(QWebSocket* socket) {
    const auto it = std::ranges::find_if(m_clients, [socket](const std::unique_ptr<Client>& c) {
        return c->socket.get() == socket;
    });
    return it == m_clients.end() ? nullptr : it->get();
}

void FakeRelay::onText(Client& client, const QString& text) {
    auto eio = engineio::parsePacket(text);
    if (!eio || eio->type != engineio::PacketType::Message) {
        return; // pongs and anything else
    }
    auto sio = socketio::parsePacket(eio->data);
    if (!sio) {
        return;
    }
    static const QRegularExpression namespacePattern(QStringLiteral("^/\\d+$"));
    switch (sio->type) {
    case socketio::PacketType::Connect: {
        if (!namespacePattern.match(sio->nsp).hasMatch()) {
            client.socket->sendTextMessage(
                QStringLiteral("44%1,{\"message\":\"Invalid namespace\"}").arg(sio->nsp));
            return;
        }
        client.nsp = sio->nsp;
        client.socket->sendTextMessage(
            QStringLiteral("40%1,{\"sid\":\"%2\"}").arg(sio->nsp, client.sid));
        emit namespaceConnected(sio->nsp);
        // nsp.emit('newClient', null): every socket of the namespace, the new one included.
        broadcast(sio->nsp, socketio::encodeEvent(sio->nsp, u"newClient", QJsonValue::Null),
                  nullptr);
        return;
    }
    case socketio::PacketType::Disconnect:
        client.nsp.clear();
        return;
    case socketio::PacketType::Event: {
        if (client.nsp.isEmpty() || client.nsp != sio->nsp) {
            return;
        }
        auto event = socketio::toEvent(*sio);
        if (!event) {
            return;
        }
        if (event->name != QStringLiteral("at") && event->name != QStringLiteral("sync") &&
            event->name != QStringLiteral("write")) {
            m_dropped.append(event->name);
            return;
        }
        // Socket.IO 4 dispatches a received event on the next tick (socket.dispatch():
        // process.nextTick) and ignores it when the socket has disconnected by then: a DISCONNECT
        // or a WebSocket close read in the same batch (both handled at once) loses it.
        QMetaObject::invokeMethod(
            this,
            [this, socket = client.socket.get(), nsp = client.nsp, name = event->name,
             payload = event->payload] {
                Client* sender = find(socket);
                if (sender == nullptr || sender->nsp != nsp) {
                    return; // "ignore packet received after disconnection"
                }
                m_relayed.push_back(RelayedEvent{.nsp = nsp, .name = name, .payload = payload});
                if (m_relayed.size() > maxRecorded) {
                    m_relayed.pop_front();
                }
                // socket.broadcast.emit(name, data): every other socket of the namespace.
                broadcast(nsp, socketio::encodeEvent(nsp, name, payload), sender);
                emit eventRelayed(nsp, name);
            },
            Qt::QueuedConnection);
        return;
    }
    case socketio::PacketType::Ack:
    case socketio::PacketType::ConnectError:
    case socketio::PacketType::BinaryEvent:
    case socketio::PacketType::BinaryAck:
        return;
    }
}

void FakeRelay::broadcast(const QString& nsp, const QString& text, const Client* except) {
    for (const auto& client : m_clients) {
        if (client.get() != except && client->nsp == nsp) {
            client->socket->sendTextMessage(text);
        }
    }
}

void FakeRelay::remove(QWebSocket* socket) {
    const auto it = std::ranges::find_if(m_clients, [socket](const std::unique_ptr<Client>& c) {
        return c->socket.get() == socket;
    });
    if (it != m_clients.end()) {
        (*it)->socket->disconnect(this);
        (*it)->socket.release()->deleteLater(); // inside its own signal: delete later
        m_clients.erase(it);
    }
}

void FakeRelay::sendRaw(const QString& nsp, const QString& text) {
    for (const auto& client : m_clients) {
        if (client->nsp == nsp) {
            client->socket->sendTextMessage(text);
        }
    }
}

void FakeRelay::sendBinary(const QString& nsp) {
    for (const auto& client : m_clients) {
        if (client->nsp == nsp) {
            client->socket->sendBinaryMessage(QByteArray("\x04\x01", 2));
        }
    }
}

void FakeRelay::dropAll() {
    std::vector<QWebSocket*> sockets;
    sockets.reserve(m_clients.size());
    for (const auto& client : m_clients) {
        sockets.push_back(client->socket.get());
    }
    for (QWebSocket* socket : sockets) {
        socket->abort();
        remove(socket);
    }
}

void FakeRelay::setPingsPaused(bool paused) { m_pingsPaused = paused; }

} // namespace hwio::remote::testing
