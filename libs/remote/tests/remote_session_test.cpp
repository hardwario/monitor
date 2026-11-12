// Monitor host ↔ Monitor joiner end to end over FakeRelay, and the joiner with a recorded web
// Terminal host (the event script of ~/terminal/src/components/console/console.tsx).

#include "fake_console.h"
#include "fake_relay.h"
#include "hwio/core/error.h"
#include "hwio/core/firmware_identity.h"
#include "hwio/device/command_tree.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/session_events.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/remote_host.h"
#include "hwio/remote/remote_operation_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "remote_test_support.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtContainerFwd>
#include <QtTypes>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
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

JoinOptions joinFor(const FakeRelay& relay, std::uint32_t code = 482913) {
    JoinOptions options;
    options.relay = relay.url();
    options.code = *SessionCode::fromValue(code);
    options.appVersion = "2.1.0-support";
    return options;
}

HostOptions hostFor(const FakeRelay& relay) {
    HostOptions options;
    options.relay = relay.url();
    options.code = *SessionCode::fromValue(482913);
    options.appVersion = "2.1.0-customer";
    options.limits.collisionWindow = 100ms;
    return options;
}

std::vector<std::string> texts(const device::ConsoleLog& log, device::Direction dir) {
    std::vector<std::string> out;
    for (const device::ConsoleLine& line : log.linesSince(0)) {
        if (line.dir == dir) {
            out.push_back(line.text);
        }
    }
    return out;
}

TEST(RemoteSession, MonitorToMonitorConsole) {
    FakeRelay relay;
    FakeConsole console;
    console.adopt(FirmwareIdentity{
        .name = "CHESTER Clime", .version = "v3.2.1", .deviceSerial = "2159018247"});
    ScriptedConsent consent;
    consent.joins = ScriptedConsent::Mode::Manual;
    RemoteHost host(console, consent, nullptr, hostFor(relay));
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));
    console.shell("boot line");

    RemoteConsoleSession joiner(joinFor(relay));
    EXPECT_EQ(joiner.identity().transport, device::Transport::Remote);
    ASSERT_TRUE(runUntil([&] { return static_cast<bool>(consent.pendingJoin); }));
    ASSERT_TRUE(runUntil([&] { return joiner.joinState() == JoinState::Waiting; }));
    EXPECT_EQ(joiner.title(), QStringLiteral("Session 482 913"));
    console.shell("live before approval"); // the relay broadcasts it anyway (ADR-0335)
    ASSERT_TRUE(runUntil([&] { return !texts(joiner.log(), device::Direction::Out).empty(); }));

    bool replaced = false;
    QObject::connect(&joiner, &RemoteConsoleSession::transcriptReplaced, [&] { replaced = true; });
    std::exchange(consent.pendingJoin, {})(true);
    ASSERT_TRUE(
        runUntil([&] { return joiner.joinState() == JoinState::Allowed && joiner.hostHello(); }));
    EXPECT_TRUE(replaced);
    EXPECT_EQ(joiner.title(), QStringLiteral("CHESTER 2159018247 — remote"));
    EXPECT_TRUE(joiner.hostHello().has_value() && !joiner.hostHello()->ops);
    EXPECT_FALSE(joiner.capabilities().testFlag(device::Capability::Operations));
    const auto out = texts(joiner.log(), device::Direction::Out);
    ASSERT_GE(out.size(), 2U);
    EXPECT_EQ(out.front(), "boot line");
    EXPECT_EQ(joiner.connection().state, device::ConnectionState::Connected);
    // The joiner's hello reaches the host.
    ASSERT_TRUE(runUntil([&] { return host.peerApp() == "HARDWARIO Monitor 2.1.0-support"; }));

    ASSERT_TRUE(joiner.sendLine("info show").has_value());
    ASSERT_TRUE(runUntil([&] { return console.sent.size() == 1; }));
    ASSERT_TRUE(runUntil([&] {
        const auto in = texts(joiner.log(), device::Direction::In);
        return !in.empty() && in.back() == "info show";
    }));
    console.logLine("[00:00:02.000,000] <wrn> app: low battery");
    ASSERT_TRUE(runUntil([&] {
        return std::ranges::any_of(joiner.log().linesSince(0), [](const device::ConsoleLine& line) {
            return line.channel == device::Channel::Log && line.level == device::LogLevel::Warning;
        });
    }));
    EXPECT_FALSE(joiner.sendLine(std::string(600, 'x')).has_value());
    EXPECT_FALSE(joiner.sendLine("a\rb").has_value());
}

