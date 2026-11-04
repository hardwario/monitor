// Plan 0014 A4 and C1: the McpController (off -> listening -> error (port in use) -> retry, a port
// change restarts, Regenerate replaces the token, disable while a call runs, a token file error,
// shutdown, active clients) and the GUI integration test against the controller's real HTTP
// server with an in-process client and the fake J-Link library: token / Origin / Host refusals,
// list_devices, connect -> prompt -> Allow once -> AI tab, flash on the tab -> prompt -> markers,
// busy, activity and result, Deny, disconnect of a user's tab, two clients, and 32 in-flight
// wait_for_state calls with the event loop staying responsive.

#include "gui_test_support.h"
#include "hex_builder.h"
#include "hwio/device/console_line.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/server.h"
#include "hwio/mcp/token.h"
#include "mcp/activity_model.h"
#include "mcp/consent_view_model.h"
#include "mcp/gui_device_provider.h"
#include "mcp/mcp_controller.h"
#include "mcp_gui_test_support.h"
#include "mcp_test_support.h" // libs/mcp/tests: RecordingReplyChannel
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTcpServer>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtTypes>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using hwio::gui::DeviceTabViewModel;
using hwio::gui::McpController;
using hwio::gui::test::freePort;
using hwio::gui::test::HttpCall;
using hwio::gui::test::HttpResponse;
using hwio::gui::test::spin;
using hwio::gui::test::toolRequest;
using hwio::gui::test::waitFor;
using namespace std::chrono_literals;

class McpControllerTest : public hwio::gui::test::McpGuiTest {
protected:
    int status(const QByteArray& request) {
        HttpCall http(mcp().listeningPort(), request);
        return http.wait(10s).status;
    }

    // The value of the shown prompt's detail row `label` ("" when none).
    QString detailValue(const QString& label) {
        for (const QVariant& row : mcp().consent()->details()) {
            if (row.toMap().value(QStringLiteral("label")).toString() == label) {
                return row.toMap().value(QStringLiteral("value")).toString();
            }
        }
        return {};
    }

    // Waits for the consent prompt of `tool`.
    bool prompted(const char* tool) {
        return waitFor([this, tool] {
            const auto* prompt = mcp().broker().current();
            return prompt != nullptr && prompt->request.tool == tool;
        });
    }

    [[nodiscard]] QString hexFile(const QString& name) const {
        std::vector<hwio::firmware::Segment> segments;
        segments.push_back(hwio::firmware::Segment{
            .address = 0, .data = std::vector<std::byte>(4096, std::byte{0x5A})});
        const auto image = hwio::firmware::FirmwareImage::fromSegments(std::move(segments));
        const QString path =
            QString::fromStdU16String(root().u16string()) + QLatin1Char('/') + name;
        QFile file(path);
        if (image && file.open(QIODevice::WriteOnly)) {
            file.write(QByteArray::fromStdString(hwio::firmware::test::toHex(*image)));
        }
        return path;
    }
};

// ---- A4: lifecycle
// -------------------------------------------------------------------------------

TEST_F(McpControllerTest, OffListeningErrorRetry) {
    (void)app();
    EXPECT_EQ(mcp().state(), McpController::Off);
    EXPECT_FALSE(mcp().enabled());
    EXPECT_EQ(mcp().port(), McpController::defaultPort);
    EXPECT_EQ(mcp().stateText(), QStringLiteral("Off"));
    listen();
    EXPECT_EQ(mcp().stateText(), QStringLiteral("Listening"));
    EXPECT_TRUE(mcp().summary().startsWith(QStringLiteral("AI assistants: listening on port")));
    EXPECT_EQ(mcp().maskedToken().size(), 43);
    EXPECT_EQ(mcp().token().size(), 43);

    // Another program on the new port.
    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress(QHostAddress::LocalHost), 0));
    ASSERT_TRUE(app().settingsStore()->setValue(McpController::portKey,
                                                static_cast<int>(blocker.serverPort())));
    ASSERT_TRUE(waitFor([this] { return mcp().state() == McpController::Error; }));
    EXPECT_EQ(mcp().error().code(), QStringLiteral("mcp.port_in_use"));
    EXPECT_TRUE(mcp().stateText().contains(QString::number(blocker.serverPort())));
    EXPECT_FALSE(mcp().tokenError());
    blocker.close();
    mcp().retry();
    ASSERT_TRUE(waitFor([this] { return mcp().state() == McpController::Listening; }));
    EXPECT_EQ(mcp().listeningPort(), mcp().port());
    mcp().setEnabled(false);
    ASSERT_TRUE(waitFor([this] { return mcp().state() == McpController::Off; }));
    EXPECT_EQ(mcp().listeningPort(), 0);
}

