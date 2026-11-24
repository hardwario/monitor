// Plan 0059 A1–A3 (ADR-0414, ADR-0416): the protocol additions against Monitor 2.0.0-rc.4 (its
// frozen tunnel codec, rc4_tunnel.h) and the web Terminal, both ways: golden messages, the rc.4
// error id baseline, and sessions over FakeRelay with rc.4-shaped and web-Terminal-shaped peers.

#include "fake_console.h"
#include "fake_relay.h"
#include "hwio/core/error.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/profile.h"
#include "hwio/remote/baseline_errors.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/remote_host.h"
#include "hwio/remote/remote_operation_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "rc4_tunnel.h"
#include "remote_test_support.h"
#include "test_profile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
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
// A target id that only newer peers know (a Nordic chip).
constexpr tunnel::OpTarget nrf52 =
    tunnel::OpTarget::fromId("nrf52").value_or(tunnel::OpTarget::App);

QJsonObject json(const char* text) { return QJsonDocument::fromJson(text).object(); }

HostOptions hostFor(const FakeRelay& relay) {
    HostOptions options;
    options.relay = relay.url();
    options.code = *SessionCode::fromValue(482913);
    options.appVersion = "2.1.0-customer";
    options.limits.collisionWindow = 100ms;
    return options;
}

JoinOptions joinFor(const FakeRelay& relay) {
    JoinOptions options;
    options.relay = relay.url();
    options.code = *SessionCode::fromValue(482913);
    options.appVersion = "2.1.0-support";
    return options;
}

// The hwio tunnel objects a raw participant received in `at` events.
std::vector<QJsonObject> tunnelAts(const RawParticipant& p) {
    std::vector<QJsonObject> out;
    for (const auto& r : p.named(QStringLiteral("at"))) {
        if (tunnel::isTunnelAt(r.payload)) {
            out.push_back(r.payload.toObject());
        }
    }
    return out;
}

std::vector<QString> reads(const RawParticipant& p) {
    std::vector<QString> out;
    for (const auto& r : p.named(QStringLiteral("at"))) {
        const QJsonObject at = r.payload.toObject();
        if (at.value(QStringLiteral("event")).toString() == QStringLiteral("read")) {
            out.push_back(at.value(QStringLiteral("data")).toString());
        }
    }
    return out;
}

bool hasMarker(const device::ConsoleLog& log, std::string_view part) {
    return std::ranges::any_of(log.linesSince(0), [part](const device::ConsoleLine& line) {
        return line.dir == device::Direction::Marker && line.text.find(part) != std::string::npos;
    });
}

// ---- the rc.4 error id baseline ----

TEST(Rc4Baseline, IsTheFrozenRc4SetAndCoversEveryErrorCode) {
    const auto ids = baselineErrorIds();
    EXPECT_EQ(ids.size(), 100U);
    EXPECT_EQ(std::set<std::string_view>(ids.begin(), ids.end()).size(), ids.size());
    EXPECT_EQ(ids.front(), "internal.unexpected");
    EXPECT_EQ(ids.back(), "remote.application_not_restored");
    for (std::size_t i = 0; i < errorTable.size(); ++i) {
        const auto code = static_cast<ErrorCode>(i);
        // A new ErrorCode needs a row in baseline_errors.cpp (plan 0059, ADR-0414).
        EXPECT_TRUE(hasBaselineMapping(code)) << idOf(code);
        const std::string_view baseline = baselineErrorId(code);
        EXPECT_TRUE(isBaselineErrorId(baseline)) << idOf(code);
        if (isBaselineErrorId(idOf(code))) {
            EXPECT_EQ(baseline, idOf(code));
        } else {
            // Same category, nearest meaning (rc.4's own categories).
            EXPECT_TRUE(errorCodeFromId(baseline).has_value()) << baseline;
        }
    }
    EXPECT_EQ(baselineErrorId(ErrorCode::TargetLowVoltage), "target.no_power");
    EXPECT_EQ(baselineErrorId(ErrorCode::RemoteTargetUnsupported), "remote.operation_denied");
    EXPECT_EQ(baselineErrorId(ErrorCode::RemoteCommandRefused), "remote.operation_denied");
}

// ---- golden messages: new host → rc.4 joiner ----

