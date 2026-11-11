#include "hwio/core/error.h"
#include "hwio/remote/protocol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <gtest/gtest.h>

#include <cstdint>

namespace hwio::remote {
namespace {

using namespace std::chrono_literals;

TEST(EngineIo, ParsesPackets) {
    auto ping = engineio::parsePacket(u"2");
    ASSERT_TRUE(ping.has_value());
    EXPECT_EQ(ping->type, engineio::PacketType::Ping);
    auto message = engineio::parsePacket(u"42/1,[\"at\"]");
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->type, engineio::PacketType::Message);
    EXPECT_EQ(message->data, QStringLiteral("2/1,[\"at\"]"));
    EXPECT_EQ(engineio::encode(engineio::PacketType::Pong), QStringLiteral("3"));
}

TEST(EngineIo, RejectsMalformedPackets) {
    for (const char16_t* text : {u"", u"7", u"x", u"\u0000", u"-1"}) {
        const auto packet = engineio::parsePacket(text);
        ASSERT_FALSE(packet.has_value());
        EXPECT_EQ(packet.error().code, ErrorCode::RemoteProtocolError);
    }
}

TEST(EngineIo, ParsesTheOpenPacket) {
    auto open = engineio::parseOpen(
        u"{\"sid\":\"abc\",\"upgrades\":[],\"pingInterval\":25000,\"pingTimeout\":20000,"
        u"\"maxPayload\":1000000}");
    ASSERT_TRUE(open.has_value());
    EXPECT_EQ(open->sid, QStringLiteral("abc"));
    EXPECT_EQ(open->pingInterval, 25000ms);
    EXPECT_EQ(open->pingTimeout, 20000ms);
    for (const char16_t* text :
         {u"", u"[]", u"{}", u"{\"sid\":1}", u"{\"sid\":\"a\",\"pingInterval\":0}",
          u"{\"sid\":\"a\",\"pingInterval\":\"x\"}", u"{\"sid\":\"a\",\"pingTimeout\":1e12}"}) {
        EXPECT_FALSE(engineio::parseOpen(text).has_value());
    }
}

TEST(SocketIo, EncodesLikeSocketIoClient) {
    EXPECT_EQ(socketio::encodeConnect(u"/482913"), QStringLiteral("40/482913,"));
    EXPECT_EQ(socketio::encodeDisconnect(u"/482913"), QStringLiteral("41/482913,"));
    QJsonObject at;
    at.insert(QStringLiteral("event"), QStringLiteral("read"));
    at.insert(QStringLiteral("data"), QStringLiteral("ok"));
    EXPECT_EQ(socketio::encodeEvent(u"/1", u"at", at),
              QStringLiteral("42/1,[\"at\",{\"data\":\"ok\",\"event\":\"read\"}]"));
    EXPECT_EQ(socketio::encodeEvent(u"/1", u"newClient", QJsonValue::Null),
              QStringLiteral("42/1,[\"newClient\",null]"));
    EXPECT_EQ(socketio::encodeEvent(u"/1", u"write", QStringLiteral("info show")),
              QStringLiteral("42/1,[\"write\",\"info show\"]"));
}

TEST(SocketIo, ParsesEventsAndConnects) {
    auto event = socketio::parsePacket(u"2/482913,[\"write\",\"info show\"]");
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, socketio::PacketType::Event);
    EXPECT_EQ(event->nsp, QStringLiteral("/482913"));
    auto decoded = socketio::toEvent(*event);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->name, QStringLiteral("write"));
    EXPECT_EQ(decoded->payload.toString(), QStringLiteral("info show"));

    auto withAck = socketio::parsePacket(u"2/1,17[\"at\",{}]");
    ASSERT_TRUE(withAck.has_value());
    EXPECT_EQ(socketio::toEvent(*withAck)->name, QStringLiteral("at"));

    auto noPayload = socketio::toEvent(*socketio::parsePacket(u"2/1,[\"newClient\"]"));
    ASSERT_TRUE(noPayload.has_value());
    EXPECT_TRUE(noPayload->payload.isNull());

    auto connect = socketio::parsePacket(u"0/1,{\"sid\":\"x\"}");
    ASSERT_TRUE(connect.has_value());
    EXPECT_EQ(connect->type, socketio::PacketType::Connect);
    auto root = socketio::parsePacket(u"0{\"sid\":\"x\"}");
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(root->nsp, QStringLiteral("/"));
    auto disconnect = socketio::parsePacket(u"1/1,");
    ASSERT_TRUE(disconnect.has_value());
    EXPECT_EQ(disconnect->type, socketio::PacketType::Disconnect);
    EXPECT_TRUE(disconnect->data.isUndefined());
}

TEST(SocketIo, RejectsMalformedAndBinaryPackets) {
    for (const char16_t* text : {u"", u"9", u"5/1,1-[\"at\",{\"_placeholder\":true,\"num\":0}]",
                                 u"6/1,1-[]", u"2/1,[\"at\",", u"2/1,{not json}", u"2/,[\"at\"]"}) {
        const auto packet = socketio::parsePacket(text);
        EXPECT_FALSE(packet.has_value()) << QString::fromUtf16(text).toStdString();
    }
    // Valid JSON but not an event.
    for (const char16_t* text : {u"2/1,{}", u"2/1,[]", u"2/1,[1,2]"}) {
        auto packet = socketio::parsePacket(text);
        ASSERT_TRUE(packet.has_value());
        EXPECT_FALSE(socketio::toEvent(*packet).has_value());
    }
}

TEST(SocketIo, RejectsOversizedData) {
    QString big = QStringLiteral("2/1,[\"at\",\"");
    big.append(QString(static_cast<qsizetype>(maxIncomingEventBytes), QLatin1Char('x')));
    big.append(QStringLiteral("\"]"));
    EXPECT_FALSE(socketio::parsePacket(big).has_value());
}

// Fuzz-style: no input crashes the parsers (ASan/UBSan runs).
TEST(SocketIo, SurvivesGarbage) {
    const QString alphabet = QStringLiteral("0123456789/,[]{}\":ab\\ é");
    std::uint32_t state = 12345;
    for (int round = 0; round < 5000; ++round) {
        QString text;
        const int length = static_cast<int>(state % 40);
        for (int i = 0; i < length; ++i) {
            state = (state * 1103515245U) + 12345U;
            text.append(alphabet.at(static_cast<qsizetype>((state >> 16) % alphabet.size())));
        }
        state = (state * 1103515245U) + 12345U;
        // Only "does not crash" is tested: the results are not looked at.
        [[maybe_unused]] const auto engineIoPacket = engineio::parsePacket(text);
        if (auto packet = socketio::parsePacket(text)) {
            [[maybe_unused]] const auto event = socketio::toEvent(*packet);
        }
        [[maybe_unused]] const auto open = engineio::parseOpen(text);
    }
}

} // namespace
} // namespace hwio::remote
