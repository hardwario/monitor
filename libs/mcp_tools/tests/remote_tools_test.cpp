// Plan 0062 (ADR-0433..0438): the MCP tools on a joined remote support tab against a
// FakeRemoteDevice - the tool matrix, support's permission (nothing but a nameless entry before
// it), the hidden view of every read path with made-up key values and a made-up session code,
// send_command over the remote link (each error and end rule, limits) and the remote operations
// (consent every time, progress, stop waiting). Limits are shrunk instead of a fake clock.
//
// Every key value, token and session code below is made up for the tests.

#include "hwio/core/error.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_redaction.h"
#include "hwio/device/session_events.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/limits.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/remote_hidden_view.h"
#include "mcp_test_support.h"
#include "tools_test_support.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <expected>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hwio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;
using hwio::device::Channel;
using hwio::device::Direction;
using hwio::mcp::ConsentCategory;
using hwio::mcp::test::FakeConsentBroker;

// Made-up values (never real keys or codes).
constexpr const char* fakeAppKey = "0F1E2D3C4B5A69788796A5B4C3D2E1F0";
constexpr const char* fakeToken = "MADEUP-CLAIM-TOKEN-7731";
constexpr const char* fakeCode = "482913";
constexpr const char* fakeCodeGrouped = "482 913";
constexpr const char* fakeRelay = "wss://relay.invalid.example/x";

const QString remoteId = QStringLiteral("remote:1");

hwio::mcp::Limits fastLimits() {
    hwio::mcp::Limits limits;
    limits.tools.remoteCommandSilence = 80ms;
    limits.tools.remoteEchoTimeout = 150ms;
    limits.tools.remoteCommandDefaultTimeout = 400ms;
    limits.resourceUpdateInterval = 10ms;
    return limits;
}

