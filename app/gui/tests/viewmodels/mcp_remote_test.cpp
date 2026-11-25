// Plan 0062 (A6, B2, B3): AI clients on a joined remote support tab, end to end in the GUI: the
// MCP server of McpController over HTTP, a remote tab of the AppViewModel joined over the
// in-process FakeRelay (libs/remote/tests) to an in-process Monitor host (RemoteHost on the
// FakeConsole with ScriptedConsent and FakeOperations, the host fixtures of the remote tests).
// The setting hides the tab; support's prompt (Allow for this session, nothing remembered); a
// tool's command reaches the customer's device and its output comes back through the hidden view;
// the same bytes on the relay as a person's line and no AI marker on the relay; a catalog flash
// confirmed or denied by the customer; Stop waiting; Stop AI; the grant ends on Leave session,
// Join again, the client going away and with the setting; errors without the session code.
//
// Every session code, key and token here is made up. No network beyond 127.0.0.1.

#include "fake_console.h"
#include "fake_relay.h"
#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/session_events.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/remote_host.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "mcp/gui_consent_broker.h"
#include "mcp/gui_device_provider.h"
#include "mcp/gui_remote_device.h"
#include "mcp/mcp_controller.h"
#include "mcp_gui_test_support.h"
#include "remote_test_support.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/remote_join_view_model.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using hwio::gui::DeviceTabViewModel;
using hwio::gui::GuiRemoteDevice;
using hwio::gui::McpController;
using hwio::gui::RemoteJoinViewModel;
using hwio::gui::test::HttpCall;
using hwio::gui::test::HttpResponse;
using hwio::gui::test::spin;
using hwio::gui::test::toolRequest;
using hwio::gui::test::waitFor;
using hwio::remote::testing::FakeConsole;
using hwio::remote::testing::FakeOperations;
using hwio::remote::testing::FakeRelay;
using hwio::remote::testing::RawParticipant;
using hwio::remote::testing::ScriptedConsent;
using namespace std::chrono_literals;

constexpr std::uint32_t codeValue = 482913; // made up
const QString remoteId = QStringLiteral("remote:1");
const QString catalogId = QStringLiteral("0123456789abcdef0123456789abcdef");

class McpRemote : public hwio::gui::test::McpGuiTest {
protected:
    void SetUp() override {
        McpGuiTest::SetUp();
        // A build that rejects the fake relay's plain-http loopback URL would use the real relay.
        if (!hwio::remote::parseRelayUrl(m_relay.url().toString().toStdString())) {
            GTEST_SKIP() << "this build does not accept the fake relay URL (HWIO_TEST_OVERRIDES)";
        }
    }
    void TearDown() override {
        destroyApp();
        m_host.reset();
        McpGuiTest::TearDown();
    }

    struct Host {
        explicit Host(const FakeRelay& relay)
            : host(console, consent, &operations,
                   hwio::remote::HostOptions{
                       .relay = relay.url(),
                       .limits = limits(),
                       .appVersion = "2.1.0-customer",
                       .code = *hwio::remote::SessionCode::fromValue(codeValue)}) {
            host.start();
        }
        static hwio::remote::HostLimits limits() {
            hwio::remote::HostLimits l;
            l.collisionWindow = 100ms;
            return l;
        }
        FakeConsole console;
        ScriptedConsent consent;
        FakeOperations operations;
        hwio::remote::RemoteHost host;
    };

    // The customer's Monitor sharing session 482 913.
    Host& host() {
        if (!m_host) {
            m_host = std::make_unique<Host>(m_relay);
            EXPECT_TRUE(waitFor(
                [this] { return m_host->host.state() == hwio::remote::HostState::Sharing; }, 10s));
        }
        return *m_host;
    }

