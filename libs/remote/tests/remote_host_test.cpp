// RemoteHost against FakeRelay with raw Socket.IO participants that behave like the web Terminal
// (plan 0044 § Tests: join approval, collision, sync format, limits, audit).

#include "fake_console.h"
#include "fake_relay.h"
#include "hwio/core/error.h"
#include "hwio/device/command_tree.h"
#include "hwio/device/console_line.h"
#include "hwio/device/session_events.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/remote_host.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "remote_test_support.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <expected>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hwio::remote {
namespace {

using namespace std::chrono_literals;
using testing::FakeConsole;
using testing::FakeOperations;
using testing::FakeRelay;
using testing::RawParticipant;
using testing::runFor;
using testing::runUntil;
using testing::ScriptedConsent;

const QString nsp = QStringLiteral("/482913");

HostOptions optionsFor(const FakeRelay& relay) {
    HostOptions options;
    options.relay = relay.url();
    options.code = *SessionCode::fromValue(482913);
    options.appVersion = "2.1.0-test";
    options.limits.collisionWindow = 150ms;
    options.limits.relay.reconnectFirstDelay = 50ms;
    options.limits.relay.reconnectMaxDelay = 100ms;
    return options;
}

QJsonObject at(const QString& event, const QJsonValue& data) {
    QJsonObject object;
    object.insert(QStringLiteral("event"), event);
    object.insert(QStringLiteral("data"), data);
    return object;
}

QList<QJsonObject> atEvents(const RawParticipant& p, const QString& event) {
    QList<QJsonObject> out;
    for (const auto& r : p.named(QStringLiteral("at"))) {
        if (r.payload.toObject().value(QStringLiteral("event")).toString() == event) {
            out.append(r.payload.toObject());
        }
    }
    return out;
}

struct HostFixture {
    explicit HostFixture(HostOptions options, HostOperations* operations = nullptr)
        : host(console, consent, operations, std::move(options)) {
        QObject::connect(&host, &RemoteHost::sharingStarted, [this] { ++shared; });
        QObject::connect(&host, &RemoteHost::ended, [this] { ended = true; });
    }
    FakeConsole console;
    ScriptedConsent consent;
    RemoteHost host;
    int shared = 0;
    bool ended = false;
};

TEST(RemoteHost, SharesAfterTheCollisionWindowAndStreamsTheConsole) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay));
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    EXPECT_EQ(f.host.state(), HostState::Sharing);
    EXPECT_EQ(f.host.link().toString(),
              relay.url().toString() + QStringLiteral("/?session=482913"));
    EXPECT_TRUE(f.console.hasEvent("Sharing started, session 482 913"));

    RawParticipant web(relay.url(), nsp); // a web Terminal joiner
    ASSERT_TRUE(web.waitConnected());
    f.console.shell("hello");
    f.console.logLine("[00:00:01.000,000] <inf> app: tick");
    ASSERT_TRUE(runUntil([&] { return atEvents(web, QStringLiteral("read")).size() == 2; }));
    EXPECT_EQ(
        atEvents(web, QStringLiteral("read")).front().value(QStringLiteral("data")).toString(),
        QStringLiteral("hello"));
    f.console.setState(device::ConnectionState::Disconnected);
    // A "connected" state for the join may come first.
    ASSERT_TRUE(runUntil([&] {
        const auto states = atEvents(web, QStringLiteral("state"));
        return !states.isEmpty() && states.back().value(QStringLiteral("data")).toString() ==
                                        QStringLiteral("disconnected");
    }));
    // Audit markers are not sent.
    for (const auto& r : web.named(QStringLiteral("at"))) {
        EXPECT_FALSE(r.payload.toObject()
                         .value(QStringLiteral("data"))
                         .toString()
                         .contains(QStringLiteral("Remote:")));
    }
}