QString compact(const QJsonObject& json) {
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

QJsonObject resultOf(const QJsonObject& response) {
    return response.value(QStringLiteral("result")).toObject();
}

// Fails when `text` holds a secret value or the session code (any form).
void expectNoSecret(const QString& text, const char* where) {
    for (const char* secret : {fakeAppKey, fakeToken, fakeCode, fakeCodeGrouped, fakeRelay}) {
        EXPECT_FALSE(text.contains(QString::fromLatin1(secret))) << where << ": " << secret;
    }
}

struct Rig {
    explicit Rig(const hwio::mcp::Limits& limits = fastLimits())
        : remote(&provider.addRemote("remote:1")), harness(provider, limits) {}

    FakeProvider provider;
    FakeRemoteDevice* remote;
    Harness harness;

    FakeConsoleSession& session() { return provider.session("remote:1"); }
    void allow() const { remote->allowed.insert("tester"); }
    PendingCall call(const char* tool, QJsonObject arguments = {},
                     std::chrono::milliseconds timeout = 5s) {
        arguments.insert(QStringLiteral("device"), remoteId);
        return harness.call(QString::fromLatin1(tool), arguments, timeout);
    }
    QJsonObject read(const QString& uri) {
        return harness.rpc(QStringLiteral("resources/read"), {{QStringLiteral("uri"), uri}});
    }
    QString readText(const QString& uri) {
        return resultOf(read(uri))
            .value(QStringLiteral("contents"))
            .toArray()
            .at(0)
            .toObject()
            .value(QStringLiteral("text"))
            .toString();
    }
    // Every activity entry, marker and the console log as one text.
    QString recorded() {
        QString text;
        for (const auto& entry : harness.activity.entries) {
            text += QString::fromStdString(entry.arguments + " " + entry.deviceId + "\n");
        }
        for (const QString& marker : provider.record().markerTexts()) {
            text += marker + QLatin1Char('\n');
        }
        return text;
    }
};

QJsonObject remoteEntry(const PendingCall& call) {
    for (const auto& item : call.structured().value(QStringLiteral("devices")).toArray()) {
        if (item.toObject().value(QStringLiteral("id")) == remoteId) {
            return item.toObject();
        }
    }
    return {};
}

// ---- the hidden view ----------------------------------------------------------------------------

TEST(RemoteHiddenView, RedactsAndReplacesTheCode) {
    const RemoteHiddenView view(hwio::device::ConsoleRedaction("chester", "app"), "482 913");
    hwio::device::ConsoleLine line;
    line.channel = Channel::Shell;
    line.dir = Direction::In;
    line.text = std::string("lrw config appkey ") + fakeAppKey;
    EXPECT_EQ(view.text(line), "lrw config appkey <redacted>");
    line.dir = Direction::Out;
    line.text = std::string("claim token: ") + fakeToken;
    EXPECT_EQ(view.text(line), "claim token: <redacted>");
    line.dir = Direction::Marker;
    line.text = "Session 482 913 — web Terminal; code 482913, not 4829130 or 1482913";
    line.textEn = line.text;
    EXPECT_EQ(view.text(line),
              "Session <session> — web Terminal; code <session>, not 4829130 or 1482913");
    EXPECT_EQ(view.scrub("x482913y"), "x<session>y");
    // No code: nothing replaced.
    EXPECT_EQ(RemoteHiddenView(hwio::device::ConsoleRedaction("chester", "app"), "").scrub("12"),
              "12");
}

// ---- support's permission (ADR-0436) ------------------------------------------------------------

TEST(RemoteTools, NothingButANamelessEntryBeforeTheGrant) {
    Rig rig;
    rig.session().shell(std::string("claim token: ") + fakeToken);
    rig.session().logLine("<inf> app: started");

    const PendingCall list = rig.harness.call(QStringLiteral("list_devices"));
    const QJsonObject entry = remoteEntry(list);
    ASSERT_FALSE(entry.isEmpty());
    EXPECT_EQ(entry.value(QStringLiteral("kind")), QStringLiteral("remote"));
    EXPECT_EQ(entry.value(QStringLiteral("display_name")),
              QStringLiteral("Remote support session"));
    EXPECT_TRUE(entry.value(QStringLiteral("profile")).isNull());
    EXPECT_TRUE(entry.value(QStringLiteral("connection")).isNull());
    const QJsonObject block = entry.value(QStringLiteral("remote")).toObject();
    EXPECT_EQ(block.value(QStringLiteral("owner")), QStringLiteral("customer"));
    EXPECT_EQ(block.value(QStringLiteral("secrets")), QStringLiteral("hidden"));
    EXPECT_FALSE(block.contains(QStringLiteral("join")));
    EXPECT_FALSE(compact(list.structured()).contains(QStringLiteral("CHESTER-M")));

    // The devices resource, the status / log / terminal resources, the list, status-all.
    const QJsonObject devices =
        QJsonDocument::fromJson(rig.readText(QStringLiteral("hwio://devices")).toUtf8()).object();
    EXPECT_FALSE(compact(devices).contains(QStringLiteral("CHESTER-M")));
    for (const char* part : {"status", "log", "terminal"}) {
        const QJsonObject response =
            rig.read(QStringLiteral("hwio://device/remote:1/") + QString::fromLatin1(part));
        EXPECT_TRUE(response.contains(QStringLiteral("error"))) << part;
        EXPECT_FALSE(response.contains(QStringLiteral("result"))) << part;
        EXPECT_TRUE(compact(response).contains(QStringLiteral("mcp.consent_denied"))) << part;
        expectNoSecret(compact(response), part);
    }
    const QString resources = compact(resultOf(rig.harness.rpc(QStringLiteral("resources/list"))));
    EXPECT_TRUE(resources.contains(QStringLiteral("Remote support session status")));
    EXPECT_FALSE(resources.contains(QStringLiteral("CHESTER-M")));
    const PendingCall status = rig.harness.call(QStringLiteral("status"));
    const QJsonArray statuses = status.structured().value(QStringLiteral("devices")).toArray();
    ASSERT_EQ(statuses.size(), 1);
    EXPECT_EQ(statuses.at(0).toObject(),
              (QJsonObject{{QStringLiteral("id"), remoteId}, {QStringLiteral("remote"), true}}));

    // The prompt has no device argument and carries nothing of the device.
    const QString prompt = compact(
        resultOf(rig.harness.rpc(QStringLiteral("prompts/get"),
                                 {{QStringLiteral("name"), QStringLiteral("debug_device")}})));
    EXPECT_FALSE(prompt.contains(QStringLiteral("remote:1")));
    EXPECT_FALSE(prompt.contains(QStringLiteral("CHESTER-M 2159018247")));
    expectNoSecret(prompt, "prompt");
}

TEST(RemoteTools, ResourceUpdatesOnlyAfterTheGrant) {
    Rig rig;
    auto listen = std::make_shared<hwio::mcp::test::RecordingReplyChannel>();
    hwio::mcp::TransportInfo http;
    http.kind = hwio::mcp::TransportKind::Http;
    http.connectionId = 99;
    http.protocolVersionHeader = "2026-07-28";
    http.methodHeader = "subscriptions/listen";
    rig.harness.server->handle(
        request(QStringLiteral("L1"), QStringLiteral("subscriptions/listen"),
                {{QStringLiteral("notifications"),
                  QJsonObject{{QStringLiteral("resourceSubscriptions"),
                               QJsonArray{QStringLiteral("hwio://device/remote:1/log")}}}}}),
        listen, http);
    ASSERT_TRUE(listen->streamStarted);
    const auto updates = [&listen] {
        int n = 0;
        for (const QJsonObject& message : listen->notifications) {
            n += message.value(QStringLiteral("method")) ==
                         QStringLiteral("notifications/resources/updated")
                     ? 1
                     : 0;
        }
        return n;
    };
    rig.session().logLine("<inf> before the grant");
    runFor(100ms);
    EXPECT_EQ(updates(), 0);
    rig.allow();
    rig.session().logLine("<inf> after the grant");
    EXPECT_TRUE(runUntil([&] { return updates() >= 1; }, 2000ms));
    listen->close();
}

TEST(RemoteTools, FirstCallAsksSupportOnceAndOnlyAPromptCounts) {
    Rig rig;
    rig.session().shell("hello");
    rig.harness.consent.mode = FakeConsentBroker::Mode::Deny;
    const PendingCall denied = rig.call("read_terminal");
    ASSERT_TRUE(denied.done());
    EXPECT_TRUE(denied.isError());
    ASSERT_EQ(rig.harness.consent.requests.size(), 1U);
    const auto& request = rig.harness.consent.requests.front();
    EXPECT_EQ(request.categories, hwio::mcp::ConsentCategories(ConsentCategory::RemoteDevice));
    EXPECT_TRUE(request.remoteDevice);
    EXPECT_EQ(request.deviceId, "remote:1");
    expectNoSecret(compact(denied.structured()), "denied");

    // A remembered answer (a rule or a client grant) does not count for a customer's device.
    rig.harness.consent.mode = FakeConsentBroker::Mode::Allow;
    rig.harness.consent.allowedAs = hwio::mcp::ConsentGrant::Remembered;
    const PendingCall remembered = rig.call("read_terminal");
    EXPECT_EQ(remembered.errorCode(), "mcp.consent_denied");
    EXPECT_TRUE(rig.remote->allowed.empty());

    rig.harness.consent.allowedAs = hwio::mcp::ConsentGrant::Prompted;
    const PendingCall allowed = rig.call("read_terminal");
    ASSERT_FALSE(allowed.isError()) << compact(allowed.response()).toStdString();
    EXPECT_TRUE(allowed.structured().value(QStringLiteral("remote")).toBool());
    EXPECT_EQ(rig.remote->allowed, std::set<std::string>{"tester"});
    const std::size_t asked = rig.harness.consent.requests.size();
    const PendingCall again = rig.call("read_log");
    EXPECT_FALSE(again.isError());
    EXPECT_EQ(rig.harness.consent.requests.size(), asked); // the session grant covers it

    // After the grant: the full entry.
    const QJsonObject entry = remoteEntry(rig.harness.call(QStringLiteral("list_devices")));
    EXPECT_EQ(entry.value(QStringLiteral("display_name")),
              QStringLiteral("CHESTER-M 2159018247 — remote"));
    EXPECT_EQ(entry.value(QStringLiteral("profile")), QStringLiteral("chester"));
    const QJsonObject block = entry.value(QStringLiteral("remote")).toObject();
    EXPECT_EQ(block.value(QStringLiteral("host")), QStringLiteral("monitor"));
    EXPECT_EQ(block.value(QStringLiteral("join")), QStringLiteral("allowed"));
    EXPECT_TRUE(block.value(QStringLiteral("commands")).toBool());
    EXPECT_EQ(block.value(QStringLiteral("secrets")), QStringLiteral("hidden"));
    const QJsonArray operations = block.value(QStringLiteral("operations")).toArray();
    ASSERT_EQ(operations.size(), 2);
    EXPECT_EQ(operations.at(0).toObject().value(QStringLiteral("kinds")),
              (QJsonArray{QStringLiteral("flash"), QStringLiteral("reset")}));
    EXPECT_EQ(operations.at(1).toObject().value(QStringLiteral("kinds")),
              (QJsonArray{QStringLiteral("lteUpdate")}));
    EXPECT_TRUE(block.value(QStringLiteral("note"))
                    .toString()
                    .contains(QStringLiteral("customer's device")));
}

TEST(RemoteTools, StopAiEndsRunningCallsWithCancelled) {
    Rig rig;
    rig.allow();
    PendingCall wait = rig.harness.start(
        QStringLiteral("wait_for_state"),
        {{QStringLiteral("device"), remoteId}, {QStringLiteral("pattern"), QStringLiteral("x")}});
    runFor(20ms);
    ASSERT_FALSE(wait.done());
    rig.remote->revoke("tester");
    ASSERT_TRUE(runUntil([&] { return wait.done(); }, 2000ms));
    EXPECT_EQ(wait.errorCode(), "cancelled");
    EXPECT_TRUE(rig.remote->allowed.empty());
}

// ---- the tool matrix (ADR-0434) -----------------------------------------------------------------

// Every tool of the server has a row here (the test fails for a new tool without one).
const std::map<QString, std::string>& matrix() {
    static const std::map<QString, std::string> rows = {
        {QStringLiteral("list_devices"), "ok"},
        {QStringLiteral("status"), "ok"},
        {QStringLiteral("read_terminal"), "ok"},
        {QStringLiteral("read_log"), "ok"},
        {QStringLiteral("wait_for_state"), "ok"},
        {QStringLiteral("wait_for_connection"), "ok"},
        {QStringLiteral("send_command"), "ok"},
        {QStringLiteral("flash"), "ok"},
        {QStringLiteral("flash_lte"), "ok"},
        {QStringLiteral("reset"), "ok"},
        {QStringLiteral("connect"), "mcp.tool_not_allowed"},
        {QStringLiteral("disconnect"), "mcp.tool_not_allowed"},
        {QStringLiteral("reconnect"), "mcp.tool_not_allowed"},
        {QStringLiteral("erase"), "mcp.capability_missing"},
        {QStringLiteral("recover"), "mcp.capability_missing"},
        {QStringLiteral("stop"), "mcp.capability_missing"},
        {QStringLiteral("start"), "mcp.capability_missing"},
        {QStringLiteral("release_probe"), "mcp.capability_missing"},
        {QStringLiteral("reacquire_probe"), "mcp.capability_missing"},
        {QStringLiteral("target_status"), "mcp.capability_missing"},
        {QStringLiteral("halt"), "mcp.capability_missing"},
        {QStringLiteral("go"), "mcp.capability_missing"},
        {QStringLiteral("read_memory"), "mcp.capability_missing"},
        {QStringLiteral("read_registers"), "mcp.capability_missing"},
        {QStringLiteral("memory_zones"), "mcp.capability_missing"},
        {QStringLiteral("read_pib"), "mcp.capability_missing"},
        {QStringLiteral("read_uicr"), "mcp.capability_missing"},
        {QStringLiteral("write_memory"), "mcp.capability_missing"},
        {QStringLiteral("write_flash"), "mcp.capability_missing"},
    };
    return rows;
}

TEST(RemoteTools, EveryToolHasItsRemoteAnswer) {
    Rig rig; // no grant: a refused tool answers without asking support
    const QJsonArray tools = resultOf(rig.harness.rpc(QStringLiteral("tools/list")))
                                 .value(QStringLiteral("tools"))
                                 .toArray();
    ASSERT_FALSE(tools.isEmpty());
    // Arguments the schemas require besides `device`.
    const QJsonObject extra{{QStringLiteral("address"), QStringLiteral("0x20000000")},
                            {QStringLiteral("data_hex"), QStringLiteral("00")}};
    for (const auto& item : tools) {
        const QString name = item.toObject().value(QStringLiteral("name")).toString();
        const auto row = matrix().find(name);
        ASSERT_NE(row, matrix().end()) << "no matrix row for " << name.toStdString();
        if (row->second == "ok") {
            continue; // covered by the tests below
        }
        const QJsonObject properties = item.toObject()
                                           .value(QStringLiteral("inputSchema"))
                                           .toObject()
                                           .value(QStringLiteral("properties"))
                                           .toObject();
        QJsonObject arguments;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            if (properties.contains(it.key())) {
                arguments.insert(it.key(), it.value());
            }
        }
        arguments.insert(QStringLiteral("device"), remoteId);
        const PendingCall call = rig.harness.call(name, arguments, 2s);
        ASSERT_TRUE(call.done()) << name.toStdString();
        EXPECT_EQ(call.rpcError(), 0)
            << name.toStdString() << " " << compact(call.response()).toStdString();
        EXPECT_EQ(call.errorCode(), row->second) << name.toStdString();
        expectNoSecret(compact(call.structured()), "matrix");
    }
    EXPECT_EQ(tools.size(), static_cast<qsizetype>(matrix().size()));
    // A refused tool asks nothing (no prompt for a tool that cannot run anyway).
    EXPECT_TRUE(rig.harness.consent.requests.empty());
    EXPECT_TRUE(rig.remote->lines.empty());
    EXPECT_TRUE(rig.remote->operations.empty());
}

