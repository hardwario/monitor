// Plan 0014 A3 / D1: the GuiDeviceProvider over the device tabs of a real AppViewModel (fake J-Link
// library): ids and the list from the tabs and the probe scanner, connect opens an AI tab with
// openedBy (appended, the current tab unchanged, not in ui/tabs), connects a user's tab
// (ADR-0242), the tab limit, disconnect of a user's tab, busy / markers / the command hint, a tab
// closed while a call holds its session, and Bluetooth tabs (step D1). Integration with plan 0036:
// an AI operation blocks the J-Link firmware update; a probe reserved for it refuses connect.

#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/session_events.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tools/device_provider.h"
#include "mcp/gui_device_provider.h"
#include "mcp/mcp_controller.h"
#include "mcp_gui_test_support.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/probe_firmware_view_model.h"
#include "workers/ble_service.h"

#include <QByteArray>
#include <QChar>
#include <QLatin1Char>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QStringLiteral>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using hwio::gui::DeviceTabViewModel;
using hwio::gui::GuiDeviceProvider;
using hwio::gui::test::spin;
using hwio::gui::test::waitFor;
using hwio::mcp::tools::DeviceEntry;
using hwio::mcp::tools::DeviceRef;
using hwio::mcp::tools::OpenRequest;
using namespace std::chrono_literals;

class McpProvider : public hwio::gui::test::McpGuiTest {
protected:
    GuiDeviceProvider& provider() { return mcp().provider(); }

    hwio::Result<DeviceRef> open(const std::string& id, const std::string& client = "Claude Code") {
        OpenRequest request;
        request.id = id;
        request.context.client = {.name = client, .version = "1"};
        request.context.clientKey = client;
        std::optional<hwio::Result<DeviceRef>> result;
        provider().open(request,
                        [&result](hwio::Result<DeviceRef> ref) { result = std::move(ref); });
        EXPECT_TRUE(result.has_value()); // synchronous (review R17)
        return result.value_or(std::unexpected(hwio::makeError(hwio::ErrorCode::Cancelled)));
    }

    // The entry of `id` (an empty id when it is not listed).
    DeviceEntry entry(const std::string& id) {
        const auto entries = provider().list();
        const auto it = std::ranges::find(entries, id, &DeviceEntry::id);
        return it == entries.end() ? DeviceEntry{} : *it;
    }

    static bool connected(const DeviceRef& ref) {
        return ref.alive() &&
               ref.console->connection().state == hwio::device::ConnectionState::Connected;
    }
};

TEST_F(McpProvider, ListsTabsAndScannedProbes) {
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    EXPECT_TRUE(provider().firstScanPending());
    bool refreshed = false;
    provider().refresh(
        [&refreshed](const hwio::Status& status) { refreshed = status.has_value(); });
    ASSERT_TRUE(waitFor([&] { return refreshed; }));
    EXPECT_FALSE(provider().firstScanPending());

    const DeviceEntry tabEntry = entry("jlink:801000001");
    ASSERT_FALSE(tabEntry.id.empty());
    EXPECT_EQ(tabEntry.state, DeviceEntry::State::Available);
    EXPECT_EQ(tabEntry.displayName, user.title().toStdString());
    EXPECT_EQ(tabEntry.profileId, "chester");
    EXPECT_FALSE(tabEntry.openedByMcp);
    const DeviceEntry probe = entry("jlink:801000002");
    ASSERT_FALSE(probe.id.empty());
    EXPECT_EQ(probe.state, DeviceEntry::State::Available);
    EXPECT_EQ(probe.displayName, "J-Link 801000002 (Test Probe 2)");
    EXPECT_FALSE(provider().find("jlink:801000001").has_value());

    QSignalSpy changed(&provider().notifier(),
                       &hwio::mcp::tools::DeviceProviderNotifier::devicesChanged);
    user.connectDevice();
    EXPECT_GE(changed.count(), 1);
    ASSERT_TRUE(waitFor([&] { return entry("jlink:801000001").connection.has_value(); }));
    EXPECT_EQ(entry("jlink:801000001").state, DeviceEntry::State::Open);
    const auto ref = provider().find("jlink:801000001");
    ASSERT_TRUE(ref.has_value());
    EXPECT_FALSE(ref.value_or(DeviceRef{}).jlink.isNull());
}

