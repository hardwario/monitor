// Plan 0012: Bluetooth in the GUI against the fake BLE API (HWIO_FAKE_BLE_SCENARIO, ADR-0087):
// the BlePickerViewModel (profile filter, Show all, name filter, selection by id, stale and removed
// rows, in-use badges, adapter errors), the Bluetooth tab (connect, console over ConsoleSession,
// markers, log pane, Reset, Flash disabled), pairing from the ErrorPanel (ADR-0227), the per-device
// options and the tab restore with `transport` (ADR-0083).

#include "gui_test_support.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/session_events.h"
#include "hwio/hw/ble/ble_scanner.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/ble_picker_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/console_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "workers/ble_service.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QModelIndex>
#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QVariant>
#include <QtContainerFwd>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using hwio::device::Channel;
using hwio::device::ConnectionState;
using hwio::device::Direction;
using hwio::gui::AppViewModel;
using hwio::gui::BlePickerViewModel;
using hwio::gui::BleService;
using hwio::gui::ConsoleViewModel;
using hwio::gui::DeviceTabViewModel;
using hwio::gui::test::spin;
using hwio::gui::test::waitFor;
using namespace std::chrono_literals;

constexpr auto scanWait = 15s; // the first fake scan period ends after BleService::scanPeriod

class BleGuiTest : public hwio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
#ifndef HWIO_TEST_OVERRIDES
        GTEST_SKIP() << "the fake BLE API needs HWIO_TEST_OVERRIDES";
#endif
        m_savedScenario = qgetenv("HWIO_FAKE_BLE_SCENARIO");
        useBleScenario("chester.json");
    }
    void TearDown() override {
        m_app.reset();
        qputenv("HWIO_FAKE_BLE_SCENARIO", m_savedScenario);
        GuiTest::TearDown();
    }

    // Read by the BLE thread when the host starts (lazily, after this call).
    static void useBleScenario(const char* name) {
        qputenv("HWIO_FAKE_BLE_SCENARIO",
                QByteArray(HWIO_BLE_SCENARIOS_DIR) + QByteArray("/") + QByteArray(name));
    }

    AppViewModel& app() {
        if (!m_app) {
            m_app = makeApp();
        }
        return *m_app;
    }
    DeviceTabViewModel& tab() { return *app().currentTabViewModel(); }

    void writeRaw(const QString& key, const QVariant& value) const {
        QSettings raw(QString::fromStdU16String(settingsFile().u16string()), QSettings::IniFormat);
        raw.setValue(QStringLiteral("meta/schemaVersion"), 1);
        raw.setValue(key, value);
    }
    [[nodiscard]] QString readRaw(const QString& key) const {
        const QSettings raw(QString::fromStdU16String(settingsFile().u16string()),
                            QSettings::IniFormat);
        return raw.value(key).toString();
    }

    static QString nameAt(const BlePickerViewModel& picker, int row) {
        return picker.data(picker.index(row), BlePickerViewModel::NameRole).toString();
    }
    static QStringList names(const BlePickerViewModel& picker) {
        QStringList list;
        for (int row = 0; row < picker.count(); ++row) {
            list.append(nameAt(picker, row));
        }
        return list;
    }

    // The Bluetooth view of the current tab, scanned until `count` rows show.
    BlePickerViewModel& scannedPicker(int count = 2) {
        tab().setTransport(DeviceTabViewModel::bleTransport());
        BlePickerViewModel& picker = *tab().blePicker();
        picker.setActive(true);
        EXPECT_TRUE(waitFor([&] { return picker.count() >= count; }, scanWait));
        return picker;
    }

    static std::vector<std::string> shellMarkers(const hwio::device::ConsoleLog& log,
                                                 hwio::device::ChannelMask mask) {
        std::vector<std::string> texts;
        for (const auto& line : log.linesSince(0, mask)) {
            if (line.dir == Direction::Marker) {
                texts.push_back(line.text);
            }
        }
        return texts;
    }

    bool connectFirst() {
        BlePickerViewModel& picker = scannedPicker();
        picker.choose(0);
        if (!picker.canConnect()) {
            return false;
        }
        picker.connectSelected();
        return waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; });
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    QByteArray m_savedScenario;
};