TEST(RemoteTools, AmbiguousWithALocalAndARemoteDevice) {
    Rig rig;
    rig.provider.add("jlink:1");
    const PendingCall call = rig.harness.call(QStringLiteral("read_log"));
    EXPECT_EQ(call.errorCode(), "mcp.device_ambiguous");
}

// ---- the hidden view of every read path (ADR-0437) ----------------------------------------------

// The customer typed a key, the device printed a token, a marker and a line hold the code.
void fillSecrets(Rig& rig) {
    rig.session().emitLine(Channel::Shell, Direction::In,
                           std::string("lrw config appkey ") + fakeAppKey);
    rig.session().shell(std::string("claim token: ") + fakeToken);
    rig.session().mutableLog().appendMarker(Channel::Shell, std::string("— Joined session ") +
                                                                fakeCodeGrouped + " —");
    rig.session().shell(std::string("code ") + fakeCode + " typed by the customer");
    rig.session().emitLine(Channel::Log, Direction::Out, std::string("<inf> session ") + fakeCode);
}

TEST(RemoteTools, EveryReadPathShowsTheHiddenView) {
    Rig rig;
    rig.allow();
    fillSecrets(rig);

    const PendingCall terminal = rig.call("read_terminal");
    ASSERT_FALSE(terminal.isError());
    const QString terminalText = compact(terminal.structured());
    expectNoSecret(terminalText, "read_terminal");
    EXPECT_TRUE(terminalText.contains(QStringLiteral("lrw config appkey <redacted>")));
    EXPECT_TRUE(terminalText.contains(QStringLiteral("claim token: <redacted>")));
    EXPECT_TRUE(terminalText.contains(QStringLiteral("Joined session <session>")));
    EXPECT_TRUE(terminalText.contains(QStringLiteral("code <session> typed")));
    EXPECT_TRUE(terminal.structured().value(QStringLiteral("remote")).toBool());

    const PendingCall cursor = rig.call("read_terminal", {{QStringLiteral("after_cursor"), 0}});
    expectNoSecret(compact(cursor.structured()), "read_terminal after_cursor");
    const PendingCall log = rig.call("read_log");
    expectNoSecret(compact(log.structured()), "read_log");
    EXPECT_TRUE(compact(log.structured()).contains(QStringLiteral("<inf> session <session>")));

    // A pattern cannot test for a hidden value.
    for (const QString& pattern :
         {QStringLiteral("482913"), QStringLiteral("MADEUP"), QStringLiteral("482 913")}) {
        const PendingCall found = rig.call("read_log", {{QStringLiteral("pattern"), pattern}});
        EXPECT_TRUE(found.structured().value(QStringLiteral("lines")).toArray().isEmpty())
            << pattern.toStdString();
        const PendingCall after = rig.call("read_log", {{QStringLiteral("pattern"), pattern},
                                                        {QStringLiteral("after_cursor"), 0}});
        EXPECT_TRUE(after.structured().value(QStringLiteral("lines")).toArray().isEmpty());
    }
    const PendingCall placeholder =
        rig.call("read_log", {{QStringLiteral("pattern"), QStringLiteral("<session>")}});
    EXPECT_EQ(placeholder.structured().value(QStringLiteral("lines")).toArray().size(), 1);

    // Resources.
    for (const char* part : {"terminal", "log", "status"}) {
        const QString text =
            rig.readText(QStringLiteral("hwio://device/remote:1/") + QString::fromLatin1(part));
        EXPECT_FALSE(text.isEmpty()) << part;
        expectNoSecret(text, part);
    }
    expectNoSecret(rig.readText(QStringLiteral("hwio://devices")), "devices");
    expectNoSecret(compact(resultOf(rig.harness.rpc(QStringLiteral("resources/list")))),
                   "resources/list");
    expectNoSecret(compact(rig.harness.call(QStringLiteral("status")).structured()), "status");
    expectNoSecret(compact(rig.harness.call(QStringLiteral("list_devices")).structured()),
                   "list_devices");
}