TEST(Rc4Compat, EveryErrorCodeReachesAnRc4Joiner) {
    for (std::size_t i = 0; i < errorTable.size(); ++i) {
        const std::string id(idOf(static_cast<ErrorCode>(i)));
        const std::array<tunnel::HostMessage, 3> messages{
            tunnel::OpDenied{.id = "op-1", .code = id},
            tunnel::OpResult{.id = "op-1", .ok = false, .code = id},
            tunnel::HostEnd{.code = id},
        };
        for (const tunnel::HostMessage& message : messages) {
            const QJsonObject at = tunnel::encodeHost(message);
            auto old = rc4::decodeHost(at);
            ASSERT_TRUE(old.has_value()) << id << ": " << old.error().detail;
            // The new decoder gets the real id back from `detail`.
            auto current = tunnel::decodeHost(at);
            ASSERT_TRUE(current.has_value()) << id;
            std::visit(
                [&](const auto& m) {
                    using T = std::decay_t<decltype(m)>;
                    if constexpr (std::is_same_v<T, tunnel::OpDenied> ||
                                  std::is_same_v<T, tunnel::HostEnd>) {
                        EXPECT_EQ(m.code, id);
                    } else if constexpr (std::is_same_v<T, tunnel::OpResult>) {
                        EXPECT_EQ(m.code, std::optional(id));
                    }
                },
                *current);
        }
    }
}

TEST(Rc4Compat, NewErrorIdsTravelAsTheirBaselineWithADetail) {
    const QJsonObject denied =
        tunnel::encodeHost(tunnel::OpDenied{.id = "op-1", .code = "remote.target_unsupported"});
    const QJsonObject data = denied.value(QStringLiteral("data")).toObject();
    EXPECT_EQ(data.value(QStringLiteral("code")).toString(),
              QStringLiteral("remote.operation_denied"));
    EXPECT_EQ(data.value(QStringLiteral("detail")).toObject().value(QStringLiteral("code")),
              QStringLiteral("remote.target_unsupported"));
    EXPECT_EQ(std::get<rc4::OpDenied>(*rc4::decodeHost(denied)).code, "remote.operation_denied");

    // op-result: the detail sits inside `error`, next to the code it refines.
    const QJsonObject result = tunnel::encodeHost(
        tunnel::OpResult{.id = "op-1", .ok = false, .code = "target.low_voltage"});
    const QJsonObject error =
        result.value(QStringLiteral("data")).toObject().value(QStringLiteral("error")).toObject();
    EXPECT_EQ(error.value(QStringLiteral("code")).toString(), QStringLiteral("target.no_power"));
    EXPECT_EQ(std::get<rc4::OpResult>(*rc4::decodeHost(result)).code,
              std::optional<std::string>("target.no_power"));
    EXPECT_EQ(std::get<tunnel::OpResult>(*tunnel::decodeHost(result)).code,
              std::optional<std::string>("target.low_voltage"));

    // An rc.4 id goes unchanged, without a detail (byte for byte as an rc.4 host).
    const QJsonObject busy =
        tunnel::encodeHost(tunnel::OpDenied{.id = "op-1", .code = "internal.busy"});
    EXPECT_EQ(busy, rc4::encodeHost(rc4::OpDenied{.id = "op-1", .code = "internal.busy"}));
}