TEST(RemoteSession, MonitorToMonitorOperation) {
    FakeRelay relay;
    FakeConsole console;
    ScriptedConsent consent;
    FakeOperations operations;
    RemoteHost host(console, consent, &operations, hostFor(relay));
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));
    RemoteConsoleSession joiner(joinFor(relay));
    ASSERT_TRUE(runUntil([&] { return joiner.hostHello().has_value(); }));
    EXPECT_TRUE(joiner.capabilities().testFlag(device::Capability::Operations));
    // The host's hello comes before the joiner's hello is processed: wait for it to be known.
    ASSERT_TRUE(runUntil([&] { return !host.peerApp().empty(); }));

    RemoteOperationClient client(joiner);
    bool accepted = false;
    std::optional<bool> ok;
    Error error;
    std::vector<tunnel::OpProgress> progress;
    QObject::connect(&client, &RemoteOperationClient::accepted, [&] { accepted = true; });
    QObject::connect(&client, &RemoteOperationClient::progress,
                     [&](const tunnel::OpProgress& p) { progress.push_back(p); });
    QObject::connect(&client, &RemoteOperationClient::finished, [&](bool success, const Error& e) {
        ok = success;
        error = e;
    });
    EXPECT_FALSE(client.request(tunnel::OpKind::Flash, tunnel::OpTarget::App).has_value());
    auto id = client.request(tunnel::OpKind::Flash, tunnel::OpTarget::App,
                             "0123456789abcdef0123456789abcdef");
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(client.request(tunnel::OpKind::Reset, tunnel::OpTarget::App).error().code,
              ErrorCode::InternalBusy);
    ASSERT_TRUE(runUntil([&] { return accepted && operations.started.size() == 1; }));
    operations.progress(OperationUpdate{.phase = "programming", .done = 5, .total = 10});
    ASSERT_TRUE(runUntil([&] { return !progress.empty(); }));
    operations.finished(std::unexpected(makeError(ErrorCode::FirmwareVerifyFailed)));
    ASSERT_TRUE(runUntil([&] { return ok.has_value(); }));
    EXPECT_FALSE(ok.value_or(true));
    EXPECT_EQ(error.code, ErrorCode::FirmwareVerifyFailed);
    EXPECT_NE(error.message.find("Reported by the customer's Monitor"), std::string::npos);

    consent.operations = ScriptedConsent::Mode::Deny;
    ok.reset();
    ASSERT_TRUE(client.request(tunnel::OpKind::Reset, tunnel::OpTarget::App).has_value());
    ASSERT_TRUE(runUntil([&] { return ok.has_value(); }));
    EXPECT_EQ(error.code, ErrorCode::RemoteOperationDenied);
}

TEST(RemoteSession, OperationClientTimesOutWithoutAnAnswer) {
    FakeRelay relay;
    RemoteConsoleSession joiner(joinFor(relay));
    RawParticipant fakeHost(relay.url(), nsp);
    ASSERT_TRUE(fakeHost.waitConnected());
    ASSERT_TRUE(runUntil([&] { return joiner.joinState() == JoinState::Waiting; }));
    fakeHost.emitEvent(
        QStringLiteral("at"),
        tunnel::encodeHost(tunnel::HostHello{
            .app = "hwio-monitor", .version = "2.1.0", .device = {}, .ops = true, .opKinds = {}}));
    ASSERT_TRUE(runUntil([&] { return joiner.hostHello().has_value(); }));
    RemoteOperationClient client(
        joiner, RemoteOperationLimits{.responseTimeout = 200ms, .inactivityTimeout = 200ms});
    std::optional<Error> error;
    QObject::connect(&client, &RemoteOperationClient::finished,
                     [&](bool, const Error& e) { error = e; });
    // ADR-0363: lteUpdate only when the hello lists it (this one lists nothing).
    EXPECT_EQ(client.request(tunnel::OpKind::LteUpdate, tunnel::OpTarget::Lte).error().code,
              ErrorCode::RemoteOperationUnsupportedPeer);
    EXPECT_FALSE(client.busy());
    ASSERT_TRUE(client.request(tunnel::OpKind::Reset, tunnel::OpTarget::App).has_value());
    // A denial for another id is ignored.
    fakeHost.emitEvent(
        QStringLiteral("at"),
        tunnel::encodeHost(tunnel::OpDenied{.id = "other", .code = "remote.operation_denied"}));
    ASSERT_TRUE(runUntil([&] { return error.has_value(); }));
    EXPECT_EQ(error.value_or(Error{}).code, ErrorCode::Timeout);
}