// ---- picker -------------------------------------------------------------------------------------

TEST_F(BleGuiTest, PickerListsProfileDevicesSortedByRssi) {
    BlePickerViewModel& picker = scannedPicker();
    // chester.json: two CHESTERs, a phone and a Ruuvi tag (no profile: hidden).
    EXPECT_EQ(names(picker),
              (QStringList{QStringLiteral("CHESTER-ABC123"), QStringLiteral("CHESTER-XYZ789")}));
    EXPECT_EQ(picker.data(picker.index(0), BlePickerViewModel::RssiRole).toInt(), -58);
    EXPECT_EQ(picker.data(picker.index(0), BlePickerViewModel::SignalLevelRole).toInt(), 3);
    EXPECT_EQ(picker.data(picker.index(1), BlePickerViewModel::SignalLevelRole).toInt(), 1);
    EXPECT_EQ(picker.data(picker.index(0), BlePickerViewModel::ProfileRole).toString(),
              QStringLiteral("chester"));
    EXPECT_TRUE(picker.data(picker.index(0), BlePickerViewModel::ConnectableRole).toBool());
    // Two connectable rows: nothing preselected.
    EXPECT_EQ(picker.selectedRow(), -1);
    EXPECT_FALSE(picker.canConnect());
    EXPECT_FALSE(picker.adapterError().valid());
}

TEST_F(BleGuiTest, ShowAllAndFilter) {
    BlePickerViewModel& picker = scannedPicker();
    picker.setShowAll(true);
    EXPECT_TRUE(waitFor([&] { return picker.count() == 4; }, scanWait));
    // Plan 0033 (ADR-0235): profile devices first, then the Phone (-40 dBm) before the Ruuvi tag.
    EXPECT_EQ(nameAt(picker, 0), QStringLiteral("CHESTER-ABC123"));
    EXPECT_EQ(nameAt(picker, 2), QStringLiteral("Phone"));
    EXPECT_EQ(picker.data(picker.index(2), BlePickerViewModel::ProfileRole).toString(), QString());
    EXPECT_EQ(picker.data(picker.index(2), BlePickerViewModel::ProfileNameRole).toString(),
              QStringLiteral("Unknown device"));

    picker.setFilterText(QStringLiteral("xyz"));
    EXPECT_EQ(names(picker), QStringList{QStringLiteral("CHESTER-XYZ789")});
    // By id too, case-insensitive.
    picker.setFilterText(QStringLiteral("aa:bb:cc:dd:ee:01"));
    EXPECT_EQ(names(picker), QStringList{QStringLiteral("Phone")});
    // An unknown device connects with the device type picked below (CHESTER).
    picker.choose(0);
    EXPECT_TRUE(picker.canConnect());
}

TEST_F(BleGuiTest, SelectionFollowsTheDeviceWhenRowsMove) {
    BlePickerViewModel& picker = scannedPicker();
    picker.choose(1);
    ASSERT_EQ(nameAt(picker, picker.selectedRow()), QStringLiteral("CHESTER-XYZ789"));
    picker.setShowAll(true); // Phone and Ruuvi come in above
    EXPECT_TRUE(waitFor([&] { return picker.count() == 4; }, scanWait));
    ASSERT_GE(picker.selectedRow(), 0);
    EXPECT_EQ(nameAt(picker, picker.selectedRow()), QStringLiteral("CHESTER-XYZ789"));
    // Filtered out: the selection goes, and the only remaining row is preselected.
    picker.setFilterText(QStringLiteral("ABC"));
    ASSERT_EQ(picker.selectedRow(), 0);
    EXPECT_EQ(picker.selectedId(), QStringLiteral("F1:22:33:44:55:66"));
}

TEST_F(BleGuiTest, ExactlyOneMatchIsPreselected) {
    BlePickerViewModel& picker = scannedPicker();
    picker.setFilterText(QStringLiteral("ABC123"));
    EXPECT_EQ(picker.count(), 1);
    EXPECT_EQ(picker.selectedRow(), 0);
    EXPECT_TRUE(picker.canConnect());
}