TEST(Rc4Compat, NewHostHellosDecodeInAnRc4Joiner) {
    // A CHESTER host of this build: as an rc.4 host plus caps and profile.
    tunnel::HostHello chester{.app = "hwio-monitor",
                              .version = "2.1.0",
                              .device = {.name = "CHESTER",
                                         .serial = "2159018247",
                                         .transport = "jlink",
                                         .firmware = "CHESTER-Clime v3.2.1"},
                              .ops = true,
                              .opKinds = {tunnel::OpKind::Flash, tunnel::OpKind::Erase,
                                          tunnel::OpKind::Recover, tunnel::OpKind::Reset,
                                          tunnel::OpKind::LteUpdate},
                              .opQuestions = true,
                              .caps = {"detail-codes", "refused-lines"},
                              .profile = "chester"};
    auto old = rc4::decodeHost(tunnel::encodeHost(chester));
    ASSERT_TRUE(old.has_value()) << old.error().detail;
    const auto& oldHello = std::get<rc4::HostHello>(*old);
    EXPECT_TRUE(oldHello.ops);
    EXPECT_EQ(oldHello.opKinds.size(), 5U);
    EXPECT_TRUE(oldHello.opQuestions);
    EXPECT_EQ(oldHello.device.serial, "2159018247");

    // A host of another profile: every extension at its bounds; rc.4 sees a console-only host.
    tunnel::HostHello other = chester;
    other.ops = false;
    other.opKinds.clear();
    other.opQuestions = false;
    other.profile = "sticker";
    other.console = false;
    other.caps.clear();
    for (std::size_t i = 0; i < tunnel::maxCaps; ++i) {
        other.caps.push_back("cap-" + std::to_string(i));
    }
    other.targets = std::vector<tunnel::HostTarget>{};
    for (std::size_t i = 0; i < tunnel::maxTargets; ++i) {
        tunnel::HostTarget target{.id = "t" + std::to_string(i),
                                  .kinds = {tunnel::OpKind::Flash, tunnel::OpKind::Reset},
                                  .catalog = false,
                                  .prepared = {}};
        for (std::size_t j = 0; j < tunnel::maxPrepared; ++j) {
            target.prepared.push_back(tunnel::PreparedFirmware{
                .id = "p" + std::to_string(j), .name = "STICKER", .version = "2.0.0"});
        }
        other.targets->push_back(std::move(target));
    }
    const QJsonObject at = tunnel::encodeHost(other);
    auto oldOther = rc4::decodeHost(at);
    ASSERT_TRUE(oldOther.has_value()) << oldOther.error().detail;
    EXPECT_FALSE(std::get<rc4::HostHello>(*oldOther).ops);
    auto current = tunnel::decodeHost(at);
    ASSERT_TRUE(current.has_value()) << current.error().detail;
    const auto& hello = std::get<tunnel::HostHello>(*current);
    EXPECT_EQ(hello.profile, "sticker");
    EXPECT_FALSE(hello.console);
    EXPECT_EQ(hello.caps, other.caps);
    ASSERT_TRUE(hello.targets.has_value());
    const std::vector<tunnel::HostTarget> targets =
        hello.targets.value_or(std::vector<tunnel::HostTarget>{});
    ASSERT_EQ(targets.size(), tunnel::maxTargets);
    EXPECT_EQ(targets.back().prepared.size(), tunnel::maxPrepared);
    EXPECT_EQ(targets.back().prepared.back().id, "p3");
    EXPECT_NE(tunnel::findTarget(hello, "t7"), nullptr);
    EXPECT_EQ(tunnel::findTarget(hello, "t8"), nullptr);
}

TEST(Rc4Compat, LineRefusedIsUnknownToRc4AndOnlyCapGated) {
    // Why the host sends it only to joiners with the refused-lines cap: rc.4 rejects the type
    // (its joiner shows a warning).
    const QJsonObject at = tunnel::encodeHost(tunnel::LineRefused{});
    EXPECT_FALSE(rc4::decodeHost(at).has_value());
    auto decoded = tunnel::decodeHost(at);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
    EXPECT_EQ(std::get<tunnel::LineRefused>(*decoded).code, "remote.command_refused");
}

// ---- golden messages: new joiner → rc.4 host ----

TEST(Rc4Compat, NewJoinerMessagesTheRc4HostDecodes) {
    auto hello = rc4::decodeJoiner(tunnel::encodeJoiner(tunnel::JoinerHello{
        .app = "hwio-monitor", .version = "2.1.0", .caps = {"targets", "refused-lines"}}));
    ASSERT_TRUE(hello.has_value()) << hello.error().detail;
    EXPECT_EQ(std::get<rc4::JoinerHello>(*hello).version, "2.1.0");
    // app / lte requests with a catalog ID are exactly rc.4's.
    const tunnel::OpRequest flash{.id = "op-1",
                                  .kind = tunnel::OpKind::Flash,
                                  .target = tunnel::OpTarget::App,
                                  .firmwareId = "0123456789abcdef0123456789abcdef"};
    auto old = rc4::decodeJoiner(tunnel::encodeJoiner(flash));
    ASSERT_TRUE(old.has_value()) << old.error().detail;
    EXPECT_EQ(std::get<rc4::OpRequest>(*old).target, rc4::OpTarget::App);
    EXPECT_EQ(tunnel::encodeJoiner(flash),
              rc4::encodeJoiner(rc4::OpRequest{.id = "op-1",
                                               .kind = rc4::OpKind::Flash,
                                               .target = rc4::OpTarget::App,
                                               .firmwareId = "0123456789abcdef0123456789abcdef"}));
    // Other targets and prepared firmware do not decode there (no answer at all, F8): the joiner
    // never sends them to a host without `targets` (JoinerGateToAnOlderHost).
    EXPECT_FALSE(
        rc4::decodeJoiner(tunnel::encodeJoiner(tunnel::OpRequest{
                              .id = "op-2", .kind = tunnel::OpKind::Reset, .target = nrf52}))
            .has_value());
    EXPECT_FALSE(
        rc4::decodeJoiner(tunnel::encodeJoiner(tunnel::OpRequest{.id = "op-3",
                                                                 .kind = tunnel::OpKind::Flash,
                                                                 .target = tunnel::OpTarget::App,
                                                                 .prepared = "debug"}))
            .has_value());
}