// The web Terminal host, scripted from console.tsx: on newClient it emits sync {state, commands};
// it emits at {state|write|read}; it writes every `write` payload to the device.
TEST(RemoteSession, JoinerWithAWebTerminalHost) {
    FakeRelay relay;
    RawParticipant webHost(relay.url(), QStringLiteral("/12345"));
    ASSERT_TRUE(webHost.waitConnected());
    RemoteConsoleSession joiner(joinFor(relay, 12345));
    ASSERT_TRUE(runUntil([&] { return webHost.named(QStringLiteral("newClient")).size() == 2; }));
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), QStringLiteral("2026-11-10T10:00:00.000Z-0"));
    entry.insert(QStringLiteral("timestamp"), QStringLiteral("2026-11-10T10:00:00.000Z"));
    entry.insert(QStringLiteral("command"), QStringLiteral("AT+CSQ"));
    entry.insert(QStringLiteral("index"), 0);
    QJsonObject response;
    response.insert(QStringLiteral("outputs"),
                    QJsonArray{QStringLiteral("+CSQ: 20,99"), QStringLiteral("OK")});
    response.insert(QStringLiteral("result"), 0);
    entry.insert(QStringLiteral("response"), response);
    QJsonObject sync;
    sync.insert(QStringLiteral("state"), QStringLiteral("connected"));
    sync.insert(QStringLiteral("commands"), QJsonArray{entry});
    webHost.emitEvent(QStringLiteral("sync"), sync);
    ASSERT_TRUE(runUntil([&] { return joiner.joinState() == JoinState::Allowed; }));
    EXPECT_EQ(joiner.title(), QStringLiteral("Session 12345 — web Terminal"));
    EXPECT_EQ(texts(joiner.log(), device::Direction::In), std::vector<std::string>{"AT+CSQ"});
    EXPECT_EQ(texts(joiner.log(), device::Direction::Out),
              (std::vector<std::string>{"+CSQ: 20,99", "OK"}));

    ASSERT_TRUE(joiner.sendLine("AT+CGSN").has_value());
    ASSERT_TRUE(runUntil([&] { return !webHost.named(QStringLiteral("write")).isEmpty(); }));
    EXPECT_EQ(webHost.named(QStringLiteral("write")).front().payload.toString(),
              QStringLiteral("AT+CGSN"));
    // No tunnel object without a host hello: a web host would type it into the device.
    EXPECT_EQ(joiner.sendTunnel(tunnel::JoinerHello{.version = "x"}).error().code,
              ErrorCode::RemoteOperationUnsupportedPeer);
    RemoteOperationClient client(joiner);
    EXPECT_EQ(client.request(tunnel::OpKind::Reset, tunnel::OpTarget::App).error().code,
              ErrorCode::RemoteOperationUnsupportedPeer);
    runFor(100ms);
    for (const auto& write : webHost.named(QStringLiteral("write"))) {
        EXPECT_TRUE(write.payload.isString());
    }

    QJsonObject at;
    at.insert(QStringLiteral("event"), QStringLiteral("read"));
    at.insert(QStringLiteral("data"), QStringLiteral("123456789"));
    webHost.emitEvent(QStringLiteral("at"), at);
    at.insert(QStringLiteral("event"), QStringLiteral("unknown"));
    webHost.emitEvent(QStringLiteral("at"), at);
    at.insert(QStringLiteral("event"), QStringLiteral("state"));
    at.insert(QStringLiteral("data"), QStringLiteral("disconnected"));
    webHost.emitEvent(QStringLiteral("at"), at);
    ASSERT_TRUE(runUntil(
        [&] { return joiner.connection().state == device::ConnectionState::Disconnected; }));
    EXPECT_EQ(texts(joiner.log(), device::Direction::Out).back(), "123456789");
}

TEST(RemoteSession, JoinerIdleNoticeAndRelayFailureEndsTheSession) {
    auto relay = std::make_unique<FakeRelay>();
    JoinOptions options = joinFor(*relay);
    options.idleNoticeAfter = 200ms;
    options.relayLimits.reconnectFirstDelay = 50ms;
    options.relayLimits.reconnectBudget = 300ms;
    RemoteConsoleSession joiner(options);
    bool idle = false;
    QObject::connect(&joiner, &RemoteConsoleSession::idleNotice, [&] { idle = true; });
    ASSERT_TRUE(runUntil([&] { return joiner.joinState() == JoinState::Waiting; }));
    ASSERT_TRUE(runUntil([&] { return idle; }));
    relay.reset();
    ASSERT_TRUE(runUntil([&] { return joiner.joinState() == JoinState::Ended; }));
    EXPECT_EQ(joiner.connection().state, device::ConnectionState::Stopped);
    ASSERT_TRUE(joiner.connection().error.has_value());
    EXPECT_EQ(joiner.connection().error.value_or(Error{}).code, ErrorCode::RemoteHostEnded);
    EXPECT_EQ(joiner.sendLine("x").error().code, ErrorCode::RemoteHostEnded);
}