TEST(RemoteTools, AWaitConditionCannotMatchAHiddenValue) {
    Rig rig;
    rig.allow();
    PendingCall token = rig.harness.start(
        QStringLiteral("wait_for_state"),
        {{QStringLiteral("device"), remoteId},
         {QStringLiteral("pattern"), QStringLiteral("MADEUP|482913|482 913|appkey 0F")},
         {QStringLiteral("source"), QStringLiteral("both")},
         {QStringLiteral("timeout"), 1}});
    runFor(20ms);
    fillSecrets(rig);
    ASSERT_TRUE(runUntil([&] { return token.done(); }, 3000ms));
    EXPECT_EQ(token.structured().value(QStringLiteral("status")), QStringLiteral("timeout"));
    expectNoSecret(compact(token.structured()), "wait_for_state");

    PendingCall placeholder = rig.harness.start(
        QStringLiteral("wait_for_state"), {{QStringLiteral("device"), remoteId},
                                           {QStringLiteral("pattern"), QStringLiteral("<session>")},
                                           {QStringLiteral("source"), QStringLiteral("terminal")},
                                           {QStringLiteral("timeout"), 2}});
    runFor(20ms);
    rig.session().shell(std::string("code ") + fakeCode);
    ASSERT_TRUE(runUntil([&] { return placeholder.done(); }, 3000ms));
    EXPECT_EQ(placeholder.structured().value(QStringLiteral("matched_line")),
              QStringLiteral("code <session>"));
}