// ---- golden messages: rc.4 peers → this build ----

TEST(Rc4Compat, Rc4MessagesDecodeAsBefore) {
    auto hello = tunnel::decodeHost(rc4::encodeHost(rc4::HostHello{
        .app = "hwio-monitor",
        .version = "2.0.0-rc.4",
        .device = {.name = "CHESTER", .serial = "1", .transport = "jlink", .firmware = ""},
        .ops = true,
        .opKinds = {rc4::OpKind::Flash, rc4::OpKind::Erase, rc4::OpKind::Recover,
                    rc4::OpKind::Reset},
        .opQuestions = false}));
    ASSERT_TRUE(hello.has_value()) << hello.error().detail;
    const auto& h = std::get<tunnel::HostHello>(*hello);
    EXPECT_TRUE(h.ops);
    EXPECT_TRUE(h.caps.empty());
    EXPECT_TRUE(h.profile.empty());
    EXPECT_TRUE(h.console);
    EXPECT_FALSE(h.targets.has_value());
    EXPECT_EQ(&RemoteConsoleSession::profileFor(h), device::ProfileRegistry::find("chester"));

    auto joinerHello = tunnel::decodeJoiner(
        rc4::encodeJoiner(rc4::JoinerHello{.app = "hwio-monitor", .version = "2.0.0-rc.4"}));
    ASSERT_TRUE(joinerHello.has_value());
    EXPECT_TRUE(std::get<tunnel::JoinerHello>(*joinerHello).caps.empty());

    auto request =
        tunnel::decodeJoiner(rc4::encodeJoiner(rc4::OpRequest{.id = "op-1",
                                                              .kind = rc4::OpKind::Erase,
                                                              .target = rc4::OpTarget::Lte,
                                                              .firmwareId = std::nullopt}));
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(std::get<tunnel::OpRequest>(*request).target, tunnel::OpTarget::Lte);
    EXPECT_FALSE(std::get<tunnel::OpRequest>(*request).prepared.has_value());

    auto denied = tunnel::decodeHost(
        rc4::encodeHost(rc4::OpDenied{.id = "op-1", .code = "remote.operation_denied"}));
    ASSERT_TRUE(denied.has_value());
    EXPECT_EQ(std::get<tunnel::OpDenied>(*denied).code, "remote.operation_denied");
}