TEST_F(BleGuiTest, StaleRowsAreDimmedThenRemoved) {
    BleService service;
    auto offset = std::chrono::milliseconds(0);
    BlePickerViewModel picker(
        &service,
        BlePickerViewModel::Hooks{
            .tabUsingDevice = {},
            .otherProgram = {},
            .now = [&offset] { return std::chrono::steady_clock::now() + offset; },
            .openUrl = {},
            .devices = {}},
        nullptr);
    picker.setActive(true);
    ASSERT_TRUE(waitFor([&] { return picker.count() == 2; }, scanWait));
    picker.setActive(false); // no new advertisements
    EXPECT_FALSE(picker.data(picker.index(0), BlePickerViewModel::StaleRole).toBool());
    offset = BlePickerViewModel::staleAfter + 1s;
    picker.update();
    ASSERT_EQ(picker.count(), 2);
    EXPECT_TRUE(picker.data(picker.index(0), BlePickerViewModel::StaleRole).toBool());
    offset = BlePickerViewModel::removeAfter + 1s;
    picker.update();
    EXPECT_EQ(picker.count(), 0);
    service.stop();
}

// Plan 0033 (ADR-0235): the picker order with scripted smoothed RSSI values.
class BlePickerOrderTest : public hwio::gui::test::GuiTest {
protected:
    static hwio::ble::BleDeviceInfo device(const std::string& id, double rssi,
                                           const std::string& profile = "chester") {
        return hwio::ble::BleDeviceInfo{.id = id,
                                        .name = "CHESTER-" + id,
                                        .rssi = static_cast<int>(rssi),
                                        .rssiSmoothed = rssi,
                                        .rssiSampled = {},
                                        .nusAdvertised = true,
                                        .profileId = profile,
                                        .lastSeen = std::chrono::steady_clock::now()};
    }
    BlePickerViewModel& picker() {
        if (!m_picker) {
            m_picker = std::make_unique<BlePickerViewModel>(
                nullptr,
                BlePickerViewModel::Hooks{.tabUsingDevice = {},
                                          .otherProgram = {},
                                          .now = {},
                                          .openUrl = {},
                                          .devices = [this] { return m_devices; }},
                nullptr);
        }
        return *m_picker;
    }
    QStringList ids() {
        QStringList list;
        for (int row = 0; row < picker().count(); ++row) {
            list.append(picker().data(picker().index(row), BlePickerViewModel::IdRole).toString());
        }
        return list;
    }
    void set(std::vector<hwio::ble::BleDeviceInfo> devices) {
        m_devices = std::move(devices);
        picker().update();
    }

private:
    std::vector<hwio::ble::BleDeviceInfo> m_devices;
    std::unique_ptr<BlePickerViewModel> m_picker;
};