TEST(RemoteTools, TheSessionCodeIsInNoResultMarkerOrActivityEntry) {
    Rig rig;
    rig.allow();
    fillSecrets(rig);
    // The client itself types the code: the activity entry and the result show it hidden.
    const PendingCall sent =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("echo 482913")}});
    ASSERT_FALSE(sent.isError()) << compact(sent.response()).toStdString();
    EXPECT_EQ(rig.remote->lines, std::vector<std::string>{"echo 482913"}); // sent unchanged
    EXPECT_EQ(sent.structured().value(QStringLiteral("command")), QStringLiteral("echo <session>"));
    PendingCall waited =
        rig.harness.start(QStringLiteral("wait_for_state"),
                          {{QStringLiteral("device"), remoteId},
                           {QStringLiteral("pattern"), QStringLiteral("x")},
                           {QStringLiteral("command"), QStringLiteral("show 482 913")},
                           {QStringLiteral("poll_interval"), 5},
                           {QStringLiteral("timeout"), 1}});
    ASSERT_TRUE(runUntil([&] { return waited.done(); }, 3000ms));
    for (const PendingCall* call : {&sent, static_cast<const PendingCall*>(&waited)}) {
        expectNoSecret(compact(call->response()), "result");
    }
    expectNoSecret(rig.recorded(), "activity and markers");
    EXPECT_TRUE(rig.recorded().contains(QStringLiteral("wait_for_state \"show <session>\"")));
}

TEST(RemoteTools, EveryAllowedToolIsDeniedWithoutTheGrant) {
    Rig rig;
    rig.session().shell(std::string("claim token: ") + fakeToken);
    rig.harness.consent.mode = FakeConsentBroker::Mode::Deny;
    // Arguments the allowed tools require besides `device`.
    const std::map<QString, QJsonObject> required{
        {QStringLiteral("send_command"), {{QStringLiteral("command"), QStringLiteral("version")}}},
        {QStringLiteral("wait_for_state"), {{QStringLiteral("pattern"), QStringLiteral("x")}}},
        {QStringLiteral("flash"), {{QStringLiteral("catalog_id"), QString(32, QLatin1Char('c'))}}}};
    std::size_t asked = 0;
    for (const auto& [name, answer] : matrix()) {
        if (answer != "ok" || name == QStringLiteral("list_devices")) {
            continue;
        }
        const auto extra = required.find(name);
        QJsonObject arguments = extra == required.end() ? QJsonObject() : extra->second;
        arguments.insert(QStringLiteral("device"), remoteId);
        const PendingCall call = rig.harness.call(name, arguments, 2s);
        ASSERT_TRUE(call.done()) << name.toStdString();
        EXPECT_TRUE(call.isError()) << name.toStdString();
        EXPECT_EQ(call.errorCode(), "mcp.tool_not_allowed") << name.toStdString(); // Deny mode
        expectNoSecret(compact(call.response()), "denied");
        ++asked;
        EXPECT_EQ(rig.harness.consent.requests.size(), asked) << name.toStdString();
        EXPECT_TRUE(
            rig.harness.consent.requests.back().categories.testFlag(ConsentCategory::RemoteDevice));
    }
    EXPECT_TRUE(rig.remote->lines.empty());
    EXPECT_TRUE(rig.remote->operations.empty());
}