    // The current tab joined to the host (allowed, after the host's hello).
    DeviceTabViewModel& joinedTab() {
        (void)host();
        DeviceTabViewModel& tab = *app().currentTabViewModel();
        if (tab.remoteSession() == nullptr) {
            EXPECT_TRUE(tab.configureRemote(QString::number(codeValue), m_relay.url()));
            tab.connectDevice();
        }
        EXPECT_TRUE(waitFor(
            [&tab] {
                const hwio::remote::RemoteConsoleSession* session = tab.remoteSession();
                return session != nullptr &&
                       session->joinState() == hwio::remote::JoinState::Allowed &&
                       session->hostHello().has_value() && tab.remoteJoin()->remoteDevice();
            },
            15s));
        EXPECT_TRUE(waitFor([this] { return !m_host->host.peerApp().empty(); }, 5s));
        return tab;
    }

    // MCP listening with the setting on, the tab joined.
    DeviceTabViewModel& ready() {
        listen();
        mcp().setRemoteSessions(true);
        return joinedTab();
    }

    std::unique_ptr<HttpCall> start(const QString& tool, QJsonObject arguments,
                                    const QString& client = QStringLiteral("Claude Code")) {
        if (!arguments.contains(QStringLiteral("device"))) {
            arguments.insert(QStringLiteral("device"), remoteId);
        }
        return std::make_unique<HttpCall>(
            mcp().listeningPort(),
            toolRequest(mcp().listeningPort(), token(), tool, arguments, client));
    }

    // Waits for a prompt of `category`.
    bool prompted(hwio::mcp::ConsentCategory category) {
        return waitFor(
            [this, category] {
                const auto* prompt = mcp().broker().current();
                return prompt != nullptr && prompt->request.categories.testFlag(category);
            },
            10s);
    }

    // A call that needs support's session prompt first: Allow for this session.
    HttpResponse callAllowed(const QString& tool, const QJsonObject& arguments) {
        const auto http = start(tool, arguments);
        EXPECT_TRUE(prompted(hwio::mcp::ConsentCategory::RemoteDevice));
        allowOnce();
        return http->wait();
    }

    [[nodiscard]] std::vector<QString> relayedText() const {
        std::vector<QString> out;
        for (const auto& event : m_relay.relayed()) {
            out.push_back(
                event.name + QLatin1Char(' ') +
                QString::fromUtf8(
                    QJsonDocument(QJsonArray{event.payload}).toJson(QJsonDocument::Compact)));
        }
        return out;
    }