TEST_F(McpProvider, ConnectOpensAnAiTabWithoutSwitching) {
    (void)app();
    const int current = app().currentTab();
    auto ref = open("jlink:801000002");
    ASSERT_TRUE(ref.has_value()) << ref.error().detail;
    ASSERT_EQ(app().tabs()->count(), 2);
    EXPECT_EQ(app().currentTab(), current);
    DeviceTabViewModel* tab = provider().tabFor("jlink:801000002");
    ASSERT_NE(tab, nullptr);
    EXPECT_EQ(tab->openedBy(), QStringLiteral("Claude Code"));
    EXPECT_EQ(app().tabs()->indexOf(tab), 1);
    EXPECT_TRUE(waitFor([&] { return connected(*ref); }));
    EXPECT_TRUE(entry("jlink:801000002").openedByMcp);
    // ADR-0244: not restored.
    app().saveState();
    EXPECT_FALSE(app()
                     .settingsStore()
                     ->value(QStringLiteral("ui/tabs"))
                     .toString()
                     .contains(QStringLiteral("801000002")));
    // The AI may close its own tab (queued).
    hwio::mcp::RequestContext owner;
    owner.clientKey = "Claude Code";
    EXPECT_TRUE(provider().close("jlink:801000002", owner).has_value());
    EXPECT_TRUE(waitFor([&] { return app().tabs()->count() == 1; }));
    EXPECT_TRUE(waitFor([&] { return !ref->alive(); }));
}

TEST_F(McpProvider, ConnectUsesTheUsersTabAndCannotCloseIt) {
    (void)app();
    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    auto ref = open("jlink:801000001");
    ASSERT_TRUE(ref.has_value()) << ref.error().detail;
    EXPECT_EQ(app().tabs()->count(), 1); // no new tab (ADR-0242)
    EXPECT_EQ(user.openedBy(), QString());
    EXPECT_EQ(ref->console.data(), user.consoleSession());
    EXPECT_TRUE(waitFor([&] { return connected(*ref); }));
    const hwio::Status closed = provider().close("jlink:801000001", {});
    ASSERT_FALSE(closed.has_value());
    EXPECT_EQ(closed.error().code, hwio::ErrorCode::McpToolNotAllowed);
    EXPECT_NE(closed.error().message.find("only the user can close it"), std::string::npos);
    spin(50ms);
    EXPECT_EQ(app().tabs()->count(), 1);
    EXPECT_TRUE(connected(*ref));
    // An unknown device.
    EXPECT_EQ(provider().close("jlink:42", {}).error().code, hwio::ErrorCode::McpDeviceNotOpen);
    EXPECT_EQ(open("jlink:0").error().code, hwio::ErrorCode::McpDeviceNotOpen);
    EXPECT_EQ(open("ble:F1:22:33:44:55:66").error().code,
              hwio::ErrorCode::McpDeviceNotOpen); // Bluetooth off in this application
}

// Plan 0014 (clarified 2026-11-04): speed_khz / auto_reconnect of `connect` apply to the AI tab
// only; the probe's remembered options (probes/<sn>/...) stay untouched.
TEST_F(McpProvider, ConnectOptionsAreNotRemembered) {
    (void)app();
    hwio::gui::SettingsStore& settings = *app().settingsStore();
    const QString recentBefore = settings.value(QStringLiteral("probes/recent")).toString();
    OpenRequest request;
    request.id = "jlink:801000002";
    request.speedKhz = 1000;
    request.autoReconnect = false;
    request.context.client = {.name = "Claude Code", .version = "1"};
    std::optional<hwio::Result<DeviceRef>> result;
    provider().open(request, [&result](hwio::Result<DeviceRef> ref) { result = std::move(ref); });
    ASSERT_TRUE(result.has_value() && result->has_value());
    DeviceTabViewModel* tab = provider().tabFor("jlink:801000002");
    ASSERT_NE(tab, nullptr);
    EXPECT_FALSE(tab->connection()->options().autoSpeed);
    EXPECT_EQ(tab->connection()->options().speedKhz, 1000);
    EXPECT_FALSE(tab->connection()->options().autoReconnect);
    EXPECT_EQ(settings.value(QStringLiteral("probes/801000002/speedKhz")).toInt(), 0);
    EXPECT_TRUE(settings.value(QStringLiteral("probes/801000002/autoReconnect")).toBool());
    EXPECT_EQ(settings.value(QStringLiteral("probes/recent")).toString(), recentBefore);
    const QSettings raw(QString::fromStdU16String(settingsFile().u16string()),
                        QSettings::IniFormat);
    EXPECT_FALSE(raw.contains(QStringLiteral("probes/801000002/speedKhz")));
    EXPECT_FALSE(raw.contains(QStringLiteral("probes/801000002/autoReconnect")));
}