TEST(Rc4Compat, UnknownFieldsAreIgnoredUnknownCodesRejected) {
    // A later host: unknown fields, caps, kinds and a detail id this build does not know.
    auto later = tunnel::decodeHost(json(
        R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"9",
            "device":{"name":"X","serial":"","transport":"jlink","firmware":""},"ops":false,
            "opKinds":[],"future":{"a":1},"caps":["targets","quantum"],"profile":"nrf",
            "targets":[{"id":"nrf52","kinds":["erase","teleport","reset"],"catalog":false,
                        "extra":true}]}})"));
    ASSERT_TRUE(later.has_value()) << later.error().detail;
    const auto& hello = std::get<tunnel::HostHello>(*later);
    EXPECT_EQ(hello.caps, (std::vector<std::string>{"targets", "quantum"}));
    ASSERT_NE(tunnel::findTarget(hello, "nrf52"), nullptr);
    EXPECT_EQ(tunnel::findTarget(hello, "nrf52")->kinds,
              (std::vector<tunnel::OpKind>{tunnel::OpKind::Erase, tunnel::OpKind::Reset}));
    auto refined = tunnel::decodeHost(json(
        R"({"event":"hwio","data":{"v":1,"type":"op-denied","id":"a",
            "code":"remote.operation_denied","detail":{"code":"remote.not_yet_invented"}}})"));
    ASSERT_TRUE(refined.has_value());
    EXPECT_EQ(std::get<tunnel::OpDenied>(*refined).code, "remote.operation_denied");

    for (
        const char* text : {
            // an unknown `code` (with or without a known detail) is still rejected
            R"({"event":"hwio","data":{"v":1,"type":"op-denied","id":"a","code":"made.up"}})",
            R"({"event":"hwio","data":{"v":1,"type":"end","code":"made.up","detail":{"code":"cancelled"}}})",
            R"({"event":"hwio","data":{"v":1,"type":"line-refused","code":"made.up"}})",
            R"({"event":"hwio","data":{"v":1,"type":"op-denied","id":"a","code":"cancelled","detail":"x"}})",
            // v stays 1
            R"({"event":"hwio","data":{"v":2,"type":"end","code":"cancelled"}})",
            // malformed extensions
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"9","device":{"name":"","serial":"","transport":"","firmware":""},"ops":false,"caps":"targets"}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"9","device":{"name":"","serial":"","transport":"","firmware":""},"ops":false,"caps":["UPPER"]}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"9","device":{"name":"","serial":"","transport":"","firmware":""},"ops":false,"profile":"a b"}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"9","device":{"name":"","serial":"","transport":"","firmware":""},"ops":false,"console":"yes"}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"9","device":{"name":"","serial":"","transport":"","firmware":""},"ops":false,"targets":[{"id":"a"},{"id":"a"}]}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"9","device":{"name":"","serial":"","transport":"","firmware":""},"ops":false,"targets":[{"id":"app","prepared":[{"id":"x","name":"n"}]}]}})",
        }) {
        EXPECT_FALSE(tunnel::decodeHost(json(text)).has_value()) << text;
    }
    // Over the bounds.
    QJsonObject tooMany = json(
        R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"9","device":{"name":"","serial":"","transport":"","firmware":""},"ops":false}})");
    QJsonObject data = tooMany.value(QStringLiteral("data")).toObject();
    QJsonArray caps;
    QJsonArray targets;
    for (std::size_t i = 0; i <= tunnel::maxCaps; ++i) {
        caps.append(QStringLiteral("c%1").arg(i));
    }
    for (std::size_t i = 0; i <= tunnel::maxTargets; ++i) {
        QJsonObject target;
        target.insert(QStringLiteral("id"), QStringLiteral("t%1").arg(i));
        targets.append(target);
    }
    for (const auto& [key, value] :
         {std::pair{QStringLiteral("caps"), caps}, std::pair{QStringLiteral("targets"), targets}}) {
        QJsonObject over = data;
        over.insert(key, value);
        tooMany.insert(QStringLiteral("data"), over);
        EXPECT_FALSE(tunnel::decodeHost(tooMany).has_value()) << key.toStdString();
    }
    EXPECT_FALSE(tunnel::OpTarget::fromId(std::string(tunnel::maxNameBytes + 1, 'a')).has_value());
    EXPECT_TRUE(tunnel::OpTarget::fromId(std::string(tunnel::maxNameBytes, 'a')).has_value());
}

TEST(Rc4Compat, PreparedFlashRequests) {
    const tunnel::OpRequest prepared{.id = "op-1",
                                     .kind = tunnel::OpKind::Flash,
                                     .target = tunnel::OpTarget::App,
                                     .prepared = "debug"};
    auto decoded = tunnel::decodeJoiner(tunnel::encodeJoiner(prepared));
    ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
    EXPECT_EQ(std::get<tunnel::OpRequest>(*decoded).prepared, std::optional<std::string>("debug"));
    EXPECT_FALSE(std::get<tunnel::OpRequest>(*decoded).firmwareId.has_value());
    for (
        const char* text : {
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"reset","target":"app","prepared":"debug"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"flash","target":"app","prepared":"debug","firmwareId":"0123456789abcdef0123456789abcdef"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"flash","target":"app","prepared":"De Bug"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"reset","target":"App"}})",
        }) {
        EXPECT_FALSE(tunnel::decodeJoiner(json(text)).has_value()) << text;
    }
}

// ---- sessions over FakeRelay ----