TEST(RemoteTools, AConnectionErrorShowsNeitherTheRelayNorTheCode) {
    Rig rig;
    rig.allow();
    rig.session().setConnection(hwio::device::ConnectionState::Disconnected,
                                makeError(ErrorCode::RemoteRelayUnreachable,
                                          std::string(fakeRelay) + " session " + fakeCode));
    for (const char* tool : {"status", "list_devices"}) {
        const PendingCall call = rig.harness.call(QString::fromLatin1(tool));
        EXPECT_TRUE(compact(call.structured()).contains(QStringLiteral("remote.relay_unreachable")))
            << tool;
        expectNoSecret(compact(call.response()), tool);
    }
    expectNoSecret(compact(rig.call("status").response()), "status remote:1");
    expectNoSecret(rig.readText(QStringLiteral("hwio://devices")), "devices");
    expectNoSecret(rig.readText(QStringLiteral("hwio://device/remote:1/status")), "status");
    const PendingCall waited = rig.call("wait_for_connection", {{QStringLiteral("timeout"), 1}});
    EXPECT_EQ(waited.structured().value(QStringLiteral("status")), QStringLiteral("timeout"));
    EXPECT_TRUE(waited.structured().value(QStringLiteral("error")).isObject());
    expectNoSecret(compact(waited.response()), "wait_for_connection");
}

TEST(RemoteTools, TheTabLeavingEndsARunningCall) {
    Rig rig;
    rig.allow();
    PendingCall wait = rig.harness.start(
        QStringLiteral("wait_for_state"),
        {{QStringLiteral("device"), remoteId}, {QStringLiteral("pattern"), QStringLiteral("x")}});
    runFor(20ms);
    ASSERT_FALSE(wait.done());
    rig.provider.dropRemoteDevice("remote:1");
    ASSERT_TRUE(runUntil([&] { return wait.done(); }, 2000ms));
    EXPECT_EQ(wait.errorCode(), "mcp.device_not_open");
}

// ---- send_command (ADR-0438) --------------------------------------------------------------------

TEST(RemoteSendCommand, SendsThroughTheRemoteDeviceAndEndsBySilence) {
    Rig rig;
    rig.allow();
    PendingCall call =
        rig.harness.start(QStringLiteral("send_command"),
                          {{QStringLiteral("device"), remoteId},
                           {QStringLiteral("command"), QStringLiteral("  info show  ")}});
    ASSERT_EQ(rig.remote->lines, std::vector<std::string>{"info show"}); // trimmed
    EXPECT_TRUE(rig.session().sent().empty()); // not ConsoleSession::sendLine
    rig.session().shell("info show");          // a shell that echoes the line itself
    rig.session().shell("vendor: HARDWARIO");
    rig.session().shell(std::string("claim token: ") + fakeToken);
    ASSERT_TRUE(runUntil([&] { return call.done(); }, 3000ms));
    ASSERT_FALSE(call.isError()) << compact(call.response()).toStdString();
    const QJsonObject result = call.structured();
    EXPECT_EQ(result.value(QStringLiteral("end")), QStringLiteral("silence"));
    EXPECT_TRUE(result.value(QStringLiteral("remote")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("output")).toArray().size(), 2);
    EXPECT_TRUE(compact(result).contains(QStringLiteral("claim token: <redacted>")));
    expectNoSecret(compact(result), "send_command");
    EXPECT_EQ(rig.provider.record().markerTexts().size(), 1U);
}

TEST(RemoteSendCommand, ThePromptRuleIsNotUsedAndTheDefaultTimeoutIsTheRemoteOne) {
    hwio::mcp::Limits limits = fastLimits();
    limits.tools.remoteCommandSilence = 5s; // only the timeout can end it
    Rig rig(limits);
    rig.allow();
    PendingCall call = rig.harness.start(
        QStringLiteral("send_command"),
        {{QStringLiteral("device"), remoteId}, {QStringLiteral("command"), QStringLiteral("x")}});
    rig.session().shell("output");
    rig.session().prompt(); // ignored
    ASSERT_TRUE(runUntil([&] { return call.done(); }, 3000ms));
    EXPECT_EQ(call.structured().value(QStringLiteral("end")), QStringLiteral("timeout"));
}

TEST(RemoteSendCommand, RefusedBeforeSending) {
    Rig rig;
    rig.allow();
    const auto refused = [&rig](const QString& command) {
        return rig.call("send_command", {{QStringLiteral("command"), command}});
    };
    EXPECT_EQ(refused(QString(513, QLatin1Char('a'))).errorCode(), "usage.invalid_argument");
    EXPECT_EQ(refused(QStringLiteral("a\tb")).errorCode(), "usage.invalid_argument");
    const PendingCall secret =
        refused(QStringLiteral("lrw config appkey ") + QString::fromLatin1(fakeAppKey));
    EXPECT_EQ(secret.errorCode(), "remote.command_refused");
    expectNoSecret(compact(secret.response()), "refused secret");
    expectNoSecret(rig.recorded(), "refused secret activity");
    EXPECT_TRUE(rig.recorded().contains(QStringLiteral("lrw config appkey <redacted>")));
    rig.remote->setJoin(RemoteJoin::Waiting);
    EXPECT_EQ(refused(QStringLiteral("version")).errorCode(), "remote.waiting_for_customer");
    rig.remote->setJoin(RemoteJoin::Ended);
    EXPECT_EQ(refused(QStringLiteral("version")).errorCode(), "remote.host_ended");
    rig.remote->setJoin(RemoteJoin::Allowed);
    rig.remote->remoteInfo.commands = false;
    EXPECT_EQ(refused(QStringLiteral("version")).errorCode(), "mcp.capability_missing");
    EXPECT_TRUE(rig.remote->lines.empty()); // nothing was sent
    EXPECT_TRUE(rig.provider.record().markers.empty());
}