TEST_F(McpProvider, TabLimitGivesSessionLimit) {
    (void)app();
    while (app().tabs()->canAdd()) {
        ASSERT_NE(app().appendTab(), nullptr);
    }
    const auto ref = open("jlink:801000002");
    ASSERT_FALSE(ref.has_value());
    EXPECT_EQ(ref.error().code, hwio::ErrorCode::InternalSessionLimit);
    EXPECT_EQ(provider().tabFor("jlink:801000002"), nullptr);
}

TEST_F(McpProvider, FailedOpenClosesTheNewTab) {
    (void)app();
    OpenRequest request;
    request.id = "jlink:801000002";
    request.profileId = "no-such-profile";
    request.context.client = {.name = "Claude Code", .version = "1"};
    std::optional<hwio::Result<DeviceRef>> result;
    provider().open(request, [&result](hwio::Result<DeviceRef> ref) { result = std::move(ref); });
    ASSERT_TRUE(result.has_value());
    const hwio::Result<DeviceRef> opened =
        result.value_or(DeviceRef{}); // (has_value() checked above)
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error().code, hwio::ErrorCode::UsageInvalidArgument);
    EXPECT_TRUE(waitFor([&] { return app().tabs()->count() == 1; }));
}

TEST_F(McpProvider, BusyMarkersAndCommandHint) {
    (void)app();
    auto ref = open("jlink:801000002");
    ASSERT_TRUE(ref.has_value());
    DeviceTabViewModel* tab = provider().tabFor("jlink:801000002");
    ASSERT_NE(tab, nullptr);
    bool cancelled = false;
    EXPECT_TRUE(provider()
                    .beginBusy("jlink:801000002", QStringLiteral("AI: Flashing…"),
                               [&cancelled] { cancelled = true; })
                    .has_value());
    EXPECT_EQ(tab->busyPage(), QStringLiteral("ai"));
    EXPECT_EQ(tab->busyText(), QStringLiteral("AI: Flashing…"));
    EXPECT_TRUE(tab->busyConfirmText().contains(QStringLiteral("AI assistant")));
    // The device bar's Cancel.
    hwio::gui::McpController::cancelOperation(tab);
    EXPECT_TRUE(cancelled);
    provider().endBusy("jlink:801000002");
    EXPECT_FALSE(tab->isBusy());

    const auto before = tab->consoleLog()->size(hwio::device::Channel::Shell);
    provider().marker("jlink:801000002", QStringLiteral("— AI (Claude Code): flash app.hex —"));
    EXPECT_EQ(tab->consoleLog()->size(hwio::device::Channel::Shell), before + 1);

    QSignalSpy ai(tab, &DeviceTabViewModel::aiStateChanged);
    provider().setAiCommandHint("jlink:801000002", true);
    EXPECT_TRUE(tab->aiCommandActive());
    provider().setAiCommandHint("jlink:801000002", false);
    EXPECT_FALSE(tab->aiCommandActive());
    EXPECT_EQ(ai.count(), 2);
}

TEST_F(McpProvider, ClosingTheTabEndsTheSessionOfACall) {
    (void)app();
    auto ref = open("jlink:801000002");
    ASSERT_TRUE(ref.has_value());
    DeviceTabViewModel* tab = provider().tabFor("jlink:801000002");
    app().closeTabObject(tab, false); // the user closes the AI's tab
    EXPECT_TRUE(waitFor([&] { return !ref->alive(); }));
    EXPECT_FALSE(provider().find("jlink:801000002").has_value());
}