TEST(RemoteHost, JoinApprovalGatesCommandsAndSync) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay));
    f.consent.joins = ScriptedConsent::Mode::Manual;
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    EXPECT_EQ(f.consent.joinQuestions, 0); // the host's own newClient is ignored
    f.console.shell("before join");

    RawParticipant web(relay.url(), nsp);
    ASSERT_TRUE(web.waitConnected());
    ASSERT_TRUE(runUntil([&] { return f.consent.joinQuestions == 1; }));
    EXPECT_TRUE(f.host.joinQuestionOpen());
    web.emitEvent(QStringLiteral("write"), QStringLiteral("info show"));
    runFor(200ms);
    EXPECT_TRUE(f.console.sent.empty()); // dropped while pending, never queued
    EXPECT_TRUE(web.named(QStringLiteral("sync")).isEmpty());

    std::exchange(f.consent.pendingJoin, {})(true);
    ASSERT_TRUE(runUntil([&] { return !web.named(QStringLiteral("sync")).isEmpty(); }));
    EXPECT_TRUE(f.console.hasEvent("were dropped while waiting for your answer"));
    EXPECT_TRUE(f.console.hasEvent("Participant allowed"));
    // The sync in the web Terminal's format.
    const QJsonObject sync = web.named(QStringLiteral("sync")).front().payload.toObject();
    EXPECT_EQ(sync.value(QStringLiteral("state")).toString(), QStringLiteral("connected"));
    const QJsonArray commands = sync.value(QStringLiteral("commands")).toArray();
    ASSERT_GE(commands.size(), 1);
    const QJsonObject first = commands.at(0).toObject();
    EXPECT_EQ(first.value(QStringLiteral("command")).toString(), QStringLiteral("(device output)"));
    EXPECT_TRUE(first.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("system-")));
    EXPECT_EQ(first.value(QStringLiteral("index")).toInt(), 0);
    EXPECT_EQ(first.value(QStringLiteral("response"))
                  .toObject()
                  .value(QStringLiteral("result"))
                  .toInt(-1),
              0);
    EXPECT_EQ(first.value(QStringLiteral("response"))
                  .toObject()
                  .value(QStringLiteral("outputs"))
                  .toArray()
                  .at(0)
                  .toString(),
              QStringLiteral("before join"));
    // The hello follows the sync (the web Terminal ignores it).
    ASSERT_TRUE(runUntil([&] { return !atEvents(web, QStringLiteral("hwio")).isEmpty(); }));

    web.emitEvent(QStringLiteral("write"), QStringLiteral("info show"));
    ASSERT_TRUE(runUntil([&] { return f.console.sent.size() == 1; }));
    EXPECT_EQ(f.console.sent.front().line, "info show");
    EXPECT_EQ(f.console.sent.front().source, device::LineSource::Remote);
    EXPECT_TRUE(f.console.hasEvent("command info show"));
    // The echo goes back as at/write.
    ASSERT_TRUE(runUntil([&] { return !atEvents(web, QStringLiteral("write")).isEmpty(); }));
}

TEST(RemoteHost, EndSessionAnswerOrTimeoutEndsTheSession) {
    FakeRelay relay;
    HostOptions options = optionsFor(relay);
    options.limits.joinApprovalTimeout = 300ms;
    HostFixture f(options);
    f.consent.joins = ScriptedConsent::Mode::Manual;
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant web(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.consent.joinQuestions == 1; }));
    ASSERT_TRUE(runUntil([&] { return f.ended; }));
    ASSERT_TRUE(f.host.endError().has_value());
    EXPECT_EQ(f.host.endError().value_or(Error{}).code, ErrorCode::RemoteJoinNotAllowed);
    EXPECT_EQ(f.consent.withdrawn, 1); // the timeout withdraws the question
    EXPECT_TRUE(runUntil([&] { return relay.clientCount(nsp) == 1; }));
}

TEST(RemoteHost, DeniedJoinEndsTheSession) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay));
    f.consent.joins = ScriptedConsent::Mode::Deny;
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant web(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.ended; }));
    EXPECT_EQ(f.host.endError().value_or(Error{}).code, ErrorCode::RemoteJoinNotAllowed);
    EXPECT_TRUE(web.named(QStringLiteral("sync")).isEmpty());
}

TEST(RemoteHost, EveryLaterJoinAsksAgain) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay));
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant first(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.consent.joinQuestions == 1 && f.host.commandsAllowed(); }));
    f.consent.joins = ScriptedConsent::Mode::Manual;
    RawParticipant second(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.consent.joinQuestions == 2; }));
    EXPECT_FALSE(f.host.commandsAllowed()); // paused for everyone while the question is open
    first.emitEvent(QStringLiteral("write"), QStringLiteral("reboot"));
    runFor(150ms);
    EXPECT_TRUE(f.console.sent.empty());
}