    static void expectNoSessionCode(const QString& text, const QUrl& relay, const char* where) {
        EXPECT_FALSE(text.contains(QStringLiteral("482913")))
            << where << ": " << text.toStdString();
        EXPECT_FALSE(text.contains(QStringLiteral("482 913")))
            << where << ": " << text.toStdString();
        EXPECT_FALSE(text.contains(relay.authority())) << where << ": " << text.toStdString();
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    FakeRelay m_relay;
    std::unique_ptr<Host> m_host;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

bool listsRemote(const HttpResponse& response) {
    const QJsonArray devices = response.structured().value(QStringLiteral("devices")).toArray();
    return std::ranges::any_of(devices, [](const QJsonValue& device) {
        return device.toObject().value(QStringLiteral("id")).toString().startsWith(u"remote:");
    });
}

QJsonObject entryOf(const HttpResponse& response, const QString& id) {
    for (const auto& device : response.structured().value(QStringLiteral("devices")).toArray()) {
        if (device.toObject().value(QStringLiteral("id")) == id) {
            return device.toObject();
        }
    }
    return {};
}

hwio::mcp::RequestContext client(const char* name) {
    hwio::mcp::RequestContext who;
    who.client = {.name = name, .version = "1"};
    who.clientKey = name;
    return who;
}

// The setting (default off) hides the remote tab; on, it is "remote:1" (nameless before
// support's Allow); off again ends the grant. Join and leave stay human.
TEST_F(McpRemote, TheSettingHidesTheRemoteTab) {
    listen();
    EXPECT_FALSE(mcp().remoteSessions());
    DeviceTabViewModel& tab = joinedTab();
    HttpResponse list = call(QStringLiteral("list_devices"));
    ASSERT_FALSE(list.isError()) << list.body.toStdString();
    EXPECT_FALSE(listsRemote(list));
    EXPECT_FALSE(mcp().provider().find("remote:1").has_value());

    mcp().setRemoteSessions(true);
    EXPECT_EQ(app().settingsStore()->value(McpController::remoteSessionsKey).toBool(), true);
    list = call(QStringLiteral("list_devices"));
    const QJsonObject entry = entryOf(list, remoteId);
    ASSERT_FALSE(entry.isEmpty()) << list.body.toStdString();
    EXPECT_EQ(entry.value(QStringLiteral("kind")), QStringLiteral("remote"));
    EXPECT_EQ(entry.value(QStringLiteral("display_name")),
              QStringLiteral("Remote support session"));
    expectNoSessionCode(QString::fromUtf8(list.body), m_relay.url(), "list_devices");
    const auto ref = mcp().provider().find("remote:1");
    ASSERT_TRUE(ref.has_value());
    EXPECT_TRUE(ref.value_or(hwio::mcp::tools::DeviceRef{}).isRemote);
    EXPECT_TRUE(ref.value_or(hwio::mcp::tools::DeviceRef{}).jlink.isNull());
    EXPECT_EQ(mcp().provider().remoteId(tab), "remote:1");

    // Join and leave stay human (ADR-0433).
    std::optional<hwio::Result<hwio::mcp::tools::DeviceRef>> opened;
    mcp().provider().open(
        hwio::mcp::tools::OpenRequest{.id = "remote:1",
                                      .profileId = {},
                                      .targetId = {},
                                      .speedKhz = std::nullopt,
                                      .autoReconnect = std::nullopt,
                                      .context = client("Claude Code")},
        [&opened](hwio::Result<hwio::mcp::tools::DeviceRef> r) { opened = std::move(r); });
    ASSERT_TRUE(opened.has_value());
    const hwio::Result<hwio::mcp::tools::DeviceRef> openResult =
        opened.value_or(std::unexpected(hwio::makeError(hwio::ErrorCode::Cancelled)));
    ASSERT_FALSE(openResult.has_value());
    EXPECT_EQ(openResult.error().code, hwio::ErrorCode::McpToolNotAllowed);
    EXPECT_EQ(mcp().provider().close("remote:1", client("Claude Code")).error().code,
              hwio::ErrorCode::McpToolNotAllowed);
    EXPECT_EQ(
        call(QStringLiteral("disconnect"), {{QStringLiteral("device"), remoteId}}).errorCode(),
        QStringLiteral("mcp.tool_not_allowed"));

    GuiRemoteDevice* device = tab.remoteJoin()->remoteDevice();
    ASSERT_NE(device, nullptr);
    device->allowAi(client("Claude Code"));
    EXPECT_TRUE(device->aiAllowed("Claude Code"));
    mcp().setRemoteSessions(false);
    EXPECT_FALSE(device->aiAllowed("Claude Code"));
    EXPECT_FALSE(listsRemote(call(QStringLiteral("list_devices"))));
    EXPECT_FALSE(mcp().provider().find("remote:1").has_value());
}

// Support's prompt: its title and rows (no session code), Deny first, then Allow for this session;
// nothing remembered, the banner names the client; later calls of the client need no prompt.
TEST_F(McpRemote, SupportsPromptAllowsForThisSessionOnly) {
    DeviceTabViewModel& tab = ready();
    auto denied = start(QStringLiteral("read_terminal"), {});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::RemoteDevice));
    EXPECT_EQ(mcp().consent()->title(),
              QStringLiteral("Claude Code wants to work with a customer's device"));
    EXPECT_FALSE(mcp().consent()->canRemember());
    EXPECT_TRUE(mcp().consent()->rememberLabel().isEmpty());
    EXPECT_EQ(mcp().consent()->allowLabel(), QStringLiteral("Allow for this session"));
    const QVariantList rows = mcp().consent()->details();
    ASSERT_GE(rows.size(), 2);
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("label")), QStringLiteral("Session"));
    const QString session = rows.at(0).toMap().value(QStringLiteral("value")).toString();
    EXPECT_FALSE(session.isEmpty());
    expectNoSessionCode(session, m_relay.url(), "session row");
    EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("value")),
              QStringLiteral("It will read the customer's console and may send commands to the "
                             "customer's device. Secret values are hidden from it. The customer "
                             "sees its commands as yours."));
    deny(); // Deny is the default (Esc, the focus; tst_mcp)
    EXPECT_EQ(denied->wait().errorCode(), QStringLiteral("mcp.consent_denied"));
    EXPECT_TRUE(tab.remoteJoin()->aiClientText().isEmpty());

    // "Always allow" does not exist for it.
    auto again = start(QStringLiteral("read_terminal"), {});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::RemoteDevice));
    allowForClient(); // ignored
    EXPECT_NE(mcp().broker().current(), nullptr);
    allowOnce(); // Allow for this session
    const HttpResponse read = again->wait();
    ASSERT_FALSE(read.isError()) << read.body.toStdString();
    EXPECT_TRUE(read.structured().value(QStringLiteral("remote")).toBool());
    EXPECT_TRUE(mcp().broker().grants().empty());
    EXPECT_EQ(tab.remoteJoin()->aiClientText(), QStringLiteral("AI client active: Claude Code"));

    // No second prompt for this client in this session; another client is asked.
    EXPECT_FALSE(
        call(QStringLiteral("read_log"), {{QStringLiteral("device"), remoteId}}).isError());
    auto other = start(QStringLiteral("read_terminal"), {}, QStringLiteral("Other Client"));
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::RemoteDevice));
    deny();
    EXPECT_EQ(other->wait().errorCode(), QStringLiteral("mcp.consent_denied"));
}

