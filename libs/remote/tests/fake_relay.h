#pragma once

// FakeRelay - a QWebSocketServer that copies the HARDWARIO Terminal relay (~/terminal/server/
// index.js, plan 0044, ADR-0334) for the tests: Engine.IO v4 over WebSocket (open packet, server
// pings), Socket.IO v5 namespaces matching ^/\d+$ (others get CONNECT_ERROR "Invalid namespace"),
// `newClient` (null) to every socket of the namespace when one connects (the new one included),
// `at`, `sync` and `write` passed to every other socket of the namespace, every other event
// dropped; like Socket.IO 4, an event is dispatched on the next tick and dropped when its socket
// left the namespace (DISCONNECT, close) before. No authentication. Plain ws:// on 127.0.0.1 (the
// test builds accept http relays on loopback).
//
// Test hooks: the events it relayed (recorded, bounded), raw text to one socket, dropping a
// socket, pausing pings. Lives on the thread that creates it (its event loop must run).

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <vector>

class QWebSocket;
class QWebSocketServer;

namespace hwio::remote::testing {

struct RelayedEvent {
    QString nsp;
    QString name;
    QJsonValue payload;
};

class FakeRelay : public QObject {
    Q_OBJECT

public:
    struct Options {
        std::chrono::milliseconds pingInterval{25000};
        std::chrono::milliseconds pingTimeout{20000};
    };

    FakeRelay();
    explicit FakeRelay(Options options);
    ~FakeRelay() override;
    FakeRelay(const FakeRelay&) = delete;
    FakeRelay& operator=(const FakeRelay&) = delete;
    FakeRelay(FakeRelay&&) = delete;
    FakeRelay& operator=(FakeRelay&&) = delete;

    // http://127.0.0.1:<port> (the relay URL for RelayConfig / --relay).
    [[nodiscard]] QUrl url() const;
    [[nodiscard]] quint16 port() const;

    // Sockets connected to `nsp` ("/123456") now.
    [[nodiscard]] int clientCount(const QString& nsp) const;
    [[nodiscard]] int socketCount() const;
    // Every at / sync / write the relay passed on (at most maxRecorded, oldest dropped).
    [[nodiscard]] const std::deque<RelayedEvent>& relayed() const noexcept { return m_relayed; }
    static constexpr std::size_t maxRecorded = 10000;
    // Every event name the relay received and dropped (not at / sync / write).
    [[nodiscard]] const QList<QString>& droppedEvents() const noexcept { return m_dropped; }

    // Sends raw Engine.IO text (or a binary frame) to every socket of `nsp`.
    void sendRaw(const QString& nsp, const QString& text);
    void sendBinary(const QString& nsp);
    // Closes every socket (as a relay restart or a network drop).
    void dropAll();
    // Stops sending pings (the clients' ping timeout fires).
    void setPingsPaused(bool paused);

signals:
    void namespaceConnected(const QString& nsp);
    void eventRelayed(const QString& nsp, const QString& name);

private:
    struct Client;

    void onNewConnection();
    void onText(Client& client, const QString& text);
    void remove(QWebSocket* socket);
    void broadcast(const QString& nsp, const QString& text, const Client* except);
    Client* find(QWebSocket* socket);

    Options m_options;
    std::unique_ptr<QWebSocketServer> m_server;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::deque<RelayedEvent> m_relayed;
    QList<QString> m_dropped;
    QTimer m_pingTimer{this};
    bool m_pingsPaused = false;
    int m_nextSid = 1;
};

} // namespace hwio::remote::testing