TEST(RemoteHost, CollisionInTheWindowPicksANewCode) {
    FakeRelay relay;
    RawParticipant otherHost(relay.url(), nsp);
    ASSERT_TRUE(otherHost.waitConnected());
    HostFixture f(optionsFor(relay));
    // The other host answers every newClient with a sync (the web host's behaviour).
    QObject::connect(&relay, &FakeRelay::namespaceConnected, &relay, [&](const QString& n) {
        if (n == nsp) {
            QJsonObject sync;
            sync.insert(QStringLiteral("state"), QStringLiteral("connected"));
            sync.insert(QStringLiteral("commands"), QJsonArray());
            otherHost.emitEvent(QStringLiteral("sync"), sync);
        }
    });
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    EXPECT_NE(f.host.code().value(), 482913U);
}

TEST(RemoteHost, CollisionLaterEndsTheSession) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay));
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant other(relay.url(), nsp);
    ASSERT_TRUE(other.waitConnected());
    other.emitEvent(QStringLiteral("at"), at(QStringLiteral("read"), QStringLiteral("x")));
    ASSERT_TRUE(runUntil([&] { return f.ended; }));
    EXPECT_EQ(f.host.endError().value_or(Error{}).code, ErrorCode::RemoteCodeCollision);
    EXPECT_TRUE(f.console.hasEvent("Another device uses this session code"));
}

TEST(RemoteHost, RemoteLinesAreBoundedAndRateLimited) {
    FakeRelay relay;
    HostOptions options = optionsFor(relay);
    options.limits.remoteLinesPerSecond = 3;
    HostFixture f(options);
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant web(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.host.commandsAllowed(); }));
    web.emitEvent(QStringLiteral("write"), QString(600, QLatin1Char('x')));
    web.emitEvent(QStringLiteral("write"), QStringLiteral("a\nb"));
    for (int i = 0; i < 6; ++i) {
        web.emitEvent(QStringLiteral("write"), QStringLiteral("cmd %1").arg(i));
    }
    runFor(300ms);
    EXPECT_EQ(f.console.sent.size(), 3U);
    EXPECT_TRUE(f.console.hasEvent("too long or with control characters"));
    EXPECT_TRUE(f.console.hasEvent("more than 3 per second"));
}

TEST(RemoteHost, SecretsAreRedactedTowardsTheRelay) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay));
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant web(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.host.commandsAllowed(); }));
    f.console.shell("claim token: 1234secret");
    ASSERT_TRUE(runUntil([&] { return !atEvents(web, QStringLiteral("read")).isEmpty(); }));
    EXPECT_EQ(atEvents(web, QStringLiteral("read")).back().value(QStringLiteral("data")).toString(),
              QStringLiteral("claim token: <redacted>"));
}

TEST(RemoteHost, IdleWarningKeepSharingAndTimeout) {
    FakeRelay relay;
    HostOptions options = optionsFor(relay);
    options.limits.idleTimeout = 600ms;
    options.limits.idleWarningBefore = 300ms;
    HostFixture f(options);
    int warnings = 0;
    QObject::connect(&f.host, &RemoteHost::idleWarning, [&](std::chrono::seconds) { ++warnings; });
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    ASSERT_TRUE(runUntil([&] { return warnings == 1; }));
    f.host.keepSharing();
    runFor(400ms);
    EXPECT_FALSE(f.ended);
    ASSERT_TRUE(runUntil([&] { return f.ended; }));
    EXPECT_EQ(f.host.endError().value_or(Error{}).code, ErrorCode::Timeout);
    EXPECT_TRUE(f.console.hasEvent("30 minutes without activity"));
}

TEST(RemoteHost, MaxLifetimeEndsTheSession) {
    FakeRelay relay;
    HostOptions options = optionsFor(relay);
    options.limits.maxLifetime = 400ms;
    HostFixture f(options);
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    for (int i = 0; i < 10 && !f.ended; ++i) {
        f.console.shell("busy");
        runFor(60ms);
    }
    ASSERT_TRUE(runUntil([&] { return f.ended; }));
    EXPECT_EQ(f.host.endError().value_or(Error{}).code, ErrorCode::Timeout);
}

TEST(RemoteHost, HostReconnectNeedsApprovalAgain) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay));
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    {
        RawParticipant web(relay.url(), nsp);
        ASSERT_TRUE(runUntil([&] { return f.host.commandsAllowed(); }));
    }
    f.consent.joins = ScriptedConsent::Mode::Manual;
    relay.dropAll();
    ASSERT_TRUE(runUntil([&] { return f.consent.joinQuestions == 2; }));
    EXPECT_FALSE(f.host.commandsAllowed());
}