// A rc.4 joiner (its frozen codec on a raw participant) on a CHESTER host of this build: every
// host message decodes there, a new error id arrives as its rc.4 baseline, no warning-causing type
// is sent to it.
TEST(Rc4CompatSession, NewHostWithAnRc4Joiner) {
    FakeRelay relay;
    FakeConsole console;
    ScriptedConsent consent;
    FakeOperations operations;
    RemoteHost host(console, consent, &operations, hostFor(relay));
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));
    RawParticipant old(relay.url(), nsp);
    ASSERT_TRUE(old.waitConnected());
    ASSERT_TRUE(runUntil([&] { return !tunnelAts(old).empty(); })); // the host's hello
    old.emitEvent(QStringLiteral("write"), rc4::encodeJoiner(rc4::JoinerHello{
                                               .app = "hwio-monitor", .version = "2.0.0-rc.4"}));
    ASSERT_TRUE(runUntil([&] { return host.peerApp() == "HARDWARIO Monitor 2.0.0-rc.4"; }));
    EXPECT_TRUE(console.hasEvent("Participant app: HARDWARIO Monitor 2.0.0-rc.4."));

    old.emitEvent(QStringLiteral("write"), rc4::encodeJoiner(rc4::OpRequest{
                                               .id = "op-1",
                                               .kind = rc4::OpKind::Flash,
                                               .target = rc4::OpTarget::App,
                                               .firmwareId = "0123456789abcdef0123456789abcdef"}));
    ASSERT_TRUE(runUntil([&] { return operations.started.size() == 1; }));
    operations.finished(std::unexpected(makeError(ErrorCode::TargetLowVoltage)));
    ASSERT_TRUE(runUntil([&] {
        return std::ranges::any_of(tunnelAts(old), [](const QJsonObject& at) {
            return at.value(QStringLiteral("data")).toObject().value(QStringLiteral("type")) ==
                   QStringLiteral("op-result");
        });
    }));
    runFor(100ms);
    bool sawHello = false;
    for (const QJsonObject& at : tunnelAts(old)) {
        auto decoded = rc4::decodeHost(at);
        ASSERT_TRUE(decoded.has_value())
            << decoded.error().detail << " "
            << QJsonDocument(at).toJson(QJsonDocument::Compact).toStdString();
        if (const auto* hello = std::get_if<rc4::HostHello>(&*decoded)) {
            sawHello = true;
            EXPECT_TRUE(hello->ops); // CHESTER: as today
            EXPECT_EQ(hello->opKinds.size(), 5U);
        }
        if (const auto* result = std::get_if<rc4::OpResult>(&*decoded)) {
            EXPECT_EQ(result->code, std::optional<std::string>("target.no_power"));
        }
    }
    EXPECT_TRUE(sawHello);
}

// A request for a target the host does not have: denied at once (before plan 0059 it did not
// decode and got no answer; F8).
TEST(Rc4CompatSession, UnknownTargetIsDeniedNotIgnored) {
    FakeRelay relay;
    FakeConsole console;
    ScriptedConsent consent;
    FakeOperations operations;
    RemoteHost host(console, consent, &operations, hostFor(relay));
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));
    RawParticipant joiner(relay.url(), nsp);
    ASSERT_TRUE(joiner.waitConnected());
    ASSERT_TRUE(runUntil([&] { return !tunnelAts(joiner).empty(); }));
    joiner.emitEvent(QStringLiteral("write"),
                     tunnel::encodeJoiner(tunnel::OpRequest{
                         .id = "op-1", .kind = tunnel::OpKind::Reset, .target = nrf52}));
    std::optional<tunnel::OpDenied> denied;
    ASSERT_TRUE(runUntil([&] {
        for (const QJsonObject& at : tunnelAts(joiner)) {
            if (auto decoded = tunnel::decodeHost(at);
                decoded && std::holds_alternative<tunnel::OpDenied>(*decoded)) {
                denied = std::get<tunnel::OpDenied>(*decoded);
            }
        }
        return denied.has_value();
    }));
    EXPECT_EQ(denied.value_or(tunnel::OpDenied{}).code, "remote.target_unsupported");
    EXPECT_TRUE(operations.started.empty());
    EXPECT_TRUE(consent.operationQuestions.empty());
}