// Step D1 (ADR-0243): Bluetooth tabs and the recently seen Bluetooth devices (fake BLE API).
TEST_F(McpProvider, BluetoothTabsAndSeenDevices) {
#ifndef HWIO_TEST_OVERRIDES
    GTEST_SKIP() << "the fake BLE API needs HWIO_TEST_OVERRIDES";
#endif
    const QByteArray saved = qgetenv("HWIO_FAKE_BLE_SCENARIO");
    qputenv("HWIO_FAKE_BLE_SCENARIO", QByteArray(HWIO_BLE_SCENARIOS_DIR "/chester.json"));
    setBluetooth(true);
    (void)app();
    hwio::gui::BleService* ble = app().bleService();
    ASSERT_NE(ble, nullptr);

    DeviceTabViewModel& user = *app().currentTabViewModel();
    ASSERT_TRUE(user.configureBle(QStringLiteral("chester"), QStringLiteral("f1:22:33:44:55:66"),
                                  QStringLiteral("CHESTER-ABC123")));
    const DeviceEntry tabEntry = entry("ble:F1:22:33:44:55:66");
    ASSERT_FALSE(tabEntry.id.empty());
    EXPECT_EQ(tabEntry.kind, "ble");
    EXPECT_EQ(tabEntry.state, DeviceEntry::State::Available);
    EXPECT_EQ(provider().tabFor("ble:f1:22:33:44:55:66"), &user);

    // A scan (a visible Bluetooth picker) fills the list of seen devices.
    ble->acquireScan();
    ASSERT_TRUE(waitFor([&] { return !entry("ble:C0:11:22:33:44:55").id.empty(); }, 20s));
    EXPECT_EQ(entry("ble:C0:11:22:33:44:55").displayName, "CHESTER-XYZ789");
    EXPECT_EQ(entry("ble:C0:11:22:33:44:55").state, DeviceEntry::State::Available);
    ble->releaseScan();

    // connect on the user's Bluetooth tab: that tab, no J-Link capabilities.
    auto ref = open("ble:F1:22:33:44:55:66");
    ASSERT_TRUE(ref.has_value()) << ref.error().detail;
    EXPECT_TRUE(ref->jlink.isNull());
    EXPECT_EQ(ref->console.data(), user.consoleSession());
    EXPECT_TRUE(waitFor([&] { return connected(*ref); }, 20s));
    EXPECT_EQ(provider().find("ble:F1:22:33:44:55:66").value_or(DeviceRef{}).console.data(),
              user.consoleSession());

    // A seen device without a tab: a new AI tab with the advertised name.
    auto other = open("ble:C0:11:22:33:44:55");
    ASSERT_TRUE(other.has_value()) << other.error().detail;
    DeviceTabViewModel* aiTab = provider().tabFor("ble:C0:11:22:33:44:55");
    ASSERT_NE(aiTab, nullptr);
    EXPECT_TRUE(aiTab->isBle());
    EXPECT_EQ(aiTab->bleDeviceName(), QStringLiteral("CHESTER-XYZ789"));
    EXPECT_EQ(aiTab->openedBy(), QStringLiteral("Claude Code"));
    destroyApp();
    qputenv("HWIO_FAKE_BLE_SCENARIO", saved);
}

// Review M3: an AI operation on a tab a user page holds busy is refused with internal.busy.
TEST_F(McpProvider, BusyUserPageRefusesAiOperations) {
    (void)app();
    ASSERT_TRUE(open("jlink:801000002").has_value());
    DeviceTabViewModel* tab = provider().tabFor("jlink:801000002");
    ASSERT_NE(tab, nullptr);
    tab->setBusy(QStringLiteral("flash"), QStringLiteral("Flashing…"), QStringLiteral("?"), {});
    const hwio::Status busy = provider().beginBusy("jlink:801000002", QStringLiteral("AI"), {});
    ASSERT_FALSE(busy.has_value());
    EXPECT_EQ(busy.error().code, hwio::ErrorCode::InternalBusy);
    EXPECT_EQ(tab->busyPage(), QStringLiteral("flash"));
    tab->clearBusy(QStringLiteral("flash"));
    EXPECT_EQ(provider().beginBusy("jlink:42", QStringLiteral("AI"), {}).error().code,
              hwio::ErrorCode::McpDeviceNotOpen);
}

// ADR-0260 (a): an AI client may close only the tabs it opened itself.
TEST_F(McpProvider, OnlyTheOpeningClientClosesItsTab) {
    (void)app();
    ASSERT_TRUE(open("jlink:801000002", "Client A").has_value());
    hwio::mcp::RequestContext other;
    other.clientKey = "Client B";
    const hwio::Status refused = provider().close("jlink:801000002", other);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, hwio::ErrorCode::McpToolNotAllowed);
    spin(50ms);
    EXPECT_EQ(app().tabs()->count(), 2);
    hwio::mcp::RequestContext owner;
    owner.clientKey = "Client A";
    EXPECT_TRUE(provider().close("jlink:801000002", owner).has_value());
    EXPECT_TRUE(waitFor([&] { return app().tabs()->count() == 1; }));
}