// B2: a tool's send_command reaches the customer's device; its output comes back through the
// hidden view (the session code replaced); the relay carries the same `write` as a person's line
// and none of support's AI markers or hints.
TEST_F(McpRemote, SendCommandReachesTheCustomersDeviceThroughTheHiddenView) {
    DeviceTabViewModel& tab = ready();
    Host& customer = host();
    // A person's line first (what the console's input sends).
    ASSERT_TRUE(tab.remoteSession()->sendLine("info show").has_value());
    ASSERT_TRUE(waitFor([&] { return customer.console.sent.size() == 1; }, 5s));
    spin(200ms);
    const std::size_t afterPerson = m_relay.relayed().size();
    std::vector<QString> personWrites;
    for (const QString& event : relayedText()) {
        if (event.startsWith(u"write ") && !event.contains(u"{")) {
            personWrites.push_back(event);
        }
    }
    ASSERT_EQ(personWrites.size(), 1U);

    const auto http = start(
        QStringLiteral("send_command"),
        {{QStringLiteral("command"), QStringLiteral("info show")}, {QStringLiteral("timeout"), 5}});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::RemoteDevice));
    allowOnce();
    ASSERT_TRUE(waitFor([&] { return customer.console.sent.size() == 2; }, 10s));
    EXPECT_TRUE(tab.aiCommandActive());
    customer.console.shell("session 482 913 of the customer");
    customer.console.shell("firmware: CHESTER-Clime v3.2.1");
    const HttpResponse response = http->wait();
    ASSERT_FALSE(response.isError()) << response.body.toStdString();
    EXPECT_EQ(customer.console.sentLines(), (std::vector<std::string>{"info show", "info show"}));
    const QJsonObject result = response.structured();
    EXPECT_TRUE(result.value(QStringLiteral("remote")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("end")), QStringLiteral("silence"));
    QStringList output;
    for (const auto& line : result.value(QStringLiteral("output")).toArray()) {
        output.append(line.toObject().value(QStringLiteral("text")).toString());
    }
    EXPECT_TRUE(output.contains(QStringLiteral("session <session> of the customer")))
        << output.join(u'|').toStdString();
    EXPECT_TRUE(output.contains(QStringLiteral("firmware: CHESTER-Clime v3.2.1")));
    expectNoSessionCode(QString::fromUtf8(response.body), m_relay.url(), "send_command");
    EXPECT_FALSE(tab.aiCommandActive());

    // The same bytes as the person's line; nothing of the AI markers or hints on the relay.
    std::vector<QString> aiWrites;
    const std::vector<QString> all = relayedText();
    for (std::size_t i = afterPerson; i < all.size(); ++i) {
        if (all.at(i).startsWith(u"write ") && !all.at(i).contains(u"{")) {
            aiWrites.push_back(all.at(i));
        }
    }
    EXPECT_EQ(aiWrites, personWrites);
    for (const QString& event : all) {
        EXPECT_FALSE(event.contains(u"AI (")) << event.toStdString();
        EXPECT_FALSE(event.contains(u"Claude Code")) << event.toStdString();
        EXPECT_FALSE(event.contains(u"AI is running")) << event.toStdString();
    }
    EXPECT_TRUE(m_relay.droppedEvents().isEmpty());
    // The marker is in support's own console.
    bool marker = false;
    for (const auto& line : tab.consoleLog()->linesSince(0)) {
        marker = marker || line.text == "— AI (Claude Code): send_command —";
    }
    EXPECT_TRUE(marker);
}