// An rc.4 host (its frozen codec on a raw participant) with a joiner of this build: CHESTER
// console rules, operations as in 0044, and only messages that rc.4 decodes.
TEST(Rc4CompatSession, NewJoinerWithAnRc4Host) {
    FakeRelay relay;
    RawParticipant oldHost(relay.url(), nsp);
    ASSERT_TRUE(oldHost.waitConnected());
    RemoteConsoleSession joiner(joinFor(relay));
    ASSERT_TRUE(runUntil([&] { return joiner.joinState() == JoinState::Waiting; }));
    std::vector<Error> warnings;
    QObject::connect(&joiner, &device::ConsoleSession::warning,
                     [&](const Error& error) { warnings.push_back(error); });
    oldHost.emitEvent(
        QStringLiteral("at"),
        rc4::encodeHost(rc4::HostHello{.app = "hwio-monitor",
                                       .version = "2.0.0-rc.4",
                                       .device = {.name = "CHESTER",
                                                  .serial = "2159018247",
                                                  .transport = "jlink",
                                                  .firmware = ""},
                                       .ops = true,
                                       .opKinds = {rc4::OpKind::Flash, rc4::OpKind::Erase,
                                                   rc4::OpKind::Recover, rc4::OpKind::Reset},
                                       .opQuestions = false}));
    ASSERT_TRUE(runUntil([&] { return joiner.hostHello().has_value(); }));
    EXPECT_EQ(joiner.profile().id, "chester");
    EXPECT_EQ(joiner.title(), QStringLiteral("CHESTER 2159018247 — remote"));

    RemoteOperationClient client(joiner);
    // Not sent to an rc.4 host: it could not decode them (F8: no answer, 90 s timeout).
    EXPECT_EQ(client.request(tunnel::OpKind::Reset, nrf52).error().code,
              ErrorCode::RemoteTargetUnsupported);
    EXPECT_EQ(client
                  .request(tunnel::OpKind::Flash, tunnel::OpTarget::App, std::nullopt,
                           std::string("debug"))
                  .error()
                  .code,
              ErrorCode::RemoteTargetUnsupported);
    EXPECT_FALSE(client.busy());
    std::optional<Error> finished;
    QObject::connect(&client, &RemoteOperationClient::finished,
                     [&](bool, const Error& e) { finished = e; });
    auto id = client.request(tunnel::OpKind::Erase, tunnel::OpTarget::Lte);
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(runUntil([&] { return oldHost.named(QStringLiteral("write")).size() >= 2; }));
    // Everything this joiner wrote decodes in rc.4 (its hello with caps, the request).
    for (const auto& write : oldHost.named(QStringLiteral("write"))) {
        auto decoded = rc4::decodeJoiner(write.payload);
        ASSERT_TRUE(decoded.has_value())
            << decoded.error().detail << " "
            << QJsonDocument(write.payload.toObject()).toJson(QJsonDocument::Compact).toStdString();
    }
    oldHost.emitEvent(QStringLiteral("at"),
                      rc4::encodeHost(rc4::OpDenied{.id = *id, .code = "remote.operation_denied"}));
    ASSERT_TRUE(runUntil([&] { return finished.has_value(); }));
    EXPECT_EQ(finished.value_or(Error{}).code, ErrorCode::RemoteOperationDenied);
    EXPECT_TRUE(warnings.empty());
}