// ADR-0260 (b): the user changing an option of an AI tab takes it over; only the user's change is
// remembered for the probe, not the AI's session overrides.
TEST_F(McpProvider, UserChangeHandsTheTabOver) {
    (void)app();
    OpenRequest request;
    request.id = "jlink:801000002";
    request.speedKhz = 1000;
    request.context.client = {.name = "Claude Code", .version = "1"};
    request.context.clientKey = "Claude Code";
    std::optional<hwio::Result<DeviceRef>> result;
    provider().open(request, [&result](hwio::Result<DeviceRef> ref) { result = std::move(ref); });
    ASSERT_TRUE(result.has_value() && result->has_value());
    DeviceTabViewModel* tab = provider().tabFor("jlink:801000002");
    ASSERT_NE(tab, nullptr);
    QSignalSpy ai(tab, &DeviceTabViewModel::aiStateChanged);
    tab->connection()->setLatencyMs(25); // the user, in the Advanced popover
    EXPECT_TRUE(tab->openedBy().isEmpty());
    EXPECT_GE(ai.count(), 1);
    hwio::gui::SettingsStore& settings = *app().settingsStore();
    EXPECT_EQ(settings.value(QStringLiteral("probes/801000002/latencyMs")).toInt(), 25);
    EXPECT_EQ(settings.value(QStringLiteral("probes/801000002/speedKhz")).toInt(), 0); // not 1000
    EXPECT_EQ(tab->connection()->options().speedKhz, 1000); // the session keeps it
    app().saveState();
    EXPECT_TRUE(
        settings.value(QStringLiteral("ui/tabs")).toString().contains(QStringLiteral("801000002")));
    hwio::mcp::RequestContext owner;
    owner.clientKey = "Claude Code";
    EXPECT_EQ(provider().close("jlink:801000002", owner).error().code,
              hwio::ErrorCode::McpToolNotAllowed);

    // Choosing another device hands a tab over too.
    ASSERT_TRUE(open("jlink:801000001").has_value());
    DeviceTabViewModel* second = provider().tabFor("jlink:801000001");
    ASSERT_NE(second, nullptr);
    EXPECT_FALSE(second->openedBy().isEmpty());
    second->disconnectDevice();
    second->chooseAnotherProbe();
    EXPECT_TRUE(second->openedBy().isEmpty());
}

// Review M4: the self-declared client name is sanitised before it reaches a tab.
TEST_F(McpProvider, ClientNameIsSanitised) {
    (void)app();
    const std::string evil = "Claude\u202E edoC\nNext line" + std::string(1000, 'x');
    ASSERT_TRUE(open("jlink:801000002", evil).has_value());
    DeviceTabViewModel* tab = provider().tabFor("jlink:801000002");
    ASSERT_NE(tab, nullptr);
    const QString name = tab->openedBy();
    EXPECT_LE(name.size(), 64);
    EXPECT_FALSE(name.contains(QChar(0x202E)));
    EXPECT_FALSE(name.contains(QLatin1Char('\n')));
    EXPECT_TRUE(name.startsWith(QStringLiteral("Claude edoC Next line")));
    EXPECT_TRUE(name.endsWith(QChar(0x2026)));
}