TEST(RemoteSendCommand, NotDeliveredRefusedAndSendErrors) {
    Rig rig;
    rig.allow();
    rig.remote->echo = false;
    const PendingCall dropped =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("version")}});
    EXPECT_EQ(dropped.errorCode(), "remote.command_not_delivered");
    const PendingCall quick =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("version")},
                                  {QStringLiteral("timeout"), 0.1}});
    EXPECT_EQ(quick.errorCode(), "remote.command_not_delivered"); // not an empty timeout
    rig.remote->refuse = true;
    const PendingCall refused =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("config reset")}});
    EXPECT_EQ(refused.errorCode(), "remote.command_refused");
    rig.remote->refuse = false;
    rig.remote->sendError = makeError(ErrorCode::RemoteHostEnded);
    const PendingCall ended =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("version")}});
    EXPECT_EQ(ended.errorCode(), "remote.host_ended");
    // Each one released the command slot and the AI hint.
    rig.remote->sendError.reset();
    rig.remote->echo = true;
    const PendingCall ok =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("version")}});
    EXPECT_FALSE(ok.isError()) << compact(ok.response()).toStdString();
    const auto& hints = rig.provider.record().hints;
    ASSERT_FALSE(hints.empty());
    EXPECT_FALSE(hints.back().second);
}

TEST(RemoteSendCommand, OneAtATimeAndARate) {
    hwio::mcp::Limits limits = fastLimits();
    limits.tools.remoteCommandsPerWindow = 2;
    limits.tools.remoteCommandWindow = 60s;
    Rig rig(limits);
    rig.allow();
    PendingCall first = rig.harness.start(
        QStringLiteral("send_command"),
        {{QStringLiteral("device"), remoteId}, {QStringLiteral("command"), QStringLiteral("a")}});
    const PendingCall busy =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("b")}});
    EXPECT_EQ(busy.errorCode(), "internal.busy");
    ASSERT_TRUE(runUntil([&] { return first.done(); }, 3000ms));
    const PendingCall second =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("c")}});
    EXPECT_FALSE(second.isError());
    const PendingCall third =
        rig.call("send_command", {{QStringLiteral("command"), QStringLiteral("d")}});
    EXPECT_EQ(third.errorCode(), "mcp.too_many_requests");
    // A polling command counts too.
    const PendingCall poll =
        rig.call("wait_for_state", {{QStringLiteral("pattern"), QStringLiteral("x")},
                                    {QStringLiteral("command"), QStringLiteral("lte state")}});
    EXPECT_EQ(poll.errorCode(), "mcp.too_many_requests");
    EXPECT_EQ(rig.remote->lines, (std::vector<std::string>{"a", "c"}));
}

TEST(RemoteSendCommand, AWaitPollingCommandIsCheckedLikeACommand) {
    Rig rig;
    rig.allow();
    const PendingCall secret = rig.call(
        "wait_for_state", {{QStringLiteral("pattern"), QStringLiteral("x")},
                           {QStringLiteral("command"), QStringLiteral("lte config password ") +
                                                           QString::fromLatin1(fakeToken)}});
    EXPECT_EQ(secret.errorCode(), "remote.command_refused");
    EXPECT_TRUE(rig.remote->lines.empty());
    expectNoSecret(rig.recorded(), "wait_for_state secret");
}

// ---- operations (D8, D12, D13) ------------------------------------------------------------------

TEST(RemoteOperations, FlashFromTheCatalogAsksEveryTimeAndReportsProgress) {
    Rig rig;
    rig.allow();
    const QString catalog = QStringLiteral("0123456789abcdef0123456789ABCDEF");
    for (int round = 0; round < 2; ++round) {
        const std::size_t asked = rig.harness.consent.requests.size();
        PendingCall flash = rig.harness.start(
            QStringLiteral("flash"),
            {{QStringLiteral("device"), remoteId}, {QStringLiteral("catalog_id"), catalog}}, true);
        ASSERT_EQ(rig.harness.consent.requests.size(), asked + 1);
        const auto& request = rig.harness.consent.requests.back();
        EXPECT_EQ(request.categories, hwio::mcp::ConsentCategories(ConsentCategory::Flash));
        EXPECT_TRUE(request.remoteDevice);
        ASSERT_EQ(rig.remote->operations.size(), static_cast<std::size_t>(round + 1));
        const auto& operation = rig.remote->operations.back();
        EXPECT_EQ(operation.request.kind, RemoteOperationKind::Flash);
        EXPECT_EQ(operation.request.target, "app");
        EXPECT_EQ(operation.request.firmwareId, catalog.toLower().toStdString());
        rig.remote->progressOperation(
            static_cast<std::size_t>(round),
            RemoteOperationProgress{.stage = RemoteOperationProgress::Stage::Running,
                                    .phase = "programming",
                                    .step = 1,
                                    .steps = 2,
                                    .done = 50,
                                    .total = 100});
        rig.remote->finishOperation(static_cast<std::size_t>(round), {});
        ASSERT_TRUE(runUntil([&] { return flash.done(); }, 2000ms));
        ASSERT_FALSE(flash.isError()) << compact(flash.response()).toStdString();
        EXPECT_EQ(flash.structured().value(QStringLiteral("status")), QStringLiteral("ok"));
        EXPECT_TRUE(flash.structured().value(QStringLiteral("remote")).toBool());
        EXPECT_GE(flash.progressCount(), 1U);
    }
    EXPECT_EQ(rig.provider.record().endBusy, 2);
}