// B2 / B3: a catalog flash requested by a tool call asks support every time (the customer's
// device row, nothing remembered), shows "AI: …" on the Flash card, is confirmed by the customer
// and completes; denied by the customer it ends with remote.operation_denied.
TEST_F(McpRemote, CatalogFlashConfirmedOrDeniedByTheCustomer) {
    DeviceTabViewModel& tab = ready();
    Host& customer = host();
    RemoteJoinViewModel& join = *tab.remoteJoin();
    ASSERT_FALSE(callAllowed(QStringLiteral("status"), {}).isError());

    auto flash = start(QStringLiteral("flash"), {{QStringLiteral("catalog_id"), catalogId}});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::Flash));
    EXPECT_FALSE(mcp().consent()->canRemember());
    EXPECT_EQ(mcp().consent()->allowLabel(), QStringLiteral("Allow once"));
    bool deviceRow = false;
    for (const QVariant& row : mcp().consent()->details()) {
        const QVariantMap map = row.toMap();
        deviceRow = deviceRow ||
                    (map.value(QStringLiteral("label")).toString() == QStringLiteral("Device") &&
                     map.value(QStringLiteral("value")).toString() ==
                         QStringLiteral("the customer's device (remote session)"));
    }
    EXPECT_TRUE(deviceRow);
    allowOnce();
    ASSERT_TRUE(waitFor([&] { return customer.operations.started.size() == 1; }, 10s));
    EXPECT_EQ(customer.operations.started.front().firmwareId.value_or(""), catalogId.toStdString());
    ASSERT_TRUE(waitFor([&] { return join.aiWaiting(); }, 5s));
    EXPECT_TRUE(join.operationPending());
    EXPECT_TRUE(join.operationStatus().startsWith(u"AI: Flashing…"))
        << join.operationStatus().toStdString();
    customer.operations.finished(hwio::Status{});
    const HttpResponse done = flash->wait();
    ASSERT_FALSE(done.isError()) << done.body.toStdString();
    EXPECT_EQ(done.structured().value(QStringLiteral("status")), QStringLiteral("ok"));
    EXPECT_TRUE(done.structured().value(QStringLiteral("remote")).toBool());
    EXPECT_FALSE(join.aiWaiting());
    EXPECT_FALSE(join.operationPending());

    customer.consent.operations = ScriptedConsent::Mode::Deny;
    auto refused = start(QStringLiteral("flash"), {{QStringLiteral("catalog_id"), catalogId}});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::Flash));
    allowOnce(); // support allows, the customer denies
    const HttpResponse denied = refused->wait();
    EXPECT_EQ(denied.errorCode(), QStringLiteral("remote.operation_denied"))
        << denied.body.toStdString();
    EXPECT_EQ(customer.operations.started.size(), 1U);
    EXPECT_FALSE(join.operationPending());
}