TEST_F(McpControllerTest, PortChangeRestartsAndForgetsGrants) {
    (void)app();
    listen();
    const int first = mcp().listeningPort();
    // A grant of a client.
    auto ask = std::make_shared<bool>(false);
    hwio::mcp::ConsentRequest request;
    request.client = {.name = "Claude Code", .version = "1"};
    request.clientKey = "Claude Code";
    request.tool = "flash";
    request.categories = hwio::mcp::ConsentCategory::Flash;
    (void)mcp().broker().request(request,
                                 [ask](const hwio::mcp::ConsentDecision&) { *ask = true; });
    allowForClient();
    EXPECT_EQ(mcp().grants().size(), 1);

    const int second = freePort();
    mcp().setPort(second);
    EXPECT_EQ(mcp().state(), McpController::Stopping);
    ASSERT_TRUE(waitFor([&] {
        return mcp().state() == McpController::Listening && mcp().listeningPort() == second;
    }));
    EXPECT_NE(first, second);
    EXPECT_TRUE(mcp().grants().isEmpty()); // ADR-0241
    EXPECT_TRUE(mcp().address().contains(QString::number(second)));
    EXPECT_TRUE(mcp()
                    .snippets()
                    .front()
                    .toMap()
                    .value(QStringLiteral("text"))
                    .toString()
                    .contains(QString::number(second)));
    // The old port is closed.
    HttpCall old(first, toolRequest(first, token(), QStringLiteral("list_devices"), {}));
    EXPECT_EQ(old.wait(3s).status, 0);
}