TEST(RemoteHost, EndSharingAndClosedConsole) {
    FakeRelay relay;
    auto console = std::make_unique<FakeConsole>();
    ScriptedConsent consent;
    RemoteHost host(*console, consent, nullptr, optionsFor(relay));
    bool ended = false;
    QObject::connect(&host, &RemoteHost::ended, [&] { ended = true; });
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));
    console.reset(); // the tab closed
    ASSERT_TRUE(ended);
    EXPECT_EQ(host.endError().value_or(Error{}).code, ErrorCode::RemoteHostEnded);
    EXPECT_TRUE(runUntil([&] { return relay.clientCount(nsp) == 0; }));
}

TEST(RemoteHost, WebTerminalJoinerHelloTimeoutIsAudited) {
    FakeRelay relay;
    HostOptions options = optionsFor(relay);
    options.limits.helloWait = 100ms;
    HostFixture f(options);
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant web(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.console.hasEvent("web Terminal or unknown"); }));
}

// ---- operations (ADR-0336) ----

struct OpsFixture {
    explicit OpsFixture(const FakeRelay& relay)
        : f(optionsFor(relay), &operations), joiner(relay.url(), nsp) {}
    FakeOperations operations;
    HostFixture f;
    RawParticipant joiner;

    void requestOp(const tunnel::OpRequest& request) {
        joiner.emitEvent(QStringLiteral("write"), tunnel::encodeJoiner(request));
    }
    [[nodiscard]] QList<tunnel::HostMessage> messages() const {
        QList<tunnel::HostMessage> out;
        for (const auto& r : joiner.named(QStringLiteral("at"))) {
            if (tunnel::isTunnelAt(r.payload)) {
                if (auto m = tunnel::decodeHost(r.payload)) {
                    out.append(*m);
                }
            }
        }
        return out;
    }
    template <class T> [[nodiscard]] std::optional<T> last() const {
        std::optional<T> found;
        for (const auto& m : messages()) {
            if (const auto* t = std::get_if<T>(&m)) {
                found = *t;
            }
        }
        return found;
    }
};

const std::string fwId = "0123456789abcdef0123456789abcdef";

TEST(RemoteHostOperations, FlashAllowedRunsAndReportsTheResult) {
    FakeRelay relay;
    OpsFixture o(relay);
    o.f.host.start();
    // The joiner connected before the host: it is not announced; join again.
    ASSERT_TRUE(runUntil([&] { return o.f.shared == 1; }));
    RawParticipant monitor(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return o.f.host.commandsAllowed(); }));
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::HostHello>().has_value(); }));
    EXPECT_TRUE(o.last<tunnel::HostHello>().value_or(tunnel::HostHello{}).ops);

    o.requestOp(tunnel::OpRequest{.id = "op1",
                                  .kind = tunnel::OpKind::Flash,
                                  .target = tunnel::OpTarget::App,
                                  .firmwareId = fwId});
    ASSERT_TRUE(runUntil([&] { return o.operations.started.size() == 1; }));
    ASSERT_EQ(o.f.consent.operationQuestions.size(), 1U);
    EXPECT_EQ(o.f.consent.operationQuestions.front().firmwareName, "CHESTER-Clime");
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::OpAccepted>().has_value(); }));
    o.operations.progress(OperationUpdate{.phase = "programming", .done = 10, .total = 100});
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::OpProgress>().has_value(); }));
    o.operations.finished(Status{});
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::OpResult>().has_value(); }));
    EXPECT_TRUE(o.last<tunnel::OpResult>().value_or(tunnel::OpResult{}).ok);
    EXPECT_TRUE(o.f.console.hasEvent("allowed by you"));
    EXPECT_TRUE(o.f.console.hasEvent(": done."));
}