// D12 (B3): Stop waiting (McpController::cancelOperation of the remote tab) ends the call with
// `cancelled`; the tab follows the operation to its result, Leave session waits, another request
// gets internal.busy.
TEST_F(McpRemote, StopWaitingEndsTheCallAndTheTabFollowsTheOperation) {
    DeviceTabViewModel& tab = ready();
    Host& customer = host();
    RemoteJoinViewModel& join = *tab.remoteJoin();
    ASSERT_FALSE(callAllowed(QStringLiteral("status"), {}).isError());
    customer.consent.operations = ScriptedConsent::Mode::Manual;

    auto reset = start(QStringLiteral("reset"), {});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::Flash));
    allowOnce();
    ASSERT_TRUE(waitFor([&] { return static_cast<bool>(customer.consent.pendingOperation); }, 10s));
    ASSERT_TRUE(join.aiWaiting());
    McpController::cancelOperation(&tab);
    const HttpResponse stopped = reset->wait();
    EXPECT_EQ(stopped.errorCode(), QStringLiteral("cancelled")) << stopped.body.toStdString();
    EXPECT_TRUE(stopped.body.contains("continues on the customer's device"));
    EXPECT_FALSE(join.aiWaiting());
    EXPECT_TRUE(join.operationPending());
    EXPECT_FALSE(tab.connection()->actionEnabled()); // Leave session waits

    auto second = start(QStringLiteral("reset"), {});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::Flash));
    allowOnce();
    EXPECT_EQ(second->wait().errorCode(), QStringLiteral("internal.busy"));

    std::exchange(customer.consent.pendingOperation, {})(true);
    ASSERT_TRUE(waitFor([&] { return customer.operations.started.size() == 1; }, 10s));
    customer.operations.finished(hwio::Status{});
    ASSERT_TRUE(waitFor([&] { return !join.operationPending(); }, 10s));
    EXPECT_FALSE(join.operationFailed());
    EXPECT_TRUE(tab.connection()->actionEnabled());
}

// ADR-0436: Stop AI ends the grant and a running call (`cancelled`); the next call asks again.
TEST_F(McpRemote, StopAiEndsTheGrantAndARunningCall) {
    DeviceTabViewModel& tab = ready();
    ASSERT_FALSE(callAllowed(QStringLiteral("status"), {}).isError());
    auto wait = start(QStringLiteral("wait_for_state"),
                      {{QStringLiteral("pattern"), QStringLiteral("never printed")},
                       {QStringLiteral("timeout"), 20}});
    spin(300ms);
    ASSERT_FALSE(wait->done());
    tab.remoteJoin()->stopAi();
    EXPECT_EQ(wait->wait(5s).errorCode(), QStringLiteral("cancelled"));
    EXPECT_TRUE(tab.remoteJoin()->aiClientText().isEmpty());
    auto again = start(QStringLiteral("read_terminal"), {});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::RemoteDevice));
    deny();
    EXPECT_EQ(again->wait().errorCode(), QStringLiteral("mcp.consent_denied"));
}

// ADR-0436: the grant ends when the tab leaves the session and with Join again (the same id, a new
// prompt), when the client goes away and when the remembered grants are forgotten.
TEST_F(McpRemote, TheGrantEndsWithTheSessionAndTheClient) {
    DeviceTabViewModel& tab = ready();
    ASSERT_FALSE(callAllowed(QStringLiteral("status"), {}).isError());
    ASSERT_TRUE(tab.remoteJoin()->remoteDevice()->aiAllowed("Claude Code"));

    tab.disconnectDevice(); // Leave session
    EXPECT_EQ(tab.remoteJoin()->remoteDevice(), nullptr);
    EXPECT_FALSE(listsRemote(call(QStringLiteral("list_devices"))));
    EXPECT_FALSE(mcp().provider().find("remote:1").has_value());

    tab.connectDevice(); // Join again
    (void)joinedTab();
    EXPECT_EQ(mcp().provider().remoteId(tab), "remote:1");
    GuiRemoteDevice* device = tab.remoteJoin()->remoteDevice();
    ASSERT_NE(device, nullptr);
    EXPECT_FALSE(device->aiAllowed("Claude Code"));
    ASSERT_FALSE(callAllowed(QStringLiteral("status"), {}).isError());
    EXPECT_TRUE(device->aiAllowed("Claude Code"));

    mcp().broker().clientGone("Claude Code");
    EXPECT_FALSE(device->aiAllowed("Claude Code"));
    device->allowAi(client("Claude Code"));
    mcp().broker().forgetAll();
    EXPECT_FALSE(device->aiAllowed("Claude Code"));
}