TEST_F(McpControllerTest, RegenerateReplacesTheToken) {
    (void)app();
    listen();
    const std::string before = token();
    const int port = mcp().listeningPort();
    EXPECT_EQ(status(toolRequest(port, before, QStringLiteral("list_devices"), {})), 200);
    QSignalSpy tokenChanged(&mcp(), &McpController::tokenChanged);
    mcp().regenerateToken();
    EXPECT_EQ(tokenChanged.count(), 1);
    // Review M2: the server restarts with the new token.
    ASSERT_TRUE(waitFor([this] { return mcp().state() == McpController::Listening; }));
    const std::string after = token();
    EXPECT_NE(before, after);
    EXPECT_EQ(status(toolRequest(port, before, QStringLiteral("list_devices"), {})), 401);
    EXPECT_EQ(status(toolRequest(port, after, QStringLiteral("list_devices"), {})), 200);
    // The file holds the new token (shared with `hwio mcp --token-file`).
    QFile file(QString::fromStdU16String((root() / "mcp-token").u16string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_TRUE(file.readAll().trimmed().toStdString() == after);
}

TEST_F(McpControllerTest, TokenFileErrorAndRegenerate) {
    {
        QFile file(QString::fromStdU16String((root() / "mcp-token").u16string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("not a token\n");
    }
    (void)app();
    mcp().setEnabled(true);
    ASSERT_TRUE(waitFor([this] { return mcp().state() == McpController::Error; }));
    EXPECT_TRUE(mcp().tokenError());
    EXPECT_EQ(mcp().error().code(), QStringLiteral("mcp.token_file"));
    mcp().regenerateToken();
    ASSERT_TRUE(waitFor([this] { return mcp().state() == McpController::Listening; }));
}

TEST_F(McpControllerTest, DisableWhileACallRuns) {
    (void)app();
    DeviceTabViewModel& tab = *app().currentTabViewModel();
    ASSERT_TRUE(tab.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    tab.connectDevice();
    listen();
    const int port = mcp().listeningPort();
    HttpCall waiting(port, toolRequest(port, token(), QStringLiteral("wait_for_state"),
                                       {{QStringLiteral("pattern"), QStringLiteral("never-seen")},
                                        {QStringLiteral("timeout"), 60}}));
    ASSERT_TRUE(waitFor([this] { return mcp().activeClients() > 0; }, 5s));
    mcp().setEnabled(false);
    ASSERT_TRUE(waitFor([this] { return mcp().state() == McpController::Off; }, 10s));
    EXPECT_EQ(mcp().activeClients(), 0);
    // The call was cancelled: the connection was closed or answered with `cancelled`.
    (void)waitFor([&] { return waiting.done(); }, 5s);
    ASSERT_GE(mcp().activity()->count(), 1);
    EXPECT_EQ(mcp().activity()->entry(0).tool, "wait_for_state");
    EXPECT_EQ(mcp().activity()->entry(0).result, "cancelled");
}

TEST_F(McpControllerTest, ShutdownStopsTheServer) {
    (void)app();
    listen();
    const int port = mcp().listeningPort();
    mcp().shutdown();
    EXPECT_EQ(mcp().state(), McpController::Off);
    HttpCall refused(port, toolRequest(port, token(), QStringLiteral("list_devices"), {}));
    EXPECT_EQ(refused.wait(3s).status, 0);
    mcp().shutdown(); // idempotent
}

TEST_F(McpControllerTest, RulesAndActiveClients) {
    (void)app();
    EXPECT_EQ(mcp().consentRule(QStringLiteral("flash")), QStringLiteral("ask"));
    mcp().setConsentRule(QStringLiteral("flash"), true);
    EXPECT_EQ(mcp().consentRule(QStringLiteral("flash")), QStringLiteral("allow"));
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("mcp/consent/flash")).toString(),
              QStringLiteral("allow"));
    EXPECT_EQ(mcp().rules().size(), 5);
    EXPECT_FALSE(mcp()
                     .rules()
                     .at(2)
                     .toMap()
                     .value(QStringLiteral("hint"))
                     .toString()
                     .isEmpty()); // memory-write (ADR-0164)
    mcp().setConsentRule(QStringLiteral("flash"), false);

    listen();
    EXPECT_EQ(mcp().activeClients(), 0);
    EXPECT_FALSE(call(QStringLiteral("list_devices")).isError());
    EXPECT_FALSE(call(QStringLiteral("list_devices"), {}, QStringLiteral("Other")).isError());
    EXPECT_EQ(mcp().activeClients(), 2);
    // (No translator in the unit tests: the numerus form is the source text.)
    EXPECT_TRUE(mcp().stateText().startsWith(QStringLiteral("Listening · 2 client")));
    EXPECT_TRUE(mcp().summary().contains(QStringLiteral(", 2 client")));
    EXPECT_EQ(mcp().activity()->count(), 2);
}

// ---- C1: integration ---------------------------------------------------------------------------

TEST_F(McpControllerTest, RefusesWrongTokenOriginAndHost) {
    (void)app();
    listen();
    const int port = mcp().listeningPort();
    const QByteArray p = QByteArray::number(port);
    const QString tool = QStringLiteral("list_devices");
    EXPECT_EQ(status(toolRequest(port, hwio::mcp::TokenStore::generateToken(), tool, {})), 401);
    EXPECT_EQ(status(toolRequest(port, token(), tool, {}, QStringLiteral("Claude Code"),
                                 {{"Origin", "http://evil.example"}})),
              403);
    EXPECT_EQ(status(toolRequest(port, token(), tool, {}, QStringLiteral("Claude Code"),
                                 {{"Host", "example.com:" + p}})),
              421);
    EXPECT_EQ(status(toolRequest(port, token(), tool, {})), 200);
    EXPECT_EQ(mcp().activity()->count(), 1); // refusals never reach the server
}

TEST_F(McpControllerTest, ConnectFlashDenyAndDisconnect) {
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    listen();
    const int port = mcp().listeningPort();

    // list_devices: the user's tab and the scanned probe.
    const HttpResponse listed =
        call(QStringLiteral("list_devices"), {{QStringLiteral("refresh"), true}});
    ASSERT_FALSE(listed.isError()) << listed.errorCode().toStdString();
    QStringList ids;
    for (const auto device : listed.structured().value(QStringLiteral("devices")).toArray()) {
        ids.append(device.toObject().value(QStringLiteral("id")).toString());
    }
    EXPECT_TRUE(ids.contains(QStringLiteral("jlink:801000001")));
    EXPECT_TRUE(ids.contains(QStringLiteral("jlink:801000002")));

    // connect -> prompt -> Allow once -> an AI tab.
    HttpCall connecting(
        port, toolRequest(port, token(), QStringLiteral("connect"),
                          {{QStringLiteral("device"), QStringLiteral("jlink:801000002")}}));
    ASSERT_TRUE(prompted("connect"));
    EXPECT_TRUE(
        detailValue(QStringLiteral("Device")).startsWith(QStringLiteral("Open a new tab for")));
    EXPECT_EQ(app().tabs()->count(), 1); // nothing opened before the answer
    allowOnce();
    const HttpResponse connected = connecting.wait();
    ASSERT_FALSE(connected.isError()) << connected.errorCode().toStdString();
    EXPECT_FALSE(connected.structured().value(QStringLiteral("already_open")).toBool());
    DeviceTabViewModel* aiTab = mcp().provider().tabFor("jlink:801000002");
    ASSERT_NE(aiTab, nullptr);
    EXPECT_EQ(aiTab->openedBy(), QStringLiteral("Claude Code"));
    EXPECT_EQ(app().currentTab(), 0);

    // flash on the AI tab -> prompt -> Allow once -> markers, busy, activity, result.
    const QString hex = hexFile(QStringLiteral("app.hex"));
    const int activityBefore = mcp().activity()->count();
    HttpCall flashing(port,
                      toolRequest(port, token(), QStringLiteral("flash"),
                                  {{QStringLiteral("device"), QStringLiteral("jlink:801000002")},
                                   {QStringLiteral("path"), hex}}));
    ASSERT_TRUE(prompted("flash"));
    allowOnce();
    bool sawBusy = false;
    ASSERT_TRUE(waitFor(
        [&] {
            sawBusy = sawBusy || aiTab->busyPage() == QStringLiteral("ai");
            return flashing.done();
        },
        60s));
    const HttpResponse flashed = flashing.response();
    ASSERT_FALSE(flashed.isError()) << flashed.errorCode().toStdString();
    EXPECT_EQ(flashed.structured().value(QStringLiteral("status")).toString(),
              QStringLiteral("ok"));
    EXPECT_TRUE(sawBusy);
    EXPECT_FALSE(aiTab->isBusy());
    EXPECT_EQ(mcp().activity()->count(), activityBefore + 1);
    EXPECT_EQ(mcp().activity()->entry(0).tool, "flash");
    EXPECT_EQ(mcp().activity()->entry(0).result, "ok");
    std::vector<std::string> markers;
    for (const hwio::device::ConsoleLine& line : aiTab->consoleLog()->linesSince(0)) {
        if (line.dir == hwio::device::Direction::Marker) {
            markers.push_back(line.text);
        }
    }
    EXPECT_TRUE(std::ranges::any_of(markers, [](const std::string& text) {
        return text.find("AI (Claude Code): flash app.hex") != std::string::npos;
    }));
    EXPECT_TRUE(std::ranges::any_of(markers, [](const std::string& text) {
        return text.find("AI: flash done in") != std::string::npos;
    }));

    // Deny.
    HttpCall denied(port,
                    toolRequest(port, token(), QStringLiteral("flash"),
                                {{QStringLiteral("device"), QStringLiteral("jlink:801000002")},
                                 {QStringLiteral("path"), hex}}));
    ASSERT_TRUE(prompted("flash"));
    deny();
    EXPECT_EQ(denied.wait().errorCode(), QStringLiteral("mcp.consent_denied"));

    // The user's tab: connected through MCP (ADR-0242) but not closable by the AI.
    HttpCall userConnect(
        port, toolRequest(port, token(), QStringLiteral("connect"),
                          {{QStringLiteral("device"), QStringLiteral("jlink:801000001")}}));
    ASSERT_TRUE(prompted("connect"));
    EXPECT_TRUE(
        detailValue(QStringLiteral("Device")).startsWith(QStringLiteral("Connect the tab")));
    allowOnce();
    ASSERT_FALSE(userConnect.wait().isError());
    EXPECT_TRUE(user.openedBy().isEmpty());
    const HttpResponse refused =
        call(QStringLiteral("disconnect"),
             {{QStringLiteral("device"), QStringLiteral("jlink:801000001")}});
    EXPECT_EQ(refused.errorCode(), QStringLiteral("mcp.tool_not_allowed"));
    EXPECT_EQ(app().tabs()->count(), 2);
    // The AI's own tab can be disconnected (closed).
    const HttpResponse closed =
        call(QStringLiteral("disconnect"),
             {{QStringLiteral("device"), QStringLiteral("jlink:801000002")}});
    EXPECT_FALSE(closed.isError()) << closed.errorCode().toStdString();
    EXPECT_TRUE(waitFor([&] { return app().tabs()->count() == 1; }));
}

TEST_F(McpControllerTest, TwoClientsOneDeniedOneAllowed) {
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    user.connectDevice();
    listen();
    const int port = mcp().listeningPort();
    const QJsonObject args{{QStringLiteral("device"), QStringLiteral("jlink:801000001")},
                           {QStringLiteral("path"), hexFile(QStringLiteral("two.hex"))}};
    HttpCall first(port, toolRequest(port, token(), QStringLiteral("flash"), args,
                                     QStringLiteral("Client A")));
    ASSERT_TRUE(waitFor([this] { return mcp().broker().current() != nullptr; }));
    HttpCall second(port, toolRequest(port, token(), QStringLiteral("flash"), args,
                                      QStringLiteral("Client B")));
    ASSERT_TRUE(waitFor([this] { return mcp().broker().queued() == 1; }));
    EXPECT_EQ(mcp().consent()->client(), QStringLiteral("Client A"));
    EXPECT_EQ(mcp().consent()->queued(), 1);
    deny();
    ASSERT_TRUE(
        waitFor([this] { return mcp().consent()->client() == QStringLiteral("Client B"); }));
    allowForClient();
    EXPECT_EQ(first.wait(60s).errorCode(), QStringLiteral("mcp.consent_denied"));
    const HttpResponse allowed = second.wait(60s);
    EXPECT_FALSE(allowed.isError()) << allowed.errorCode().toStdString();
    ASSERT_EQ(mcp().grants().size(), 1);
    EXPECT_EQ(mcp().grants().front().toMap().value(QStringLiteral("client")).toString(),
              QStringLiteral("Client B 1")); // with the version (ADR-0241)
}

// ADR-0097: 32 calls in flight (16 over HTTP, the connection limit, and 16 handed to the server
// directly) keep the GUI event loop responsive.
TEST_F(McpControllerTest, ManyWaitingCallsKeepTheEventLoopResponsive) {
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    user.connectDevice();
    listen();
    ASSERT_TRUE(waitFor([&] { return user.state() == DeviceTabViewModel::Connected; }));
    // Lines for the scans.
    for (int i = 0; i < 5000; ++i) {
        hwio::device::ConsoleLine line;
        line.channel = hwio::device::Channel::Log;
        line.dir = hwio::device::Direction::In;
        line.text = "app: sample line " + std::to_string(i);
        user.consoleLog()->append(std::move(line));
    }
    const int port = mcp().listeningPort();
    const QJsonObject args{{QStringLiteral("device"), QStringLiteral("jlink:801000001")},
                           {QStringLiteral("pattern"), QStringLiteral("never (seen|matched) \\d+")},
                           {QStringLiteral("timeout"), 3}};
    std::vector<std::unique_ptr<HttpCall>> http;
    http.reserve(16);
    for (int i = 0; i < 16; ++i) {
        http.push_back(std::make_unique<HttpCall>(
            port, toolRequest(port, token(), QStringLiteral("wait_for_state"), args)));
    }
    std::vector<std::shared_ptr<hwio::mcp::test::RecordingReplyChannel>> direct;
    direct.reserve(16);
    for (int i = 0; i < 16; ++i) {
        auto reply = std::make_shared<hwio::mcp::test::RecordingReplyChannel>(false);
        direct.push_back(reply);
        const QByteArray body = toolRequest(port, token(), QStringLiteral("wait_for_state"), args);
        mcp().serverForTests()->handle(
            body.mid(body.indexOf("\r\n\r\n") + 4), reply,
            hwio::mcp::TransportInfo{.kind = hwio::mcp::TransportKind::Stdio,
                                     .connectionId = hwio::mcp::nextConnectionId(),
                                     .protocolVersionHeader = std::nullopt,
                                     .methodHeader = std::nullopt,
                                     .nameHeader = std::nullopt,
                                     .sessionId = std::nullopt});
    }
    // A 10 ms timer: the largest lateness while the calls wait.
    QElapsedTimer clock;
    clock.start();
    qint64 last = 0;
    qint64 worst = 0;
    QTimer ticker;
    ticker.setInterval(10);
    QObject::connect(&ticker, &QTimer::timeout, [&] {
        const qint64 now = clock.elapsed();
        if (last != 0) {
            worst = std::max(worst, now - last - 10);
        }
        last = now;
    });
    ticker.start();
    spin(1500ms);
    EXPECT_GE(mcp().serverForTests()->activeCalls(), 32U);
    ticker.stop();
    EXPECT_LT(worst, 50) << "timer latency " << worst << " ms";
    ASSERT_TRUE(waitFor(
        [&] {
            return std::ranges::all_of(http, [](const auto& c) { return c->done(); }) &&
                   std::ranges::all_of(direct, [](const auto& r) { return r->done(); });
        },
        20s));
    EXPECT_EQ(http.front()->response().structured().value(QStringLiteral("status")).toString(),
              QStringLiteral("timeout"));
}

// ---- review M2 / missing tests -----------------------------------------------------------------

TEST_F(McpControllerTest, RegenerateEndsPromptsCallsAndSessions) {
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    user.connectDevice();
    listen();
    const int port = mcp().listeningPort();
    const std::string before = token();
    HttpCall waiting(port, toolRequest(port, before, QStringLiteral("wait_for_state"),
                                       {{QStringLiteral("pattern"), QStringLiteral("never-seen")},
                                        {QStringLiteral("timeout"), 60}}));
    HttpCall flashing(port,
                      toolRequest(port, before, QStringLiteral("flash"),
                                  {{QStringLiteral("device"), QStringLiteral("jlink:801000001")},
                                   {QStringLiteral("path"), hexFile(QStringLiteral("r.hex"))}}));
    ASSERT_TRUE(prompted("flash"));
    ASSERT_TRUE(waitFor([this] { return mcp().serverForTests()->activeCalls() == 2; }));
    mcp().regenerateToken();
    EXPECT_EQ(mcp().broker().current(), nullptr); // the prompt was denied
    ASSERT_TRUE(waitFor(
        [this] {
            return mcp().state() == McpController::Listening && mcp().activity()->count() >= 2;
        },
        15s));
    QStringList results;
    for (int i = 0; i < mcp().activity()->count(); ++i) {
        results.append(QString::fromStdString(mcp().activity()->entry(i).tool + "=" +
                                              mcp().activity()->entry(i).result));
    }
    EXPECT_TRUE(results.contains(QStringLiteral("flash=mcp.consent_denied")))
        << results.join(' ').toStdString();
    EXPECT_TRUE(results.contains(QStringLiteral("wait_for_state=cancelled")))
        << results.join(' ').toStdString();
    const int newPort = mcp().listeningPort();
    EXPECT_EQ(status(toolRequest(newPort, before, QStringLiteral("list_devices"), {})), 401);
    EXPECT_EQ(status(toolRequest(newPort, token(), QStringLiteral("list_devices"), {})), 200);
}

TEST_F(McpControllerTest, ShutdownWithAPendingPromptAndARunningCall) {
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    user.connectDevice();
    listen();
    const int port = mcp().listeningPort();
    HttpCall waiting(port, toolRequest(port, token(), QStringLiteral("wait_for_state"),
                                       {{QStringLiteral("pattern"), QStringLiteral("never-seen")},
                                        {QStringLiteral("timeout"), 60}}));
    HttpCall flashing(port,
                      toolRequest(port, token(), QStringLiteral("flash"),
                                  {{QStringLiteral("device"), QStringLiteral("jlink:801000001")},
                                   {QStringLiteral("path"), hexFile(QStringLiteral("s.hex"))}}));
    ASSERT_TRUE(prompted("flash"));
    ASSERT_TRUE(waitFor([this] { return mcp().serverForTests()->activeCalls() == 2; }));
    mcp().shutdown();
    EXPECT_EQ(mcp().state(), McpController::Off);
    EXPECT_EQ(mcp().broker().current(), nullptr);
    EXPECT_FALSE(user.isBusy());
    EXPECT_FALSE(user.aiCommandActive());
    ASSERT_GE(mcp().activity()->count(), 1);
    EXPECT_EQ(mcp().activity()->entry(mcp().activity()->count() - 1).result, "mcp.consent_denied");
    spin(100ms); // nothing is left that touches the destroyed server
}

TEST_F(McpControllerTest, EraseAndWriteMemoryOverHttp) {
    // A probe whose chip has RAM at 0x20000000 (write_memory).
    useScenario(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
        "product": "Test Probe", "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "booted\n"}]},
        "chip": {"ram": [{"address": 536870912, "size": 4096}]}}]})");
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    user.connectDevice();
    ASSERT_TRUE(waitFor([&] { return user.state() == DeviceTabViewModel::Connected; }));
    listen();
    const int port = mcp().listeningPort();

    HttpCall erasing(port,
                     toolRequest(port, token(), QStringLiteral("erase"),
                                 {{QStringLiteral("device"), QStringLiteral("jlink:801000001")},
                                  {QStringLiteral("scope"), QStringLiteral("code")}}));
    ASSERT_TRUE(prompted("erase"));
    allowOnce();
    const HttpResponse erased = erasing.wait(60s);
    ASSERT_FALSE(erased.isError()) << erased.errorCode().toStdString();
    EXPECT_EQ(mcp().activity()->entry(0).tool, "erase");
    EXPECT_EQ(mcp().activity()->entry(0).result, "ok");

    HttpCall writing(port,
                     toolRequest(port, token(), QStringLiteral("write_memory"),
                                 {{QStringLiteral("device"), QStringLiteral("jlink:801000001")},
                                  {QStringLiteral("address"), QStringLiteral("0x20000010")},
                                  {QStringLiteral("data_hex"), QStringLiteral("deadbeef")}}));
    ASSERT_TRUE(prompted("write_memory"));
    bool warned = false;
    for (const QVariant& row : mcp().consent()->details()) {
        warned = warned || row.toMap().value(QStringLiteral("warning")).toBool();
    }
    EXPECT_TRUE(warned); // ADR-0164
    allowOnce();
    const HttpResponse written = writing.wait(60s);
    ASSERT_FALSE(written.isError())
        << QJsonDocument(written.structured()).toJson(QJsonDocument::Compact).toStdString();
    EXPECT_EQ(written.structured().value(QStringLiteral("units_written")).toInteger(), 4);
}