TEST(RemoteHostOperations, DeniedTimeoutNotInCatalogBusyAndInvalid) {
    FakeRelay relay;
    OpsFixture o(relay);
    o.f.host.start();
    ASSERT_TRUE(runUntil([&] { return o.f.shared == 1; }));
    RawParticipant monitor(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return o.f.host.commandsAllowed(); }));

    o.f.consent.operations = ScriptedConsent::Mode::Deny;
    o.requestOp(tunnel::OpRequest{.id = "a",
                                  .kind = tunnel::OpKind::Reset,
                                  .target = tunnel::OpTarget::App,
                                  .firmwareId = std::nullopt});
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::OpDenied>().has_value(); }));
    EXPECT_EQ(o.last<tunnel::OpDenied>().value_or(tunnel::OpDenied{}).code,
              "remote.operation_denied");

    o.operations.catalogKnows = false;
    o.requestOp(tunnel::OpRequest{.id = "b",
                                  .kind = tunnel::OpKind::Flash,
                                  .target = tunnel::OpTarget::App,
                                  .firmwareId = fwId});
    ASSERT_TRUE(runUntil(
        [&] { return o.last<tunnel::OpDenied>().value_or(tunnel::OpDenied{}).id == "b"; }));
    EXPECT_EQ(o.last<tunnel::OpDenied>().value_or(tunnel::OpDenied{}).code,
              "remote.firmware_not_in_catalog");

    o.requestOp(tunnel::OpRequest{.id = "c",
                                  .kind = tunnel::OpKind::Flash,
                                  .target = tunnel::OpTarget::Lte,
                                  .firmwareId = fwId});
    ASSERT_TRUE(runUntil(
        [&] { return o.last<tunnel::OpDenied>().value_or(tunnel::OpDenied{}).id == "c"; }));

    o.f.consent.operations = ScriptedConsent::Mode::Manual;
    o.requestOp(tunnel::OpRequest{.id = "d",
                                  .kind = tunnel::OpKind::Erase,
                                  .target = tunnel::OpTarget::App,
                                  .firmwareId = std::nullopt});
    ASSERT_TRUE(runUntil([&] { return static_cast<bool>(o.f.consent.pendingOperation); }));
    o.requestOp(tunnel::OpRequest{.id = "e",
                                  .kind = tunnel::OpKind::Reset,
                                  .target = tunnel::OpTarget::App,
                                  .firmwareId = std::nullopt});
    ASSERT_TRUE(runUntil(
        [&] { return o.last<tunnel::OpDenied>().value_or(tunnel::OpDenied{}).id == "e"; }));
    EXPECT_EQ(o.last<tunnel::OpDenied>().value_or(tunnel::OpDenied{}).code, "internal.busy");
    std::exchange(o.f.consent.pendingOperation, {})(true);
    ASSERT_TRUE(runUntil([&] { return o.operations.started.size() == 1; }));
    EXPECT_EQ(o.operations.started.front().id, "d");
    o.operations.finished(std::unexpected(makeError(ErrorCode::TargetConnectFailed)));
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::OpResult>().has_value(); }));
    EXPECT_FALSE(o.last<tunnel::OpResult>().value_or(tunnel::OpResult{}).ok);
    EXPECT_EQ(o.last<tunnel::OpResult>().value_or(tunnel::OpResult{}).code,
              "target.connect_failed");
}

TEST(RemoteHostOperations, OperationQuestionTimesOut) {
    FakeRelay relay;
    HostOptions options = optionsFor(relay);
    options.limits.operationTimeout = 200ms;
    FakeOperations operations;
    HostFixture f(options, &operations);
    f.consent.operations = ScriptedConsent::Mode::Manual;
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant monitor(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.host.commandsAllowed(); }));
    monitor.emitEvent(QStringLiteral("write"),
                      tunnel::encodeJoiner(tunnel::OpRequest{.id = "t",
                                                             .kind = tunnel::OpKind::Recover,
                                                             .target = tunnel::OpTarget::Lte,
                                                             .firmwareId = std::nullopt}));
    ASSERT_TRUE(runUntil([&] { return f.consent.withdrawn == 1; }));
    EXPECT_TRUE(operations.started.empty());
    EXPECT_TRUE(f.console.hasEvent("no answer within 60 s"));
}

TEST(RemoteHostOperations, NoOperationsWithoutHostOperationsOrBeforeApproval) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay)); // BLE-like: no operations
    f.consent.joins = ScriptedConsent::Mode::Manual;
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant monitor(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return f.consent.joinQuestions == 1; }));
    const QJsonObject request =
        tunnel::encodeJoiner(tunnel::OpRequest{.id = "x",
                                               .kind = tunnel::OpKind::Reset,
                                               .target = tunnel::OpTarget::App,
                                               .firmwareId = std::nullopt});
    monitor.emitEvent(QStringLiteral("write"), request); // before approval
    ASSERT_TRUE(runUntil([&] { return !atEvents(monitor, QStringLiteral("hwio")).isEmpty(); }));
    std::exchange(f.consent.pendingJoin, {})(true);
    ASSERT_TRUE(runUntil([&] { return f.host.commandsAllowed(); }));
    monitor.emitEvent(QStringLiteral("write"), request);
    ASSERT_TRUE(runUntil([&] {
        return std::ranges::any_of(atEvents(monitor, QStringLiteral("hwio")), [](const auto& a) {
            return a.value(QStringLiteral("data"))
                       .toObject()
                       .value(QStringLiteral("code"))
                       .toString() == QStringLiteral("remote.operation_unsupported_peer");
        });
    }));
    EXPECT_TRUE(f.console.sent.empty());
}

