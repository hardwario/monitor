#include "hwio/core/error.h"
#include "hwio/remote/session_code.h"

#include <QString>
#include <QUrl>

#include <gtest/gtest.h>

#include <set>

namespace hwio::remote {
namespace {

TEST(SessionCode, GeneratedCodesAreSixDigitsFromTheWholeRange) {
    std::set<std::uint32_t> seen;
    for (int i = 0; i < 2000; ++i) {
        const SessionCode code = SessionCode::generate();
        ASSERT_GE(code.value(), minHostCode);
        ASSERT_LE(code.value(), maxCode);
        EXPECT_EQ(code.text().size(), 6U);
        seen.insert(code.value());
    }
    EXPECT_GT(seen.size(), 1990U); // a CSPRNG over 900 000 values: almost no repeats
}

TEST(SessionCode, GroupedForReadingAloud) {
    EXPECT_EQ(SessionCode::parse("482913")->grouped(), "482 913");
    EXPECT_EQ(SessionCode::parse("12345")->grouped(), "12345");
    EXPECT_EQ(SessionCode::parse("482913")->socketNamespace(), QStringLiteral("/482913"));
}

TEST(SessionCode, ParsesWebTerminalCodesLikeNumber) {
    // The web joiner joins Number(code): leading zeros are dropped.
    EXPECT_EQ(SessionCode::parse("012345")->text(), "12345");
    EXPECT_EQ(SessionCode::parse("7")->socketNamespace(), QStringLiteral("/7"));
    EXPECT_EQ(SessionCode::parse("482 913")->value(), 482913U);
    EXPECT_EQ(SessionCode::parse(" 482913 ")->value(), 482913U);
}

TEST(SessionCode, RejectsInvalidCodes) {
    for (const char* text :
         {"", " ", "0", "000000", "1234567", "12a456", "-12345", "12.34", "４８２９１３"}) {
        const auto code = SessionCode::parse(text);
        ASSERT_FALSE(code.has_value()) << text;
        EXPECT_EQ(code.error().code, ErrorCode::RemoteCodeInvalid) << text;
    }
}

TEST(RelayUrl, HttpsOnly) {
    EXPECT_EQ(parseRelayUrl("https://terminal.hardwario.com/")->toString(),
              QStringLiteral("https://terminal.hardwario.com"));
    EXPECT_TRUE(parseRelayUrl("https://staging.example.com:8443/relay").has_value());
    for (const char* text : {"http://terminal.hardwario.com", "ftp://x", "terminal.hardwario.com",
                             "https://", "https://user:pw@host", "https://host/?a=1",
                             "https://host/#k=1", "wss://terminal.hardwario.com"}) {
        const auto url = parseRelayUrl(text);
        ASSERT_FALSE(url.has_value()) << text;
        EXPECT_EQ(url.error().code, ErrorCode::UsageInvalidArgument) << text;
    }
}

#ifdef HWIO_TEST_OVERRIDES
TEST(RelayUrl, TestBuildsAcceptHttpOnLoopbackOnly) {
    EXPECT_TRUE(parseRelayUrl("http://127.0.0.1:5000").has_value());
    EXPECT_TRUE(parseRelayUrl("http://localhost:5000").has_value());
    EXPECT_FALSE(parseRelayUrl("http://10.0.0.1:5000").has_value());
}
#endif

TEST(RelayUrl, EngineIoEndpointAndLink) {
    const QUrl relay = *parseRelayUrl("https://terminal.hardwario.com");
    EXPECT_EQ(engineIoUrl(relay).toString(),
              QStringLiteral("wss://terminal.hardwario.com/socket.io/?EIO=4&transport=websocket"));
    EXPECT_EQ(sessionLink(relay, *SessionCode::parse("482913")).toString(),
              QStringLiteral("https://terminal.hardwario.com/?session=482913"));
}

TEST(RelayUrl, DefaultAndConfigured) {
    EXPECT_EQ(configuredRelayUrl(nullptr).toString(),
              QStringLiteral("https://terminal.hardwario.com"));
}

TEST(JoinInput, CodeOrLink) {
    auto code = parseJoinInput(" 482 913 ");
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(code->code.value(), 482913U);
    EXPECT_FALSE(code->relay.has_value());

    auto link = parseJoinInput("https://terminal.hardwario.com/?session=482913");
    ASSERT_TRUE(link.has_value());
    EXPECT_EQ(link->code.value(), 482913U);
    ASSERT_TRUE(link->relay.has_value());
    EXPECT_TRUE(sameRelay(*link->relay, *parseRelayUrl("https://TERMINAL.hardwario.com")));
    EXPECT_FALSE(sameRelay(*link->relay, *parseRelayUrl("https://evil.example.com")));
}

TEST(JoinInput, Invalid) {
    EXPECT_EQ(parseJoinInput("https://terminal.hardwario.com/").error().code,
              ErrorCode::RemoteCodeInvalid);
    EXPECT_EQ(parseJoinInput("https://terminal.hardwario.com/?session=abc").error().code,
              ErrorCode::RemoteCodeInvalid);
    EXPECT_EQ(parseJoinInput("http://terminal.hardwario.com/?session=482913").error().code,
              ErrorCode::UsageInvalidArgument);
    EXPECT_EQ(parseJoinInput("abc").error().code, ErrorCode::RemoteCodeInvalid);
}

} // namespace
} // namespace hwio::remote