// A host of this build with a profile other than CHESTER: an rc.4 joiner sees a console-only host
// (ops: false); a joiner of this build uses the neutral console rules for a profile it does not
// know, and refused remote lines come back as a marker (refused-lines) while the web Terminal and
// rc.4 joiners get the English relay line.
TEST(Rc4CompatSession, OtherProfileHostRefusesLinesAndRedacts) {
    FakeRelay relay;
    FakeConsole console;
    console.profileOverride = &testing::testProfile;
    ScriptedConsent consent;
    FakeOperations operations;
    RemoteHost host(console, consent, &operations, hostFor(relay));
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));

    RawParticipant web(relay.url(), nsp); // web Terminal: never sends a hello
    ASSERT_TRUE(web.waitConnected());
    RemoteConsoleSession joiner(joinFor(relay));
    ASSERT_TRUE(runUntil([&] {
        return joiner.hostHello().has_value() && joiner.joinState() == JoinState::Allowed;
    }));
    ASSERT_TRUE(runUntil([&] { return !host.peerApp().empty(); }));
    const tunnel::HostHello hello = joiner.hostHello().value_or(tunnel::HostHello{});
    EXPECT_FALSE(hello.ops);
    EXPECT_EQ(hello.profile, "testdev");
    EXPECT_EQ(&joiner.profile(), &device::genericConsoleProfile());
    EXPECT_TRUE(console.hasEvent(
        "Participant app: HARDWARIO Monitor 2.1.0-support (capabilities: targets, detail-codes, "
        "refused-lines)."));
    for (const QJsonObject& at : tunnelAts(web)) {
        auto decoded = rc4::decodeHost(at);
        ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
        if (const auto* oldHello = std::get_if<rc4::HostHello>(&*decoded)) {
            EXPECT_FALSE(oldHello->ops); // an rc.4 joiner offers the console only
        }
    }
    // No operations behind ops: false.
    RemoteOperationClient client(joiner);
    EXPECT_EQ(client.request(tunnel::OpKind::Reset, tunnel::OpTarget::App).error().code,
              ErrorCode::RemoteOperationUnsupportedPeer);

    // Refused from remote: sets a secret, a refused setter, an identity-destroying command.
    for (const char* line : {"config secret-key 00112233", "config serial-number 42",
                             "settings  erase", "nfc write 0 00"}) {
        ASSERT_TRUE(joiner.sendLine(line).has_value());
    }
    // Allowed: reading the secret (its output is redacted), reading the serial number.
    ASSERT_TRUE(joiner.sendLine("config secret-key").has_value());
    ASSERT_TRUE(joiner.sendLine("config serial-number").has_value());
    ASSERT_TRUE(runUntil([&] { return console.sent.size() == 2; }));
    EXPECT_EQ(console.sentLines(),
              (std::vector<std::string>{"config secret-key", "config serial-number"}));
    console.shell("config secret-key 00112233445566778899aabbccddeeff");
    ASSERT_TRUE(runUntil([&] {
        const auto r = reads(web);
        return std::ranges::count(
                   r, QStringLiteral("(HARDWARIO Monitor: the command was refused: "
                                     "it changes the device's keys or identity.)")) == 4 &&
               std::ranges::count(r, QStringLiteral("config secret-key <redacted>")) == 1;
    }));
    for (const QString& read : reads(web)) {
        EXPECT_FALSE(read.contains(QStringLiteral("00112233"))) << read.toStdString();
    }
    EXPECT_TRUE(console.hasEvent(
        "a command that changes the device's keys or identity was refused (config secret-key "
        "<redacted>)"));
    EXPECT_TRUE(console.hasEvent(
        "a command that changes the device's keys or identity was refused (settings  erase)"));
    // The joiner (refused-lines cap) gets a marker per refused line; the web Terminal no tunnel
    // type it does not know (its at events are only state / write / read / hwio, and the hwio ones
    // decode in rc.4: checked above for the hello; line-refused below is the cap-gated one).
    ASSERT_TRUE(runUntil([&] {
        return std::ranges::count_if(joiner.log().linesSince(0), [](const device::ConsoleLine& l) {
                   return l.dir == device::Direction::Marker &&
                          l.text.find("The customer's Monitor refused this command") !=
                              std::string::npos;
               }) == 4;
    }));
    EXPECT_TRUE(hasMarker(joiner.log(), "it changes the device's keys or identity"));
}

// Without the refused-lines cap in the last joiner hello (an rc.4 joiner), no line-refused is
// sent: only the English read line.
TEST(Rc4CompatSession, NoLineRefusedWithoutTheCap) {
    FakeRelay relay;
    FakeConsole console;
    console.profileOverride = &testing::testProfile;
    ScriptedConsent consent;
    RemoteHost host(console, consent, nullptr, hostFor(relay));
    host.start();
    ASSERT_TRUE(runUntil([&] { return host.state() == HostState::Sharing; }));
    RawParticipant old(relay.url(), nsp);
    ASSERT_TRUE(old.waitConnected());
    ASSERT_TRUE(runUntil([&] { return !tunnelAts(old).empty(); }));
    old.emitEvent(QStringLiteral("write"), rc4::encodeJoiner(rc4::JoinerHello{
                                               .app = "hwio-monitor", .version = "2.0.0-rc.4"}));
    ASSERT_TRUE(runUntil([&] { return !host.peerApp().empty(); }));
    old.emitEvent(QStringLiteral("write"), QStringLiteral("settings erase"));
    ASSERT_TRUE(runUntil([&] { return !reads(old).empty(); }));
    runFor(100ms);
    EXPECT_TRUE(console.sent.empty());
    for (const QJsonObject& at : tunnelAts(old)) {
        auto decoded = rc4::decodeHost(at);
        EXPECT_TRUE(decoded.has_value()) << decoded.error().detail;
    }
}

} // namespace
} // namespace hwio::remote