// ADR-0363: the guided LTE update - opKinds in the hello, the question names the recommended
// firmware, phases go to the joiner (a new phase at once), the outcome passes through its code.
TEST(RemoteHostOperations, LteUpdateAskedWithTheRecommendedFirmware) {
    FakeRelay relay;
    OpsFixture o(relay);
    o.f.host.start();
    ASSERT_TRUE(runUntil([&] { return o.f.shared == 1; }));
    RawParticipant monitor(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return o.f.host.commandsAllowed(); }));
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::HostHello>().has_value(); }));
    EXPECT_TRUE(tunnel::supports(o.last<tunnel::HostHello>().value_or(tunnel::HostHello{}),
                                 tunnel::OpKind::LteUpdate));

    o.requestOp(tunnel::OpRequest{.id = "lte1",
                                  .kind = tunnel::OpKind::LteUpdate,
                                  .target = tunnel::OpTarget::Lte,
                                  .firmwareId = std::nullopt});
    ASSERT_TRUE(runUntil([&] { return o.operations.started.size() == 1; }));
    ASSERT_EQ(o.f.consent.operationQuestions.size(), 1U);
    const QString question = operationQuestionText(o.f.consent.operationQuestions.front());
    EXPECT_TRUE(question.contains(QStringLiteral("HARDWARIO LTE firmware 1.7.0")))
        << question.toStdString();
    EXPECT_TRUE(question.contains(QStringLiteral("move the J-Link cable")))
        << question.toStdString();
    o.operations.progress(OperationUpdate{.phase = "connect-app"});
    o.operations.progress(OperationUpdate{.phase = "connect-lte"}); // within 250 ms: sent anyway
    ASSERT_TRUE(runUntil([&] {
        return o.last<tunnel::OpProgress>().value_or(tunnel::OpProgress{}).phase == "connect-lte";
    }));
    o.operations.finished(
        std::unexpected(makeError(ErrorCode::RemoteApplicationNotRestored, "test")));
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::OpResult>().has_value(); }));
    EXPECT_EQ(o.last<tunnel::OpResult>().value_or(tunnel::OpResult{}).code,
              "remote.application_not_restored");
    EXPECT_TRUE(o.f.console.hasEvent("Update the LTE firmware to HARDWARIO LTE firmware 1.7.0"));
}

TEST(RemoteHostOperations, LteUpdateRefusedWhenTheHostHasNone) {
    FakeRelay relay;
    OpsFixture o(relay);
    o.operations.lteUpdate = false;
    o.f.host.start();
    ASSERT_TRUE(runUntil([&] { return o.f.shared == 1; }));
    RawParticipant monitor(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return o.f.host.commandsAllowed(); }));
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::HostHello>().has_value(); }));
    EXPECT_FALSE(tunnel::supports(o.last<tunnel::HostHello>().value_or(tunnel::HostHello{}),
                                  tunnel::OpKind::LteUpdate));
    o.requestOp(tunnel::OpRequest{.id = "lte2",
                                  .kind = tunnel::OpKind::LteUpdate,
                                  .target = tunnel::OpTarget::Lte,
                                  .firmwareId = std::nullopt});
    ASSERT_TRUE(runUntil([&] { return o.last<tunnel::OpDenied>().has_value(); }));
    EXPECT_EQ(o.last<tunnel::OpDenied>().value_or(tunnel::OpDenied{}).code,
              "remote.operation_unsupported_peer");
    EXPECT_TRUE(o.f.consent.operationQuestions.empty());
    EXPECT_TRUE(o.operations.started.empty());
}