TEST_F(BlePickerOrderTest, ARowOvertakesOnlyWhenStrongerByTheMargin) {
    set({device("A", -60), device("B", -62), device("C", -70)});
    EXPECT_EQ(ids(), (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
    // B is now stronger than A, but by less than 6 dB: A stays above.
    set({device("A", -62), device("B", -57.5), device("C", -70)});
    EXPECT_EQ(ids(), (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
    // 6 dB or more: B moves up; C passes nobody.
    set({device("A", -64), device("B", -58), device("C", -66)});
    EXPECT_EQ(ids(), (QStringList{QStringLiteral("B"), QStringLiteral("A"), QStringLiteral("C")}));
    // The shown RSSI is the smoothed value, whole dBm.
    EXPECT_EQ(picker().data(picker().index(1), BlePickerViewModel::RssiRole).toInt(), -64);
}

TEST_F(BlePickerOrderTest, ProfileDevicesFirstAndUnknownRssiLast) {
    picker().setShowAll(true);
    set({device("phone", -40, ""), device("A", -80), device("B", 0), device("tag", 0, "")});
    EXPECT_EQ(ids(), (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("phone"),
                                  QStringLiteral("tag")}));
}

TEST_F(BlePickerOrderTest, TheOrderIsFrozenWhilePointedAt) {
    set({device("A", -60), device("B", -70)});
    picker().setFreezeOrder(true);
    // B is far stronger now and a new device appears: the order stays, the new one is appended.
    set({device("N", -40), device("A", -80), device("B", -50)});
    EXPECT_EQ(ids(), (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("N")}));
    picker().setFreezeOrder(false); // the pointer left: the order catches up
    EXPECT_EQ(ids(), (QStringList{QStringLiteral("N"), QStringLiteral("B"), QStringLiteral("A")}));
}

TEST_F(BleGuiTest, InUseByAnotherProgramDisablesTheRow) {
    BleService service;
    BlePickerViewModel picker(
        &service,
        BlePickerViewModel::Hooks{.tabUsingDevice = [](const QString&) { return -1; },
                                  .otherProgram =
                                      [](const QString& id) {
                                          return id == QStringLiteral("F1:22:33:44:55:66")
                                                     ? QStringLiteral("hwio, PID 42")
                                                     : QString();
                                      },
                                  .now = {},
                                  .openUrl = {},
                                  .devices = {}},
        nullptr);
    picker.setActive(true);
    ASSERT_TRUE(waitFor([&] { return picker.count() == 2; }, scanWait));
    EXPECT_EQ(picker.data(picker.index(0), BlePickerViewModel::InUseOtherRole).toString(),
              QStringLiteral("hwio, PID 42"));
    EXPECT_FALSE(picker.data(picker.index(0), BlePickerViewModel::ConnectableRole).toBool());
    // The other CHESTER is the only connectable one: preselected; the disabled row cannot be
    // chosen.
    EXPECT_EQ(picker.selectedRow(), 1);
    picker.choose(0);
    EXPECT_EQ(picker.selectedRow(), 1);
    EXPECT_TRUE(picker.canConnect());
    picker.setActive(false);
    service.stop();
}

TEST_F(BleGuiTest, AdapterOffShowsTheError) {
    useBleScenario("adapter_off.json");
    tab().setTransport(DeviceTabViewModel::bleTransport());
    BlePickerViewModel& picker = *tab().blePicker();
    picker.setActive(true);
    ASSERT_TRUE(waitFor([&] { return picker.adapterError().valid(); }, 5s));
    EXPECT_EQ(picker.adapterError().code(), QStringLiteral("ble.adapter_off"));
    EXPECT_EQ(picker.count(), 0);
    EXPECT_FALSE(picker.scanning());
}

TEST_F(BleGuiTest, NoBluetoothHostWithoutBluetoothUse) {
    ASSERT_NE(app().bleService(), nullptr);
    EXPECT_FALSE(app().bleService()->hostCreated()); // ADR-0228: lazy
    tab().setTransport(DeviceTabViewModel::bleTransport());
    EXPECT_FALSE(app().bleService()->hostCreated()); // the view is not visible yet
    tab().blePicker()->setActive(true);
    EXPECT_TRUE(app().bleService()->hostCreated());
}

// ---- Bluetooth tab ------------------------------------------------------------------------------

TEST_F(BleGuiTest, ConnectsAndUsesTheConsole) {
    ASSERT_TRUE(connectFirst());
    DeviceTabViewModel& current = tab();
    EXPECT_TRUE(current.isBle());
    EXPECT_EQ(current.title(), QStringLiteral("CHESTER · CHESTER-ABC123"));
    EXPECT_EQ(current.session(), nullptr); // no J-Link session: no operations
    ASSERT_NE(current.consoleSession(), nullptr);
    ASSERT_NE(current.bleSession(), nullptr);
    EXPECT_EQ(current.connection()->deviceName(), QStringLiteral("CHESTER-ABC123"));
    EXPECT_TRUE(current.connection()->isBle());
    EXPECT_FALSE(current.pageDisabledReason(QStringLiteral("flash")).isEmpty());
    EXPECT_TRUE(current.pageDisabledReason(QStringLiteral("console")).isEmpty());
    current.setCurrentPage(QStringLiteral("flash"));
    EXPECT_EQ(current.currentPage(), QStringLiteral("console"));
    EXPECT_FALSE(current.ensureSession()); // operations need a J-Link

    // The markers go to the shell only (ADR-0082): the log pane stays hidden.
    const auto shell = shellMarkers(*current.consoleLog(), hwio::device::ChannelMask::Shell);
    ASSERT_FALSE(shell.empty());
    EXPECT_EQ(shell.back(), "— connected (Bluetooth) —");
    EXPECT_TRUE(shellMarkers(*current.consoleLog(), hwio::device::ChannelMask::Log).empty());

    auto* console =
        qobject_cast<ConsoleViewModel*>(current.pageViewModel(QStringLiteral("console")));
    ASSERT_NE(console, nullptr);
    EXPECT_FALSE(console->hasReset());
    EXPECT_FALSE(console->canReset());
    EXPECT_FALSE(console->showLogPane());
    EXPECT_TRUE(waitFor([&] { return console->inputEnabled(); }));
    EXPECT_TRUE(console->send(QStringLiteral("version")));
    EXPECT_TRUE(waitFor([&] {
        return std::ranges::any_of(
            current.consoleLog()->linesSince(0, hwio::device::ChannelMask::Shell),
            [](const auto& line) {
                return line.text.find("Firmware version: v3.1.0") != std::string::npos;
            });
    }));

    current.disconnectDevice();
    EXPECT_EQ(current.state(), DeviceTabViewModel::Disconnected);
    EXPECT_EQ(current.consoleSession(), nullptr);
    EXPECT_EQ(shellMarkers(*current.consoleLog(), hwio::device::ChannelMask::Shell).back(),
              "— disconnected —");
    // Reconnect keeps the tab's log (ADR-0063).
    const auto before = current.consoleLog()->size(Channel::Shell);
    current.connectDevice();
    EXPECT_TRUE(waitFor([&] { return current.state() == DeviceTabViewModel::Connected; }));
    EXPECT_GT(current.consoleLog()->size(Channel::Shell), before);
}

TEST_F(BleGuiTest, ChoosingTheDeviceOfAnotherTabSwitchesToIt) {
    ASSERT_TRUE(connectFirst());
    DeviceTabViewModel* first = &tab();
    app().newTab();
    ASSERT_NE(&tab(), first);
    BlePickerViewModel& picker = scannedPicker();
    EXPECT_EQ(picker.data(picker.index(0), BlePickerViewModel::InUseTabRole).toInt(), 1);
    QSignalSpy activation(&tab(), &DeviceTabViewModel::tabActivationRequested);
    picker.choose(0);
    ASSERT_EQ(activation.count(), 1);
    EXPECT_EQ(activation.first().first().toInt(), 0);
    EXPECT_EQ(picker.selectedRow(), 1);   // the free CHESTER stays preselected
    EXPECT_FALSE(picker.isSelectable(0)); // the arrow keys skip the device of another tab
    EXPECT_TRUE(picker.isSelectable(1));
    EXPECT_FALSE(picker.isSelectable(-1));
    EXPECT_FALSE(picker.isSelectable(picker.count()));
}

TEST_F(BleGuiTest, PairFromTheErrorPanelThenConnects) {
    useBleScenario("pair_then_connect.json");
    BlePickerViewModel& picker = scannedPicker(1);
    picker.choose(0);
    picker.connectSelected();
    hwio::gui::ConnectionViewModel& connection = *tab().connection();
    ASSERT_TRUE(waitFor([&] { return connection.pairingError(); }));
    EXPECT_EQ(connection.error().code(), QStringLiteral("ble.not_paired"));
    EXPECT_FALSE(connection.pairingHelp().isEmpty());
#ifdef Q_OS_MACOS
    EXPECT_TRUE(connection.customActionText().isEmpty());
#else
    EXPECT_EQ(connection.customActionText(), QStringLiteral("Pair"));
    connection.triggerCustomAction();
    EXPECT_TRUE(tab().pairing());
    EXPECT_FALSE(connection.actionEnabled());
    EXPECT_TRUE(waitFor([&] { return tab().state() == DeviceTabViewModel::Connected; }));
    EXPECT_FALSE(tab().pairing());
    EXPECT_FALSE(connection.error().valid());
#endif
}

TEST_F(BleGuiTest, PairingPromptStateText) {
    useBleScenario("pairing_prompt.json");
    BlePickerViewModel& picker = scannedPicker(1);
    picker.choose(0);
    picker.connectSelected();
    hwio::gui::ConnectionViewModel& connection = *tab().connection();
    ASSERT_TRUE(waitFor([&] { return connection.pairingPending(); }));
    EXPECT_TRUE(connection.stateText().startsWith(QStringLiteral("Waiting for pairing")));
    EXPECT_TRUE(connection.passkeyHint().contains(QStringLiteral("123456")));
    EXPECT_TRUE(waitFor([&] { return tab().state() == DeviceTabViewModel::Connected; }));
    EXPECT_FALSE(connection.pairingPending());
    EXPECT_TRUE(connection.stateText().startsWith(QStringLiteral("Connected")));
}

TEST_F(BleGuiTest, OptionsAreRememberedPerDevice) {
    ASSERT_TRUE(tab().configureBle(QStringLiteral("chester"), QStringLiteral("f1:22:33:44:55:66"),
                                   QStringLiteral("CHESTER-ABC123")));
    EXPECT_EQ(tab().bleDeviceId(), QStringLiteral("F1:22:33:44:55:66")); // canonical (B6)
    hwio::gui::ConnectionViewModel& connection = *tab().connection();
    EXPECT_FALSE(connection.autoReconnect()); // ADR-0229
    connection.setAutoReconnect(true);
    connection.setLogToFile(false);

    app().newTab();
    ASSERT_TRUE(tab().configureBle(QStringLiteral("chester"), QStringLiteral("F1:22:33:44:55:66"),
                                   QString()));
    EXPECT_TRUE(tab().connection()->autoReconnect());
    EXPECT_FALSE(tab().connection()->logToFile());
    EXPECT_EQ(tab().title(), QStringLiteral("CHESTER · F1:22:33:44:55:66")); // no name: the id
}

TEST_F(BleGuiTest, TabsAreRestoredWithTheirTransport) {
    const QJsonArray tabs{
        QJsonObject{{QStringLiteral("profile"), QStringLiteral("chester")},
                    {QStringLiteral("target"), QStringLiteral("app")},
                    {QStringLiteral("sn"), 801000001}}, // written before plan 0012
        QJsonObject{{QStringLiteral("transport"), QStringLiteral("ble")},
                    {QStringLiteral("profile"), QStringLiteral("chester")},
                    {QStringLiteral("id"), QStringLiteral("F1:22:33:44:55:66")},
                    {QStringLiteral("name"), QStringLiteral("CHESTER-ABC123")}}};
    const QJsonObject root{{QStringLiteral("current"), 1}, {QStringLiteral("tabs"), tabs}};
    writeRaw(QStringLiteral("ui/tabs"),
             QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
    AppViewModel& restored = app();
    ASSERT_EQ(restored.tabs()->count(), 2);
    EXPECT_FALSE(restored.tabs()->at(0)->isBle());
    DeviceTabViewModel* bleTab = restored.tabs()->at(1);
    EXPECT_TRUE(bleTab->isBle());
    EXPECT_EQ(bleTab->state(), DeviceTabViewModel::Disconnected); // ADR-0083: not connected
    EXPECT_EQ(bleTab->title(), QStringLiteral("CHESTER · CHESTER-ABC123"));
    EXPECT_EQ(restored.currentTab(), 1);

    restored.saveState();
    const QJsonArray saved = QJsonDocument::fromJson(readRaw(QStringLiteral("ui/tabs")).toUtf8())
                                 .object()
                                 .value(QStringLiteral("tabs"))
                                 .toArray();
    ASSERT_EQ(saved.size(), 2);
    EXPECT_EQ(saved.at(0).toObject().value(QStringLiteral("transport")).toString(),
              QStringLiteral("jlink"));
    EXPECT_EQ(saved.at(1).toObject().value(QStringLiteral("transport")).toString(),
              QStringLiteral("ble"));
    EXPECT_EQ(saved.at(1).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("F1:22:33:44:55:66"));
}

TEST_F(BleGuiTest, NewTabsOpenInTheLastTransport) {
    tab().setTransport(DeviceTabViewModel::bleTransport());
    app().newTab();
    EXPECT_EQ(tab().transport(), DeviceTabViewModel::bleTransport());
    tab().setTransport(DeviceTabViewModel::jlinkTransport());
    app().newTab();
    EXPECT_EQ(tab().transport(), DeviceTabViewModel::jlinkTransport());
}

} // namespace