// ADR-0434: errors of the remote device never carry the session code or the relay; a session the
// customer ended takes no lines and gives no grant.
TEST_F(McpRemote, ErrorsCarryNeitherTheSessionCodeNorTheRelay) {
    DeviceTabViewModel& tab = ready();
    GuiRemoteDevice* device = tab.remoteJoin()->remoteDevice();
    ASSERT_NE(device, nullptr);
    const QString name = device->displayName(true);
    EXPECT_TRUE(name.endsWith(u" — remote")) << name.toStdString();
    expectNoSessionCode(name, m_relay.url(), "display name");

    m_host->host.end();
    ASSERT_TRUE(waitFor(
        [&tab] {
            return tab.remoteSession() != nullptr &&
                   tab.remoteSession()->joinState() == hwio::remote::JoinState::Ended;
        },
        15s));
    const auto errorText = [](const hwio::Error& error) {
        return QString::fromStdString(error.message + "|" + error.cause + "|" + error.suggestion +
                                      "|" + error.detail + "|" + error.messageEn + "|" +
                                      error.causeEn);
    };
    const hwio::Status sent = device->sendLine("config show", client("Claude Code"));
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error().code, hwio::ErrorCode::RemoteHostEnded);
    expectNoSessionCode(errorText(sent.error()), m_relay.url(), "sendLine");
    const auto requested = device->requestOperation(
        hwio::mcp::tools::RemoteOperationRequest{
            .kind = hwio::mcp::tools::RemoteOperationKind::Reset, .target = "app"},
        client("Claude Code"), {});
    ASSERT_FALSE(requested.has_value());
    expectNoSessionCode(errorText(requested.error()), m_relay.url(), "requestOperation");
    device->allowAi(client("Claude Code"));
    EXPECT_FALSE(device->aiAllowed("Claude Code"));
    EXPECT_EQ(device->info().join, hwio::mcp::tools::RemoteJoin::Ended);
}