TEST(RemoteOperations, ArgumentsAndOffers) {
    Rig rig;
    rig.allow();
    const auto flash = [&rig](QJsonObject arguments) {
        return rig.call("flash", std::move(arguments), 1s);
    };
    EXPECT_EQ(flash({{QStringLiteral("path"), QStringLiteral("/tmp/app.hex")}}).errorCode(),
              "usage.invalid_argument");
    EXPECT_EQ(flash({{QStringLiteral("prepared_id"), QStringLiteral("debug")}}).errorCode(),
              "usage.invalid_argument");
    EXPECT_EQ(flash({{QStringLiteral("catalog_id"), QString(32, QLatin1Char('a'))},
                     {QStringLiteral("target"), QStringLiteral("lte")}})
                  .errorCode(),
              "remote.target_unsupported");
    EXPECT_EQ(
        rig.call("flash_lte", {{QStringLiteral("path"), QStringLiteral("/tmp/m.zip")}}).errorCode(),
        "usage.invalid_argument");
    EXPECT_EQ(rig.call("reset", {{QStringLiteral("halt"), true}}).errorCode(),
              "usage.invalid_argument");
    EXPECT_TRUE(rig.remote->operations.empty());

    PendingCall prepared = rig.harness.start(
        QStringLiteral("flash"), {{QStringLiteral("device"), remoteId},
                                  {QStringLiteral("prepared_id"), QStringLiteral("release")}});
    ASSERT_EQ(rig.remote->operations.size(), 1U);
    EXPECT_EQ(rig.remote->operations.back().request.prepared, std::string("release"));
    rig.remote->finishOperation(0, std::unexpected(makeError(ErrorCode::RemoteOperationDenied)));
    ASSERT_TRUE(runUntil([&] { return prepared.done(); }, 2000ms));
    EXPECT_EQ(prepared.errorCode(), "remote.operation_denied");

    PendingCall lte = rig.harness.start(QStringLiteral("flash_lte"),
                                        {{QStringLiteral("device"), remoteId}}, true);
    ASSERT_EQ(rig.remote->operations.size(), 2U);
    EXPECT_EQ(rig.remote->operations.back().request.kind, RemoteOperationKind::LteUpdate);
    rig.remote->progressOperation(
        1, RemoteOperationProgress{.stage = RemoteOperationProgress::Stage::WaitingForSupport});
    rig.remote->finishOperation(1, {});
    ASSERT_TRUE(runUntil([&] { return lte.done(); }, 2000ms));
    EXPECT_EQ(lte.structured().value(QStringLiteral("operation")), QStringLiteral("lteUpdate"));

    PendingCall reset =
        rig.harness.start(QStringLiteral("reset"), {{QStringLiteral("device"), remoteId}});
    ASSERT_EQ(rig.remote->operations.size(), 3U);
    EXPECT_EQ(rig.remote->operations.back().request.kind, RemoteOperationKind::Reset);
    rig.remote->finishOperation(2, {});
    ASSERT_TRUE(runUntil([&] { return reset.done(); }, 2000ms));
    EXPECT_FALSE(reset.structured().value(QStringLiteral("halted")).toBool());

    rig.remote->setJoin(RemoteJoin::Waiting);
    EXPECT_EQ(rig.call("reset").errorCode(), "remote.waiting_for_customer");
}

TEST(RemoteOperations, ARememberedGrantDoesNotCoverAnOperation) {
    Rig rig;
    rig.allow();
    rig.harness.consent.allowedAs = hwio::mcp::ConsentGrant::Remembered;
    const PendingCall reset = rig.call("reset");
    EXPECT_EQ(reset.errorCode(), "mcp.consent_denied");
    EXPECT_TRUE(rig.remote->operations.empty());
}

TEST(RemoteOperations, CancelStopsWaiting) {
    Rig rig;
    rig.allow();
    PendingCall flash = rig.harness.start(
        QStringLiteral("flash"), {{QStringLiteral("device"), remoteId},
                                  {QStringLiteral("catalog_id"), QString(32, QLatin1Char('b'))}});
    ASSERT_EQ(rig.remote->operations.size(), 1U);
    ASSERT_TRUE(rig.provider.record().busyCancel);
    rig.provider.record().busyCancel(); // the busy page's button
    ASSERT_TRUE(runUntil([&] { return flash.done(); }, 2000ms));
    EXPECT_EQ(flash.errorCode(), "cancelled");
    EXPECT_TRUE(flash.errorMessage().find("continues on the customer's device") !=
                std::string::npos);
    EXPECT_TRUE(rig.remote->operations.front().detached);
    EXPECT_EQ(rig.provider.record().endBusy, 1);
    const auto markers = rig.provider.record().markerTexts();
    ASSERT_FALSE(markers.empty());
    EXPECT_TRUE(markers.back().contains(QStringLiteral("cancelled")));
}

// ---- local devices keep their rules -------------------------------------------------------------

TEST(RemoteTools, LocalFlashArgumentsStay) {
    FakeProvider provider;
    Harness harness(provider);
    provider.add("jlink:1");
    EXPECT_EQ(harness
                  .call(QStringLiteral("flash"),
                        {{QStringLiteral("prepared_id"), QStringLiteral("release")}})
                  .errorCode(),
              "usage.invalid_argument");
    EXPECT_EQ(harness.call(QStringLiteral("flash_lte")).errorCode(), "mcp.capability_missing");
}

} // namespace
} // namespace hwio::mcp::tools::test
