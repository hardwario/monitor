#include "fake_relay.h"
#include "hwio/core/error.h"
#include "hwio/remote/relay_client.h"
#include "hwio/remote/session_code.h"
#include "remote_test_support.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace hwio::remote {
namespace {

using namespace std::chrono_literals;
using testing::FakeRelay;
using testing::RawParticipant;
using testing::runFor;
using testing::runUntil;

// Records the signals of one RelayClient.
struct Recorder {
    explicit Recorder(RelayClient& client) {
        QObject::connect(&client, &RelayClient::connected, [this](bool again) {
            ++connects;
            reconnected = again;
        });
        QObject::connect(&client, &RelayClient::connectionLost,
                         [this](const Error& error) { lost = error; });
        QObject::connect(&client, &RelayClient::failed,
                         [this](const Error& error) { failed = error; });
        QObject::connect(&client, &RelayClient::newClient, [this] { ++newClients; });
        QObject::connect(&client, &RelayClient::atReceived,
                         [this](const QJsonValue& payload) { at.append(payload); });
        QObject::connect(&client, &RelayClient::syncReceived,
                         [this](const QJsonValue& payload) { sync.append(payload); });
        QObject::connect(&client, &RelayClient::writeReceived,
                         [this](const QJsonValue& payload) { write.append(payload); });
        QObject::connect(&client, &RelayClient::warning,
                         [this](const Error& error) { warnings.append(error); });
    }
    int connects = 0;
    bool reconnected = false;
    std::optional<Error> lost;
    std::optional<Error> failed;
    int newClients = 0;
    QList<QJsonValue> at, sync, write;
    QList<Error> warnings;
};

RelayConfig configFor(const FakeRelay& relay, std::uint32_t code = 482913) {
    RelayConfig config;
    config.relay = relay.url();
    config.code = *SessionCode::fromValue(code);
    config.limits.connectTimeout = 3000ms;
    config.limits.reconnectFirstDelay = 50ms;
    config.limits.reconnectMaxDelay = 200ms;
    config.limits.reconnectBudget = 3000ms;
    return config;
}

QJsonObject atRead(const QString& line) {
    QJsonObject at;
    at.insert(QStringLiteral("event"), QStringLiteral("read"));
    at.insert(QStringLiteral("data"), line);
    return at;
}

TEST(RelayClient, NewClientGoesToEveryoneTheNewSocketIncluded) {
    FakeRelay relay;
    RelayClient host(configFor(relay));
    Recorder hostEvents(host);
    host.start();
    ASSERT_TRUE(runUntil([&] { return hostEvents.connects == 1 && hostEvents.newClients == 1; }));
    EXPECT_FALSE(hostEvents.reconnected);

    RelayClient joiner(configFor(relay));
    Recorder joinerEvents(joiner);
    joiner.start();
    ASSERT_TRUE(
        runUntil([&] { return joinerEvents.newClients == 1 && hostEvents.newClients == 2; }));
    EXPECT_EQ(relay.clientCount(QStringLiteral("/482913")), 2);
}

TEST(RelayClient, EventsGoToTheOthersOnlyAndOtherEventsAreDropped) {
    FakeRelay relay;
    RelayClient host(configFor(relay));
    Recorder hostEvents(host);
    host.start();
    ASSERT_TRUE(runUntil([&] { return hostEvents.connects == 1; }));
    RelayClient joiner(configFor(relay));
    Recorder joinerEvents(joiner);
    joiner.start();
    ASSERT_TRUE(runUntil([&] { return hostEvents.newClients == 2 && joinerEvents.connects == 1; }));

    host.emitEvent(QStringLiteral("at"), atRead(QStringLiteral("hello")), true);
    host.emitEvent(QStringLiteral("hwio:op"), QJsonObject{}); // dropped by the relay
    joiner.emitEvent(QStringLiteral("write"), QStringLiteral("info show"));
    ASSERT_TRUE(
        runUntil([&] { return joinerEvents.at.size() == 1 && hostEvents.write.size() == 1; }));
    EXPECT_EQ(joinerEvents.at.front().toObject().value(QStringLiteral("data")).toString(),
              QStringLiteral("hello"));
    EXPECT_EQ(hostEvents.write.front().toString(), QStringLiteral("info show"));
    runFor(100ms);
    EXPECT_TRUE(hostEvents.at.isEmpty()); // never back to the sender
    EXPECT_EQ(relay.droppedEvents(), QList<QString>{QStringLiteral("hwio:op")});
}

TEST(RelayClient, FirstConnectFailureIsFinal) {
    quint16 port = 0;
    {
        FakeRelay relay; // a free port that is closed again
        port = relay.port();
    }
    RelayConfig config;
    config.relay = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
    config.limits.connectTimeout = 2000ms;
    RelayClient client(config);
    Recorder events(client);
    client.start();
    ASSERT_TRUE(runUntil([&] { return events.failed.has_value(); }));
    EXPECT_EQ(events.failed.value_or(Error{}).code, ErrorCode::RemoteRelayUnreachable);
    EXPECT_EQ(events.connects, 0);
}

TEST(RelayClient, ReconnectsAfterALostConnection) {
    FakeRelay relay;
    RelayClient client(configFor(relay));
    Recorder events(client);
    client.start();
    ASSERT_TRUE(runUntil([&] { return events.connects == 1; }));
    relay.dropAll();
    ASSERT_TRUE(runUntil([&] { return events.connects == 2; }));
    EXPECT_TRUE(events.lost.has_value());
    EXPECT_TRUE(events.reconnected);
    EXPECT_EQ(events.newClients, 2); // its own newClient after each connect
}

TEST(RelayClient, PingTimeoutLosesTheConnectionAndItReconnects) {
    FakeRelay relay(FakeRelay::Options{.pingInterval = 100ms, .pingTimeout = 100ms});
    RelayClient client(configFor(relay));
    Recorder events(client);
    client.start();
    ASSERT_TRUE(runUntil([&] { return events.connects == 1; }));
    runFor(400ms); // pings keep it alive
    EXPECT_FALSE(events.lost.has_value());
    relay.setPingsPaused(true);
    ASSERT_TRUE(runUntil([&] { return events.lost.has_value(); }));
    EXPECT_EQ(events.lost.value_or(Error{}).code, ErrorCode::RemoteRelayUnreachable);
    relay.setPingsPaused(false);
    ASSERT_TRUE(runUntil([&] { return events.connects >= 2; }));
    EXPECT_FALSE(events.failed.has_value());
}

TEST(RelayClient, ReconnectBudgetEndsTheConnection) {
    auto relay = std::make_unique<FakeRelay>();
    RelayConfig config = configFor(*relay);
    config.limits.reconnectBudget = 500ms;
    RelayClient client(config);
    Recorder events(client);
    client.start();
    ASSERT_TRUE(runUntil([&] { return events.connects == 1; }));
    relay.reset(); // the relay is gone for good
    ASSERT_TRUE(runUntil([&] { return events.failed.has_value(); }));
    EXPECT_EQ(events.failed.value_or(Error{}).code, ErrorCode::RemoteRelayUnreachable);
    EXPECT_TRUE(events.lost.has_value());
}

TEST(RelayClient, OversizedAndMalformedEventsAreDroppedThenEndTheSession) {
    FakeRelay relay;
    RelayClient client(configFor(relay));
    Recorder events(client);
    client.start();
    ASSERT_TRUE(runUntil([&] { return events.connects == 1; }));
    const QString nsp = QStringLiteral("/482913");
    relay.sendRaw(nsp, QStringLiteral("42/482913,[\"at\",{\"event\":"));
    relay.sendBinary(nsp);
    ASSERT_TRUE(runUntil([&] { return events.warnings.size() == 2; }));
    EXPECT_EQ(events.warnings.front().code, ErrorCode::RemoteProtocolError);
    const QString big = QStringLiteral("42/482913,[\"at\",{\"event\":\"read\",\"data\":\"%1\"}]")
                            .arg(QString(qsizetype{70} * 1024, QLatin1Char('x')));
    relay.sendRaw(nsp, big);
    relay.sendRaw(nsp, big);
    ASSERT_TRUE(runUntil([&] { return events.warnings.size() == 4; }));
    EXPECT_FALSE(events.failed.has_value());
    EXPECT_TRUE(events.at.isEmpty());
    relay.sendRaw(nsp, big); // the third oversized event
    ASSERT_TRUE(runUntil([&] { return events.failed.has_value(); }));
    EXPECT_EQ(events.failed.value_or(Error{}).code, ErrorCode::RemoteProtocolError);
}

TEST(RelayClient, InvalidNamespaceIsRefusedByTheRelay) {
    FakeRelay relay;
    RawParticipant raw(relay.url(), QStringLiteral("/abc"));
    ASSERT_TRUE(runUntil([&] { return !raw.connectError.isEmpty(); }));
    EXPECT_EQ(raw.connectError, QStringLiteral("Invalid namespace"));
}

TEST(RelayClient, FullOutgoingQueueDropsTheOldestDeviceLines) {
    FakeRelay relay;
    RelayConfig config = configFor(relay);
    config.limits.outgoingQueueBytes = 400;
    RelayClient host(config);
    Recorder hostEvents(host);
    RawParticipant joiner(relay.url(), QStringLiteral("/482913"));
    ASSERT_TRUE(joiner.waitConnected());
    host.start(); // queued while the namespace connects
    host.emitEvent(QStringLiteral("at"),
                   QJsonObject{{QStringLiteral("event"), QStringLiteral("state")},
                               {QStringLiteral("data"), QStringLiteral("connected")}});
    for (int i = 0; i < 20; ++i) {
        host.emitEvent(QStringLiteral("at"), atRead(QStringLiteral("line %1").arg(i)), true);
    }
    ASSERT_TRUE(runUntil([&] { return hostEvents.connects == 1; }));
    ASSERT_TRUE(runUntil([&] {
        const auto at = joiner.named(QStringLiteral("at"));
        return !at.isEmpty() && at.back()
                                    .payload.toObject()
                                    .value(QStringLiteral("data"))
                                    .toString()
                                    .contains(QStringLiteral("dropped"));
    }));
    const auto at = joiner.named(QStringLiteral("at"));
    EXPECT_EQ(at.front().payload.toObject().value(QStringLiteral("event")).toString(),
              QStringLiteral("state")); // not droppable
    EXPECT_LT(at.size(), 22);
    EXPECT_FALSE(hostEvents.warnings.isEmpty());
}

TEST(RelayClient, StopDisconnectsAndEmitsNothingMore) {
    FakeRelay relay;
    auto client = std::make_unique<RelayClient>(configFor(relay));
    Recorder events(*client);
    client->start();
    ASSERT_TRUE(runUntil([&] { return events.connects == 1; }));
    client->stop();
    ASSERT_TRUE(runUntil([&] { return relay.clientCount(QStringLiteral("/482913")) == 0; }));
    client.reset();
}

} // namespace
} // namespace hwio::remote
