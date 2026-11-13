#include "hwio/remote/protocol.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "quoted.h"

#include <QByteArray>
#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>
#include <QStringView>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <utility>

namespace hwio::remote {

namespace {

std::unexpected<Error> protocolError(std::string detail) {
    return std::unexpected(makeError(ErrorCode::RemoteProtocolError, std::move(detail)));
}

} // namespace

namespace engineio {

Result<Packet> parsePacket(QStringView text) {
    if (text.isEmpty()) {
        return protocolError("empty Engine.IO packet");
    }
    const QChar first = text.front();
    if (first < QLatin1Char('0') || first > QLatin1Char('6')) {
        return protocolError(std::format("unknown Engine.IO packet type {}",
                                         detail::quoted(toStdString(QString(first)))));
    }
    return Packet{.type = static_cast<PacketType>(first.unicode() - u'0'),
                  .data = text.mid(1).toString()};
}

Result<OpenInfo> parseOpen(QStringView data) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return protocolError("Engine.IO open packet is not a JSON object");
    }
    const QJsonObject object = document.object();
    OpenInfo info;
    info.sid = object.value(QStringLiteral("sid")).toString();
    if (info.sid.isEmpty()) {
        return protocolError("Engine.IO open packet without sid");
    }
    constexpr double maxInterval = 600000.0; // 10 min
    const auto interval =
        [&](const QString& key,
            std::chrono::milliseconds fallback) -> Result<std::chrono::milliseconds> {
        const QJsonValue value = object.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        const double ms = value.toDouble(-1.0);
        if (!value.isDouble() || ms < 1.0 || ms > maxInterval) {
            return protocolError(std::format("Engine.IO {} out of range", toStdString(key)));
        }
        return std::chrono::milliseconds(static_cast<std::int64_t>(ms));
    };
    auto pingInterval = interval(QStringLiteral("pingInterval"), info.pingInterval);
    if (!pingInterval) {
        return std::unexpected(std::move(pingInterval).error());
    }
    auto pingTimeout = interval(QStringLiteral("pingTimeout"), info.pingTimeout);
    if (!pingTimeout) {
        return std::unexpected(std::move(pingTimeout).error());
    }
    info.pingInterval = *pingInterval;
    info.pingTimeout = *pingTimeout;
    const double maxPayload = object.value(QStringLiteral("maxPayload")).toDouble(0.0);
    if (maxPayload >= 1.0) {
        info.maxPayload = static_cast<std::uint64_t>(maxPayload);
    }
    return info;
}

QString encode(PacketType type, QStringView data) {
    QString text;
    text.reserve(data.size() + 1);
    text.append(QChar(static_cast<char16_t>(u'0' + static_cast<char16_t>(type))));
    text.append(data);
    return text;
}

} // namespace engineio

namespace socketio {

Result<Packet> parsePacket(QStringView text, std::size_t maxChars) {
    if (std::cmp_greater(text.size(), maxChars)) {
        return protocolError(std::format("Socket.IO packet of {} characters", text.size()));
    }
    if (text.isEmpty()) {
        return protocolError("empty Socket.IO packet");
    }
    const QChar first = text.front();
    if (first < QLatin1Char('0') || first > QLatin1Char('6')) {
        return protocolError("unknown Socket.IO packet type");
    }
    Packet packet;
    packet.type = static_cast<PacketType>(first.unicode() - u'0');
    if (packet.type == PacketType::BinaryEvent || packet.type == PacketType::BinaryAck) {
        return protocolError("binary Socket.IO packets are not supported");
    }
    qsizetype pos = 1;
    if (pos < text.size() && text[pos] == QLatin1Char('/')) {
        const qsizetype comma = text.indexOf(QLatin1Char(','), pos);
        // A namespace without data may end the packet without a comma ("1/123").
        const qsizetype end = comma < 0 ? text.size() : comma;
        packet.nsp = text.mid(pos, end - pos).toString();
        if (packet.nsp.size() < 2 || packet.nsp.size() > 64) {
            return protocolError("bad Socket.IO namespace");
        }
        pos = comma < 0 ? text.size() : comma + 1;
    }
    // An acknowledgement id (digits) before the data; not used by the relay, ignored.
    while (pos < text.size() && text[pos].isDigit()) {
        ++pos;
    }
    if (pos < text.size()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(text.mid(pos).toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            return protocolError(std::format("Socket.IO data is not JSON: {}",
                                             toStdString(parseError.errorString())));
        }
        packet.data =
            document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
    }
    return packet;
}

Result<Event> toEvent(const Packet& packet) {
    if (!packet.data.isArray()) {
        return protocolError("Socket.IO event without an argument array");
    }
    const QJsonArray arguments = packet.data.toArray();
    if (arguments.isEmpty() || !arguments.at(0).isString()) {
        return protocolError("Socket.IO event without a name");
    }
    return Event{.name = arguments.at(0).toString(),
                 .payload = arguments.size() > 1 ? arguments.at(1) : QJsonValue(QJsonValue::Null)};
}

namespace {

QString header(PacketType type, QStringView nsp) {
    QString text = engineio::encode(engineio::PacketType::Message);
    text.append(QChar(static_cast<char16_t>(u'0' + static_cast<char16_t>(type))));
    if (nsp != u"/") {
        text.append(nsp);
        text.append(QLatin1Char(','));
    }
    return text;
}

} // namespace

QString encodeConnect(QStringView nsp) { return header(PacketType::Connect, nsp); }

QString encodeDisconnect(QStringView nsp) { return header(PacketType::Disconnect, nsp); }

QString encodeEvent(QStringView nsp, QStringView name, const QJsonValue& payload) {
    QJsonArray arguments;
    arguments.append(name.toString());
    arguments.append(payload);
    QString text = header(PacketType::Event, nsp);
    text.append(QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact)));
    return text;
}

} // namespace socketio

} // namespace hwio::remote