// Plan 0036 (ADR-0105, ADR-0132): while an AI operation runs on a tab of the probe, the J-Link
// firmware update is blocked with "Used by an AI assistant" (also for the tab's own banner).
TEST_F(McpProvider, AiOperationBlocksTheFirmwareUpdate) {
    (void)app();
    ASSERT_TRUE(open("jlink:801000002").has_value());
    DeviceTabViewModel* tab = provider().tabFor("jlink:801000002");
    ASSERT_NE(tab, nullptr);
    hwio::gui::ProbeFirmwareViewModel& firmware = *app().probeFirmware();
    const QString aiReason = QStringLiteral("Used by an AI assistant");
    constexpr std::uint32_t serial = 801000002;
    EXPECT_FALSE(provider().aiOperationRuns(serial));
    EXPECT_EQ(firmware.blockedReason(serial, tab), QString()); // the banner's own tab
    EXPECT_EQ(firmware.blockedReasonFor(QStringLiteral("801000002")),
              QStringLiteral("Disconnect tab 2 first."));

    ASSERT_TRUE(
        provider().beginBusy("jlink:801000002", QStringLiteral("AI: Flashing…"), {}).has_value());
    EXPECT_TRUE(provider().aiOperationRuns(serial));
    EXPECT_FALSE(provider().aiOperationRuns(801000003));
    EXPECT_EQ(firmware.blockedReason(serial, tab), aiReason);
    EXPECT_EQ(firmware.blockedReasonFor(QStringLiteral("801000002")), aiReason);
    EXPECT_NE(firmware.blockedReason(801000003), aiReason);
    firmware.requestFromTab(*tab); // ignored: the tab stays connected
    EXPECT_EQ(firmware.state(), hwio::gui::ProbeFirmwareViewModel::Idle);
    EXPECT_NE(tab->session(), nullptr);
    firmware.requestForSerial(QStringLiteral("801000002"));
    EXPECT_EQ(firmware.state(), hwio::gui::ProbeFirmwareViewModel::Idle);
    EXPECT_TRUE(app().toasts().contains(aiReason));

    provider().endBusy("jlink:801000002");
    EXPECT_FALSE(provider().aiOperationRuns(serial));
    EXPECT_EQ(firmware.blockedReason(serial, tab), QString());
    // A user operation on the tab keeps its own reason.
    tab->setBusy(QStringLiteral("flash"), QStringLiteral("Flashing…"), QStringLiteral("?"), {});
    EXPECT_FALSE(provider().aiOperationRuns(serial));
    EXPECT_EQ(firmware.blockedReason(serial, tab),
              QStringLiteral("Wait for the current operation to finish."));
    tab->clearBusy(QStringLiteral("flash"));
}

// Plan 0036 (ADR-0257): a probe reserved for a J-Link firmware update refuses the AI's connect
// with the registry's probe.open_failed, for a new AI tab (closed again) and for a user's tab.
TEST_F(McpProvider, ReservedProbeRefusesConnect) {
    (void)app();
    hwio::gui::ProbeFirmwareViewModel& firmware = *app().probeFirmware();
    firmware.requestForSerial(QStringLiteral("801000002"));
    ASSERT_NE(firmware.state(), hwio::gui::ProbeFirmwareViewModel::Idle);
    ASSERT_TRUE(app().sessionRegistry().reserved(801000002));

    auto refused = open("jlink:801000002");
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, hwio::ErrorCode::ProbeOpenFailed);
    EXPECT_NE(refused.error().message.find("is being updated"), std::string::npos)
        << refused.error().message;
    EXPECT_TRUE(waitFor([&] { return app().tabs()->count() == 1; })); // the new tab closed again

    // A user's tab configured for the probe (no session) is not connected either.
    DeviceTabViewModel* user = app().currentTabViewModel();
    ASSERT_TRUE(user->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000002,
                                QStringLiteral("Test Probe")));
    refused = open("jlink:801000002");
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, hwio::ErrorCode::ProbeOpenFailed);
    EXPECT_EQ(user->session(), nullptr);
    EXPECT_EQ(app().tabs()->count(), 1);

    // Released when the request ends: connect works again.
    ASSERT_TRUE(
        waitFor([&] { return firmware.state() != hwio::gui::ProbeFirmwareViewModel::Reading; }));
    firmware.cancel();
    ASSERT_TRUE(waitFor([&] { return !app().sessionRegistry().reserved(801000002); }));
    EXPECT_TRUE(open("jlink:801000002").has_value());
    EXPECT_NE(user->session(), nullptr);
}

TEST(McpProviderIds, ParseIds) {
    EXPECT_EQ(GuiDeviceProvider::probeSerialOf("jlink:801000001"), 801000001U);
    EXPECT_FALSE(GuiDeviceProvider::probeSerialOf("jlink:0801").has_value());
    EXPECT_FALSE(GuiDeviceProvider::probeSerialOf("jlink:4294967296").has_value());
    EXPECT_FALSE(GuiDeviceProvider::probeSerialOf("ble:1").has_value());
    EXPECT_EQ(GuiDeviceProvider::bleIdOf("ble:f1:22:33:44:55:66"), "F1:22:33:44:55:66");
    EXPECT_EQ(GuiDeviceProvider::bleIdOf("ble:"), "");
    EXPECT_EQ(GuiDeviceProvider::bleIdOf("jlink:1"), "");
}

} // namespace
