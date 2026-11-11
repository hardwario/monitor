#pragma once

// Engine.IO v4 and Socket.IO v5 packet codecs (plan/features/0044, ADR-0321): the subset the
// HARDWARIO Terminal relay uses over a WebSocket. Text packets only; binary packets, binary events
// and acknowledgements are not supported (a binary Socket.IO packet is a protocol error, an
// acknowledgement id of an incoming event is ignored). Pure functions, any thread.
//
// Engine.IO text packet: <type digit><data>; 0 open (JSON), 1 close, 2 ping, 3 pong, 4 message,
// 5 upgrade, 6 noop.
// Socket.IO packet (in an Engine.IO message): <type digit>[/<namespace>,][<ack id>][<JSON>];
// 0 CONNECT, 1 DISCONNECT, 2 EVENT, 3 ACK, 4 CONNECT_ERROR, 5 BINARY_EVENT, 6 BINARY_ACK.

#include "hwio/core/result.h"

#include <QJsonValue>
#include <QString>
#include <QStringLiteral>
#include <QStringView>

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace hwio::remote {

// Incoming event payloads larger than this are dropped (plan 0044 § Limits); checked before
// parsing.
inline constexpr std::size_t maxIncomingEventBytes = 64ULL * 1024;

namespace engineio {

enum class PacketType : std::uint8_t {
    Open = 0,
    Close = 1,
    Ping = 2,
    Pong = 3,
    Message = 4,
    Upgrade = 5,
    Noop = 6,
};

struct Packet {
    PacketType type = PacketType::Noop;
    QString data; // the text after the type digit
};

// The handshake of the open packet.
struct OpenInfo {
    QString sid;
    std::chrono::milliseconds pingInterval{25000};
    std::chrono::milliseconds pingTimeout{20000};
    std::uint64_t maxPayload = 1000000;
};

// Errors: remote.protocol_error (empty, unknown type).
[[nodiscard]] Result<Packet> parsePacket(QStringView text);
// Errors: remote.protocol_error (not a JSON object, missing sid, intervals out of 1 ms..10 min).
[[nodiscard]] Result<OpenInfo> parseOpen(QStringView data);
[[nodiscard]] QString encode(PacketType type, QStringView data = {});

} // namespace engineio

namespace socketio {

enum class PacketType : std::uint8_t {
    Connect = 0,
    Disconnect = 1,
    Event = 2,
    Ack = 3,
    ConnectError = 4,
    BinaryEvent = 5,
    BinaryAck = 6,
};

struct Packet {
    PacketType type = PacketType::Event;
    QString nsp = QStringLiteral("/");
    QJsonValue data = QJsonValue::Undefined; // Undefined when the packet has no data
};

// An EVENT: [name, payload] (more arguments are ignored; no argument = null payload).
struct Event {
    QString name;
    QJsonValue payload = QJsonValue::Null;
};

// Errors: remote.protocol_error (unknown or binary type, a bad namespace, invalid JSON, data over
// maxIncomingEventBytes).
[[nodiscard]] Result<Packet> parsePacket(QStringView text);
// Errors: remote.protocol_error (not an array, no string name).
[[nodiscard]] Result<Event> toEvent(const Packet& packet);

[[nodiscard]] QString encodeConnect(QStringView nsp);
[[nodiscard]] QString encodeDisconnect(QStringView nsp);
// 2/<nsp>,["<name>",<payload>] (compact JSON).
[[nodiscard]] QString encodeEvent(QStringView nsp, QStringView name, const QJsonValue& payload);

} // namespace socketio

} // namespace hwio::remote