// ADR-0241: a legacy session's end (server clientGone) forgets its grants.
TEST_F(McpControllerTest, GrantForgottenWhenTheLegacySessionEnds) {
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    user.connectDevice();
    listen();
    const int port = mcp().listeningPort();
    const auto legacyRequest = [&](const QByteArray& method, const QJsonObject& message,
                                   const QByteArray& session) {
        const QByteArray body = message.isEmpty()
                                    ? QByteArray()
                                    : QJsonDocument(message).toJson(QJsonDocument::Compact);
        QByteArray text = method + " /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
                          "\r\nAuthorization: Bearer " + QByteArray::fromStdString(token()) +
                          "\r\nAccept: application/json, text/event-stream\r\n";
        if (!body.isEmpty()) {
            text += "Content-Type: application/json\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\n";
        }
        if (!session.isEmpty()) {
            text += "Mcp-Session-Id: " + session + "\r\nMCP-Protocol-Version: 2025-11-25\r\n";
        }
        return text + "\r\n" + body;
    };
    HttpCall init(
        port,
        legacyRequest(
            "POST",
            QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), 1},
                {QStringLiteral("method"), QStringLiteral("initialize")},
                {QStringLiteral("params"),
                 QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-11-25")},
                             {QStringLiteral("capabilities"), QJsonObject()},
                             {QStringLiteral("clientInfo"),
                              QJsonObject{{QStringLiteral("name"), QStringLiteral("Legacy")},
                                          {QStringLiteral("version"), QStringLiteral("2.0")}}}}}},
            {}));
    const HttpResponse initialized = init.wait();
    const QByteArray session = initialized.header("mcp-session-id");
    ASSERT_FALSE(session.isEmpty());
    HttpCall flashing(
        port,
        legacyRequest(
            "POST",
            QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), 2},
                {QStringLiteral("method"), QStringLiteral("tools/call")},
                {QStringLiteral("params"),
                 QJsonObject{
                     {QStringLiteral("name"), QStringLiteral("flash")},
                     {QStringLiteral("arguments"),
                      QJsonObject{{QStringLiteral("device"), QStringLiteral("jlink:801000001")},
                                  {QStringLiteral("path"), hexFile(QStringLiteral("l.hex"))}}}}}},
            session));
    ASSERT_TRUE(prompted("flash"));
    // ADR-0241: the client and its version in the prompt.
    EXPECT_EQ(detailValue(QStringLiteral("Client")), QStringLiteral("Legacy 2.0"));
    allowForClient();
    ASSERT_FALSE(flashing.wait(60s).isError());
    ASSERT_EQ(mcp().grants().size(), 1);
    EXPECT_EQ(mcp().grants().front().toMap().value(QStringLiteral("client")).toString(),
              QStringLiteral("Legacy 2.0"));
    HttpCall ending(port, legacyRequest("DELETE", {}, session));
    EXPECT_EQ(ending.wait().status, 200);
    EXPECT_TRUE(waitFor([this] { return mcp().grants().isEmpty(); }));
}

} // namespace