// ADR-0364: the command tree goes to an allowed participant (not before), learn requests for known
// commands pass rate-limited, others are dropped, and their count is audited at the end.
std::shared_ptr<const device::CommandTree> treeOf(std::initializer_list<const char*> names) {
    device::CommandTree tree;
    std::vector<device::CommandEntry> entries;
    for (const char* name : names) {
        entries.push_back(device::CommandEntry{.name = name, .help = "help"});
    }
    EXPECT_TRUE(tree.setChildren({}, entries));
    return std::make_shared<const device::CommandTree>(std::move(tree));
}

QList<tunnel::HostCommands> commandsOf(const RawParticipant& p) {
    QList<tunnel::HostCommands> out;
    for (const auto& r : p.named(QStringLiteral("at"))) {
        if (auto m = tunnel::decodeHost(r.payload)) {
            if (const auto* c = std::get_if<tunnel::HostCommands>(&*m)) {
                out.append(*c);
            }
        }
    }
    return out;
}

TEST(RemoteHostCommands, TreeAfterAllowAndLearnLimits) {
    FakeRelay relay;
    HostOptions options = optionsFor(relay);
    options.limits.learnPerSession = 3;
    options.limits.commandsDelay = 10ms;
    HostFixture f(options);
    f.consent.joins = ScriptedConsent::Mode::Manual;
    QStringList learned;
    QObject::connect(&f.host, &RemoteHost::learnRequested,
                     [&](const QStringList& path) { learned.append(path.join(u' ')); });
    f.host.setCommandTree(treeOf({"app", "lte", "info"}));
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant monitor(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return static_cast<bool>(f.consent.pendingJoin); }));
    const auto learn = [&](std::vector<std::string> path) {
        monitor.emitEvent(QStringLiteral("write"),
                          tunnel::encodeJoiner(tunnel::Learn{.path = std::move(path)}));
    };
    learn({"app"}); // before the approval: dropped
    runFor(200ms);
    EXPECT_TRUE(commandsOf(monitor).isEmpty());
    std::exchange(f.consent.pendingJoin, {})(true);
    ASSERT_TRUE(runUntil([&] { return commandsOf(monitor).size() == 1; }));
    EXPECT_EQ(commandsOf(monitor).front().tree->nodeCount(), 3U);
    EXPECT_TRUE(learned.isEmpty());

    learn({"nope"}); // unknown command
    learn({"app"});
    learn({"lte"});
    learn({"info"}); // the third within a second: dropped
    ASSERT_TRUE(runUntil([&] { return learned.size() == 2; }));
    runFor(1100ms);
    learn({"info"});
    learn({"app"}); // beyond the session limit (3)
    ASSERT_TRUE(runUntil([&] { return learned.size() == 3; }));
    runFor(200ms);
    EXPECT_EQ(learned,
              (QStringList{QStringLiteral("app"), QStringLiteral("lte"), QStringLiteral("info")}));
    EXPECT_FALSE(f.console.hasEvent("command app")); // not audited as remote commands

    // A grown tree is sent again; the same tree is not.
    f.host.setCommandTree(treeOf({"app", "lte", "info", "time"}));
    ASSERT_TRUE(runUntil([&] { return commandsOf(monitor).size() == 2; }));
    f.host.end();
    EXPECT_TRUE(f.console.hasEvent("Help of 3 command(s) read for support's autocomplete."));
}

// ADR-0364: a tree too large for a joiner's event limit (64 KiB) goes without its help texts.
TEST(RemoteHostCommands, LargeTreeWithoutHelpTexts) {
    FakeRelay relay;
    HostFixture f(optionsFor(relay));
    device::CommandTree tree;
    std::vector<device::CommandEntry> entries;
    for (int i = 0; i < 400; ++i) {
        entries.push_back(device::CommandEntry{.name = std::format("command{}", i),
                                               .help = std::string(250, 'h')});
    }
    ASSERT_TRUE(tree.setChildren({}, entries));
    f.host.setCommandTree(std::make_shared<const device::CommandTree>(std::move(tree)));
    f.host.start();
    ASSERT_TRUE(runUntil([&] { return f.shared == 1; }));
    RawParticipant monitor(relay.url(), nsp);
    ASSERT_TRUE(runUntil([&] { return commandsOf(monitor).size() == 1; }));
    const auto received = commandsOf(monitor).front().tree;
    EXPECT_EQ(received->nodeCount(), 400U);
    const std::vector<std::string> first{"command0"};
    EXPECT_EQ(received->find(first)->help, "");
}

} // namespace
} // namespace hwio::remote