// ADR-0354: End sharing on the host sends `end`; the joiner's session ends right away.
TEST(RemoteSession, HostEndEndsTheJoinerSession) {
    FakeRelay relay;
    FakeConsole console;
    ScriptedConsent consent;
    RemoteHost host(console, consent, nullptr, hostFor(relay));
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));
    JoinOptions options = joinFor(relay);
    options.relayLimits.reconnectBudget = 60s; // only `end` can end the session in time
    RemoteConsoleSession joiner(options);
    ASSERT_TRUE(
        runUntil([&] { return joiner.joinState() == JoinState::Allowed && joiner.hostHello(); }));
    host.end();
    ASSERT_TRUE(runUntil([&] { return joiner.joinState() == JoinState::Ended; }, 2s));
    EXPECT_EQ(joiner.connection().state, device::ConnectionState::Stopped);
    EXPECT_EQ(joiner.connection().error.value_or(Error{}).code, ErrorCode::RemoteHostEnded);
}

// An `end` object before the host's hello is not from a Monitor host: ignored (ADR-0336).
TEST(RemoteSession, HostEndBeforeHelloIsIgnored) {
    FakeRelay relay;
    RawParticipant webHost(relay.url(), QStringLiteral("/12345"));
    ASSERT_TRUE(webHost.waitConnected());
    RemoteConsoleSession joiner(joinFor(relay, 12345));
    ASSERT_TRUE(runUntil([&] { return webHost.named(QStringLiteral("newClient")).size() == 2; }));
    webHost.emitEvent(QStringLiteral("at"), tunnel::encodeHost(tunnel::HostEnd{}));
    runFor(200ms);
    EXPECT_NE(joiner.joinState(), JoinState::Ended);
}

TEST(RemoteSession, JoinerRelayUnreachable) {
    quint16 port = 0;
    {
        FakeRelay relay;
        port = relay.port();
    }
    JoinOptions options;
    options.relay = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
    options.code = *SessionCode::fromValue(482913);
    RemoteConsoleSession joiner(options);
    ASSERT_TRUE(runUntil([&] { return joiner.joinState() == JoinState::Ended; }));
    EXPECT_EQ(joiner.connection().error.value_or(Error{}).code, ErrorCode::RemoteRelayUnreachable);
}

// ADR-0364: a Monitor host's command tree reaches the joiner; learn goes to the host once.
TEST(RemoteSession, CommandTreeAndLearn) {
    FakeRelay relay;
    FakeConsole console;
    ScriptedConsent consent;
    RemoteHost host(console, consent, nullptr, hostFor(relay));
    QStringList learned;
    QObject::connect(&host, &RemoteHost::learnRequested,
                     [&](const QStringList& path) { learned.append(path.join(u' ')); });
    device::CommandTree tree;
    const std::vector<device::CommandEntry> top{{.name = "app", .help = "Application"}};
    ASSERT_TRUE(tree.setChildren({}, top));
    host.setCommandTree(std::make_shared<const device::CommandTree>(tree));
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));
    RemoteConsoleSession joiner(joinFor(relay));
    int changes = 0;
    QObject::connect(&joiner, &RemoteConsoleSession::commandTreeChanged, [&] { ++changes; });
    ASSERT_TRUE(runUntil([&] { return joiner.commandTree() != nullptr; }));
    EXPECT_EQ(changes, 1);
    const std::vector<std::string> app{"app"};
    ASSERT_NE(joiner.commandTree()->find(app), nullptr);
    joiner.requestLearn(app);
    joiner.requestLearn(app); // asked already
    ASSERT_TRUE(runUntil([&] { return learned.size() == 1; }));
    runFor(200ms);
    EXPECT_EQ(learned.size(), 1);
    // The host learns it: the grown tree follows.
    const std::vector<device::CommandEntry> sub{{.name = "config", .help = "Configuration"}};
    ASSERT_TRUE(tree.setChildren(app, sub));
    host.setCommandTree(std::make_shared<const device::CommandTree>(tree));
    const std::vector<std::string> config{"app", "config"};
    ASSERT_TRUE(runUntil([&] { return joiner.commandTree()->find(config) != nullptr; }));
}

} // namespace
} // namespace hwio::remote