// ADR-0434 / ADR-0437 with a host that sends no hello (the web Terminal, the mobile app: the tab
// title carries the session code) and then an older Monitor's hello without targets: the name,
// the prompt, the results and a connection error never carry the code or the relay; the hello-less
// host offers no operations, the older Monitor the CHESTER app / lte ones.
TEST_F(McpRemote, HostsWithoutAHelloOrTargetsNeverShowTheCode) {
    listen();
    mcp().setRemoteSessions(true);
    RawParticipant web(m_relay.url(), QStringLiteral("/482913"));
    ASSERT_TRUE(web.waitConnected());
    DeviceTabViewModel& tab = *app().currentTabViewModel();
    ASSERT_TRUE(tab.configureRemote(QString::number(codeValue), m_relay.url()));
    tab.connectDevice();
    ASSERT_TRUE(waitFor([&] { return web.named(QStringLiteral("newClient")).size() >= 2; }, 10s));
    web.emitEvent(QStringLiteral("sync"),
                  QJsonObject{{QStringLiteral("state"), QStringLiteral("connected")},
                              {QStringLiteral("commands"), QJsonArray()}});
    ASSERT_TRUE(waitFor(
        [&] {
            return tab.remoteSession() != nullptr &&
                   tab.remoteSession()->joinState() == hwio::remote::JoinState::Allowed;
        },
        10s));
    ASSERT_TRUE(tab.title().contains(QStringLiteral("482 913"))); // the case this test is about
    web.emitEvent(QStringLiteral("at"),
                  QJsonObject{{QStringLiteral("event"), QStringLiteral("read")},
                              {QStringLiteral("data"), QStringLiteral("code 482913 printed")}});
    ASSERT_TRUE(waitFor([&] { return tab.consoleLog()->lastSeq() > 0; }, 5s));

    auto read = start(QStringLiteral("read_terminal"), {});
    ASSERT_TRUE(prompted(hwio::mcp::ConsentCategory::RemoteDevice));
    const QString session =
        mcp().consent()->details().at(0).toMap().value(QStringLiteral("value")).toString();
    EXPECT_EQ(session, QStringLiteral("Remote session (web Terminal or mobile app)"));
    allowOnce();
    const HttpResponse terminal = read->wait();
    ASSERT_FALSE(terminal.isError()) << terminal.body.toStdString();
    EXPECT_TRUE(terminal.body.contains("code <session> printed")) << terminal.body.toStdString();
    expectNoSessionCode(QString::fromUtf8(terminal.body), m_relay.url(), "read_terminal");

    HttpResponse list = call(QStringLiteral("list_devices"));
    QJsonObject entry = entryOf(list, remoteId);
    EXPECT_EQ(entry.value(QStringLiteral("display_name")),
              QStringLiteral("Remote session (web Terminal or mobile app)"));
    QJsonObject block = entry.value(QStringLiteral("remote")).toObject();
    EXPECT_EQ(block.value(QStringLiteral("host")), QStringLiteral("other"));
    EXPECT_TRUE(block.value(QStringLiteral("operations")).toArray().isEmpty());
    expectNoSessionCode(QString::fromUtf8(list.body), m_relay.url(), "list_devices");
    const HttpResponse flash =
        call(QStringLiteral("flash"),
             {{QStringLiteral("device"), remoteId}, {QStringLiteral("catalog_id"), catalogId}});
    EXPECT_TRUE(flash.isError());
    EXPECT_EQ(flash.errorCode(), QStringLiteral("remote.target_unsupported"))
        << flash.body.toStdString();

    // An older Monitor (2.0.0-rc.4): ops with opKinds, no targets, no device name.
    namespace tunnel = hwio::remote::tunnel;
    web.emitEvent(QStringLiteral("at"),
                  tunnel::encodeHost(tunnel::HostHello{
                      .app = "hwio-monitor",
                      .version = "2.0.0-rc.4",
                      .device = {},
                      .ops = true,
                      .opKinds = {tunnel::OpKind::Flash, tunnel::OpKind::Erase,
                                  tunnel::OpKind::Recover, tunnel::OpKind::Reset}}));
    ASSERT_TRUE(waitFor([&] { return tab.remoteSession()->hostHello().has_value(); }, 5s));
    list = call(QStringLiteral("list_devices"));
    entry = entryOf(list, remoteId);
    EXPECT_EQ(entry.value(QStringLiteral("display_name")), QStringLiteral("CHESTER — remote"));
    block = entry.value(QStringLiteral("remote")).toObject();
    EXPECT_EQ(block.value(QStringLiteral("host")), QStringLiteral("monitor"));
    const QJsonArray operations = block.value(QStringLiteral("operations")).toArray();
    ASSERT_EQ(operations.size(), 2) << list.body.toStdString();
    EXPECT_EQ(operations.at(0).toObject(),
              (QJsonObject{{QStringLiteral("target"), QStringLiteral("app")},
                           {QStringLiteral("kinds"),
                            QJsonArray{QStringLiteral("flash"), QStringLiteral("reset")}},
                           {QStringLiteral("catalog"), true}}));
    EXPECT_EQ(operations.at(1).toObject(),
              (QJsonObject{{QStringLiteral("target"), QStringLiteral("lte")},
                           {QStringLiteral("kinds"), QJsonArray{QStringLiteral("reset")}}}));
    expectNoSessionCode(QString::fromUtf8(list.body), m_relay.url(), "list_devices (rc.4)");

    // A connection error of the relay reaches status and list_devices without the relay.
    m_relay.dropAll();
    ASSERT_TRUE(waitFor(
        [&] {
            return tab.remoteSession()->connection().state !=
                   hwio::device::ConnectionState::Connected;
        },
        10s));
    const HttpResponse status =
        call(QStringLiteral("status"), {{QStringLiteral("device"), remoteId}});
    expectNoSessionCode(QString::fromUtf8(status.body), m_relay.url(), "status (relay lost)");
    list = call(QStringLiteral("list_devices"));
    expectNoSessionCode(QString::fromUtf8(list.body), m_relay.url(), "list_devices (relay lost)");
}

} // namespace
