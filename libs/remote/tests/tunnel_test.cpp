#include "hwio/core/error.h"
#include "hwio/remote/tunnel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <gtest/gtest.h>

#include <variant>

namespace hwio::remote::tunnel {
namespace {

QJsonValue json(const char* text) { return QJsonDocument::fromJson(QByteArray(text)).object(); }

TEST(Tunnel, HostMessagesRoundTrip) {
    const HostMessage messages[] = {
        HostHello{.app = "hwio-monitor",
                  .version = "2.1.0",
                  .device = {.name = "CHESTER",
                             .serial = "1",
                             .transport = "jlink",
                             .firmware = "Clime v3"},
                  .ops = true},
        OpAccepted{.id = "op-1"},
        OpDenied{.id = "op-1", .code = "internal.busy"},
        OpProgress{
            .id = "op-1", .phase = "programming", .step = 1, .steps = 2, .done = 5, .total = 10},
        OpResult{.id = "op-1", .ok = false, .code = "firmware.verify_failed"},
        OpResult{.id = "op-1", .ok = true, .code = std::nullopt},
    };
    for (const HostMessage& message : messages) {
        const QJsonObject at = encodeHost(message);
        EXPECT_EQ(at.value(QStringLiteral("event")).toString(), QStringLiteral("hwio"));
        EXPECT_TRUE(isTunnelAt(at));
        auto decoded = decodeHost(at);
        ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
        EXPECT_EQ(decoded->index(), message.index());
    }
    const auto hello = std::get<HostHello>(*decodeHost(encodeHost(messages[0])));
    EXPECT_TRUE(hello.ops);
    EXPECT_EQ(hello.device.firmware, "Clime v3");
}

TEST(Tunnel, JoinerMessagesRoundTrip) {
    const QJsonObject write =
        encodeJoiner(OpRequest{.id = "a",
                               .kind = OpKind::Flash,
                               .target = OpTarget::App,
                               .firmwareId = "0123456789ABCDEF0123456789abcdef"});
    EXPECT_TRUE(write.contains(QStringLiteral("hwio")));
    auto decoded = decodeJoiner(write);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
    const auto request = std::get<OpRequest>(*decoded);
    EXPECT_EQ(request.kind, OpKind::Flash);
    ASSERT_TRUE(request.firmwareId.has_value());
    auto hello = decodeJoiner(encodeJoiner(JoinerHello{.app = "hwio-monitor", .version = "2.1"}));
    ASSERT_TRUE(hello.has_value());
    EXPECT_TRUE(std::holds_alternative<JoinerHello>(*hello));
}

TEST(Tunnel, StringsAreNeverTunnelMessages) {
    EXPECT_FALSE(decodeJoiner(QJsonValue(QStringLiteral("{\"hwio\":{\"v\":1}}"))).has_value());
    EXPECT_FALSE(isTunnelAt(QJsonValue(QStringLiteral("hwio"))));
}

TEST(Tunnel, RejectsInvalidJoinerMessages) {
    for (
        const char* text : {
            R"({})",
            R"({"hwio":"x"})",
            R"({"hwio":{"v":2,"type":"hello","app":"hwio-monitor","version":"1"}})",
            R"({"hwio":{"v":"1","type":"hello","app":"hwio-monitor","version":"1"}})",
            R"({"hwio":{"v":1,"type":"bye"}})",
            R"({"hwio":{"v":1,"type":"hello","app":"other","version":"1"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"format","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"reset","target":"uicr"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a b","kind":"reset","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"","kind":"reset","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"0123456789012345678901234567890123","kind":"reset","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"flash","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"flash","target":"app","firmwareId":"xyz"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"flash","target":"app","firmwareId":"../../etc/passwd"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"reset","target":"app","firmwareId":"0123456789abcdef0123456789abcdef"}})",
        }) {
        const auto decoded = decodeJoiner(json(text));
        ASSERT_FALSE(decoded.has_value()) << text;
        EXPECT_EQ(decoded.error().code, ErrorCode::RemoteProtocolError) << text;
    }
}

TEST(Tunnel, RejectsInvalidHostMessages) {
    for (
        const char* text : {
            R"({"event":"hwio","data":{"v":1,"type":"op-denied","id":"a","code":"made.up"}})",
            R"({"event":"hwio","data":{"v":1,"type":"op-result","id":"a","ok":false}})",
            R"({"event":"hwio","data":{"v":1,"type":"op-progress","id":"a","step":0,"steps":1,"done":0,"total":0}})",
            R"({"event":"hwio","data":{"v":1,"type":"op-progress","id":"a","step":1,"steps":1,"done":-1,"total":0}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"1","ops":true}})",
            R"({"event":"read","data":"x"})",
        }) {
        EXPECT_FALSE(decodeHost(json(text)).has_value()) << text;
    }
}

TEST(Tunnel, DisplayTextIsSanitisedAndBounded) {
    auto hello = decodeHost(json(
        R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"2\u001b[31m","device":{"name":"A\u0007B","serial":"","transport":"ble","firmware":""},"ops":false}})"));
    ASSERT_TRUE(hello.has_value());
    const auto& h = std::get<HostHello>(*hello);
    EXPECT_EQ(h.device.name.find('\x07'), std::string::npos);
    EXPECT_EQ(h.version.find('\x1b'), std::string::npos);
    const std::string longName(200, 'x');
    QJsonObject at = encodeHost(
        HostHello{.app = "hwio-monitor",
                  .version = "1",
                  .device = {.name = longName, .serial = {}, .transport = {}, .firmware = {}},
                  .ops = false});
    EXPECT_FALSE(decodeHost(at).has_value());
}

} // namespace
} // namespace hwio::remote::tunnel
