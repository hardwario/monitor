#pragma once

// Test helpers of libs/remote: event-loop pumping (as libs/mcp/tests/mcp_test_support.h) and a raw
// Socket.IO participant on the test thread that speaks to the FakeRelay like the web Terminal does
// (socket.io-client with transports: ["websocket"]). Tests pump the Qt event loop with runUntil().

#include "hwio/remote/protocol.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QThread>
#include <QUrl>
#include <QWebSocket>

#include <chrono>
#include <functional>
#include <memory>

namespace hwio::remote::testing {

// Processes events until `done()` is true or `timeout` passed; returns done().
inline bool runUntil(const std::function<bool()>& done,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
    const QDeadlineTimer deadline(timeout);
    while (!done()) {
        if (deadline.hasExpired()) {
            return done();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return true;
}

inline void runFor(std::chrono::milliseconds duration) {
    const QDeadlineTimer deadline(duration);
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
}

// One raw Socket.IO participant (the web Terminal's socket.io-client) on the test thread: answers
// pings, connects `nsp`, records the events it receives, emits events.
class RawParticipant : public QObject {
public:
    struct Received {
        QString name;
        QJsonValue payload;
    };

    RawParticipant(const QUrl& relay, QString nsp) : m_nsp(std::move(nsp)) {
        QObject::connect(&m_socket, &QWebSocket::textMessageReceived, this,
                         [this](const QString& text) { onText(text); });
        QObject::connect(&m_socket, &QWebSocket::disconnected, this, [this] { closed = true; });
        QUrl url = relay;
        url.setScheme(QStringLiteral("ws"));
        url.setPath(QStringLiteral("/socket.io/"));
        url.setQuery(QStringLiteral("EIO=4&transport=websocket"));
        m_socket.open(url);
    }

    bool waitConnected() {
        return runUntil([this] { return connected; });
    }

    void emitEvent(const QString& name, const QJsonValue& payload) {
        m_socket.sendTextMessage(socketio::encodeEvent(m_nsp, name, payload));
    }
    void sendRaw(const QString& text) { m_socket.sendTextMessage(text); }
    void close() { m_socket.close(); }

    [[nodiscard]] QList<Received> named(const QString& name) const {
        QList<Received> out;
        for (const Received& r : received) {
            if (r.name == name) {
                out.append(r);
            }
        }
        return out;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    bool connected = false;
    bool closed = false;
    QString connectError;
    QList<Received> received;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    void onText(const QString& text) {
        if (text == QStringLiteral("2")) {
            m_socket.sendTextMessage(QStringLiteral("3"));
            return;
        }
        if (text.startsWith(QLatin1Char('0'))) {
            m_socket.sendTextMessage(socketio::encodeConnect(m_nsp));
            return;
        }
        if (!text.startsWith(QLatin1Char('4'))) {
            return;
        }
        auto packet = socketio::parsePacket(QStringView(text).mid(1));
        if (!packet) {
            return;
        }
        if (packet->type == socketio::PacketType::Connect) {
            connected = true;
        } else if (packet->type == socketio::PacketType::ConnectError) {
            connectError = packet->data.toObject().value(QStringLiteral("message")).toString();
        } else if (packet->type == socketio::PacketType::Event) {
            if (auto event = socketio::toEvent(*packet)) {
                received.append(Received{.name = event->name, .payload = event->payload});
            }
        }
    }

    QString m_nsp;
    QWebSocket m_socket;
};

} // namespace hwio::remote::testing
