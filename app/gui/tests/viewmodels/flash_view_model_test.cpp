// FlashViewModel, FirmwareSourceViewModel, RecentSourcesModel and FirmwareLoader
// (plan/features/0006-m3-flash-settings.md, D1-D4) against the fake J-Link library and the fake
// nRF Util: the flash state machine for each kind (incl. the LTE bundle step 1/2 -> 2/2 and a
// modem failure), cross-target phases, flashing from a disconnected tab, cancel, kind-mismatch
// hints, catalog ID validation and download (local HTTP fixture), the recent list, the
// confirmation texts, busy (internal.busy) and the interrupted flash (ADR-0109).

#include "gui_test_support.h"
#include "hex_builder.h"
#include "http_fixture.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/program_progress.h"
#include "hwio/hw/nrfutil/nrfutil.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/tunnel.h"
#include "pages/firmware_services.h"
#include "settings_store.h"
#include "support/fake_jlink_scenario.h"
#include "support/test_page_view_model.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/dependency_status.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/firmware_source_view_model.h"
#include "viewmodels/flash_view_model.h"
#include "viewmodels/recent_sources_model.h"
#include "viewmodels/remote_share_view_model.h"
#include "viewmodels/ui_mode.h"
#include "viewmodels/ui_mode_controller.h"
#include "wizard/lte_update_wizard.h"
#include "wizard/wizard.h"
#include "wizard/wizard_view_model.h"
#include "workers/firmware_loader.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hwio::ErrorCode;
using hwio::firmware::FirmwareImage;
using hwio::firmware::Segment;
using hwio::gui::AppViewModel;
using hwio::gui::DeviceTabViewModel;
using hwio::gui::FirmwareSourceViewModel;
using hwio::gui::FlashViewModel;
using hwio::gui::RecentSourcesModel;
using hwio::gui::test::waitFor;
using Kind = FirmwareSourceViewModel::Kind;

// One probe with RTT; `chip` is the scenario's chip object (default: an nRF52840).
std::string probeScenario(const std::string& chip = "{}") {
    return std::string(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
        "product": "Test Probe", "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}]},
        "chip": )") +
           chip + "}]}";
}

QString randomId() { return QUuid::createUuid().toString(QUuid::Id128); }

class FlashTest : public hwio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        qputenv("HWIO_NRFUTIL", (root() / "no-nrfutil" / "nrfutil").string().c_str());
    }
    void TearDown() override {
        m_app.reset();
        qunsetenv("HWIO_NRFUTIL");
        qunsetenv("HWIO_FAKE_NRFUTIL_MODE");
        qunsetenv("HWIO_CATALOG_URL");
        qunsetenv("HWIO_DESIGNATED_URL");
        qunsetenv("HWIO_DESIGNATED_SHA256");
        GuiTest::TearDown();
    }

    void start(const std::string& scenario, const QString& target = QStringLiteral("app")) {
        m_scenario = scenario;
        useScenario(m_scenario.c_str());
        m_app = makeApp();
        tab().configure(QStringLiteral("chester"), target, 801000001, QStringLiteral("Test Probe"));
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    // Plan 0033: a board with the APP and LTE connectors; moveCable() moves the J-Link.
    void startBoard(const std::string& lteChip = "") {
        moveCable("app");
        start(std::string(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
            "product": "Test Probe", "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
            "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}]},
            "connectors": {"file": ")") +
              (root() / "connector").generic_string() +
              R"(", "chips": {"app": {"memory": [{"address": 4096, "hex": "a5a5a5a5"}]},
              "lte": {"family": "nrf91")" +
              lteChip + "}}}}]}");
    }
    void moveCable(const std::string& connector) const {
        QFile file(QString::fromStdString((root() / "connector").string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray::fromStdString(connector));
    }
    hwio::gui::LteUpdateWizard* openLteWizard() {
        tab().openWizard(QStringLiteral("lte-update"));
        return qobject_cast<hwio::gui::LteUpdateWizard*>(app().wizard()->wizard());
    }
    static bool loadInto(hwio::gui::FirmwareSourceViewModel& target, const fs::path& path) {
        target.loadFile(QUrl::fromLocalFile(QString::fromStdString(path.string())));
        return waitFor([&] { return target.sourceState() != FirmwareSourceViewModel::Loading; });
    }
    static bool reachStage(const hwio::gui::LteUpdateWizard& wizard, int stage) {
        return waitFor([&] { return wizard.stage() == stage; }, std::chrono::seconds(60));
    }
    // Plan 0045 (ADR-0360): the expert actions are Expert mode.
    void enableExpertActions() {
        ASSERT_TRUE(
            app().settingsStore()->setValue(QStringLiteral("ui/mode"), QStringLiteral("expert")));
    }
    [[nodiscard]] DeviceTabViewModel& tab() { return *m_app->currentTabViewModel(); }
    [[nodiscard]] FlashViewModel& flash() {
        return *qobject_cast<FlashViewModel*>(tab().pageViewModel(FlashViewModel::pageId));
    }
    [[nodiscard]] FirmwareSourceViewModel& source() { return *flash().source(); }

    [[nodiscard]] fs::path writeFile(const std::string& name, const QByteArray& bytes) const {
        const fs::path path = root() / name;
        QFile file(QString::fromStdString(path.string()));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
        return path;
    }

    [[nodiscard]] static FirmwareImage image(std::uint32_t address = 0x1000,
                                             std::size_t size = 4096, std::uint8_t value = 0x5A) {
        std::vector<Segment> segments;
        segments.push_back(
            Segment{.address = address, .data = std::vector<std::byte>(size, std::byte{value})});
        return *FirmwareImage::fromSegments(std::move(segments));
    }

    [[nodiscard]] fs::path hexFile(const std::string& name = "app.hex",
                                   const FirmwareImage& content = image()) const {
        return writeFile(name, QByteArray::fromStdString(hwio::firmware::test::toHex(content)));
    }

    [[nodiscard]] fs::path bundleFile() const {
        const QByteArray modem = hwio::firmware::test::zipBytes(
            {{.name = "firmware.update.image.segments.0.hex", .data = ":00000001FF\n"}});
        return writeFile("bundle.zip", hwio::firmware::test::zipBytes(
                                           {{.name = "modem.zip", .data = modem.toStdString()},
                                            {.name = "application.hex",
                                             .data = hwio::firmware::test::toHex(image())}}));
    }

    [[nodiscard]] fs::path modemFile() const {
        return writeFile(
            "mfw_nrf91x1_2.0.2.zip",
            hwio::firmware::test::zipBytes(
                {{.name = "firmware.update.image.segments.0.hex", .data = ":00000001FF\n"}}));
    }

    bool load(const fs::path& path) {
        source().loadFile(QUrl::fromLocalFile(QString::fromStdString(path.string())));
        return waitFor(
            [this] { return source().sourceState() != FirmwareSourceViewModel::Loading; });
    }

    bool finished() {
        return waitFor(
            [this] {
                return flash().opState() != FlashViewModel::Running &&
                       flash().opState() != FlashViewModel::Cancelling;
            },
            std::chrono::seconds(60));
    }

    [[nodiscard]] QStringList phaseTitles() const {
        QStringList titles;
        const auto* page = qobject_cast<FlashViewModel*>(
            m_app->currentTabViewModel()->pageViewModel(FlashViewModel::pageId));
        for (const QVariant& phase : page->phases()) {
            titles.append(phase.toMap().value(QStringLiteral("title")).toString());
        }
        return titles;
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    std::string m_scenario;
};

// ADR-0233 (plan 0033): every tab works with the application MCU; application firmware first.
TEST_F(FlashTest, KindDefaultsToApplicationFirmware) {
    start(probeScenario(), QStringLiteral("lte"));
    EXPECT_EQ(flash().kind(), Kind::AppFirmware);
    EXPECT_EQ(flash().targetId(), QStringLiteral("app"));
    EXPECT_EQ(source().kind(), Kind::AppFirmware);
    flash().setKind(Kind::LteFirmware);
    EXPECT_EQ(flash().targetId(), QStringLiteral("lte"));
    flash().setKind(Kind::AppFirmware);
    EXPECT_EQ(flash().targetId(), QStringLiteral("app"));
    // Re-choosing the selected kind changes nothing.
    flash().setKind(Kind::AppFirmware);
    EXPECT_EQ(flash().kind(), Kind::AppFirmware);
    // 0021 (ADR-0174): two kinds, the modem is part of LTE firmware.
    ASSERT_EQ(flash().kinds().size(), 2);
    EXPECT_TRUE(flash()
                    .kinds()
                    .at(0)
                    .toMap()
                    .value(QStringLiteral("mcu"))
                    .toString()
                    .contains(QStringLiteral("nRF52840")));
}

TEST_F(FlashTest, TheLastKindIsRememberedPerTabInMemoryOnly) {
    start(probeScenario());
    flash().setKind(Kind::LteFirmware);
    // Nothing is persisted (review F4, user decision 2026-10-16: no flash/lastKind key).
    EXPECT_FALSE(app().settingsStore()->value(QStringLiteral("flash/lastKind")).isValid());
    // The tab keeps its choice when it is configured again (no session).
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001,
                                QStringLiteral("Test Probe")));
    EXPECT_EQ(flash().kind(), Kind::LteFirmware);

    // Another tab starts from its own target, not from the first tab's choice.
    DeviceTabViewModel* first = &tab();
    app().newTab();
    ASSERT_NE(&tab(), first);
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001,
                                QStringLiteral("Test Probe")));
    EXPECT_EQ(flash().kind(), Kind::AppFirmware);
    EXPECT_EQ(flash().targetId(), QStringLiteral("app"));
}

TEST_F(FlashTest, LoadedHexIsReadyWithSummaryAndRecent) {
    start(probeScenario());
    EXPECT_EQ(flash().cannotFlashReason(),
              QStringLiteral("Choose a firmware file or enter a catalog ID"));
    ASSERT_TRUE(load(hexFile()));
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_TRUE(source().sourceSummary().startsWith(QStringLiteral("app.hex · Intel HEX · ")));
    EXPECT_TRUE(source().sourceSummary().contains(QStringLiteral("0x00001000–0x00002000")));
    EXPECT_FALSE(source().hasUicr());
    EXPECT_TRUE(flash().canFlash());
    EXPECT_EQ(flash().flashSummary(), QStringLiteral("Application firmware · app.hex"));
    ASSERT_EQ(source().recent()->count(), 1);
    // The recent list keeps the path of the file URL ('/' separators on Windows too).
    EXPECT_EQ(source().recent()->sourceAt(0),
              QDir::fromNativeSeparators(QString::fromStdString((root() / "app.hex").string())));
}

TEST_F(FlashTest, AKindChangeDoesNotAddToRecent) {
    start(probeScenario());
    ASSERT_TRUE(load(hexFile()));
    ASSERT_EQ(source().recent()->count(), 1);
    // The HEX fits LTE firmware too (as the own file, 0021), but only an explicit load adds it to
    // the LTE list (F7).
    flash().setKind(Kind::LteFirmware);
    source().setLteSource(FirmwareSourceViewModel::OwnFile);
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_EQ(source().recent()->count(), 0);
    EXPECT_EQ(source().recent()->entries().size(), 1);
    flash().setKind(Kind::AppFirmware);
    EXPECT_EQ(source().recent()->count(), 1);
}

TEST_F(FlashTest, AFileOfAnotherKindShowsAHintAndNeverSwitchesSilently) {
    start(probeScenario());
    // 0021 (ADR-0174): modem firmware is LTE firmware.
    ASSERT_TRUE(load(modemFile()));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Invalid);
    EXPECT_EQ(source().suggestedKind(), Kind::LteFirmware);
    EXPECT_EQ(source().kindHint(),
              QStringLiteral("This is LTE firmware — switch to LTE firmware?"));
    EXPECT_EQ(flash().kind(), Kind::AppFirmware);
    EXPECT_FALSE(flash().canFlash());
    source().acceptSuggestedKind();
    EXPECT_EQ(flash().kind(), Kind::LteFirmware);
    // A file loaded for another kind is the LTE card's own file once switched.
    source().setLteSource(FirmwareSourceViewModel::OwnFile);
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_TRUE(source().modemNoteVisible());

    // An LTE bundle under application firmware suggests LTE firmware.
    flash().setKind(Kind::AppFirmware);
    ASSERT_TRUE(load(bundleFile()));
    EXPECT_EQ(source().suggestedKind(), Kind::LteFirmware);
    EXPECT_EQ(source().kindHint(),
              QStringLiteral("This is LTE firmware — switch to LTE firmware?"));

    // A file that is not firmware at all is an error (no hint).
    ASSERT_TRUE(load(writeFile("notes.txt", "hello")));
    EXPECT_EQ(source().suggestedKind(), -1);
    EXPECT_TRUE(source().error().valid());
}

// 0021 (ADR-0174): every LTE input fits the LTE card; the summary follows the classification and
// the operation is chosen from it.
TEST_F(FlashTest, EveryLteInputFitsTheLteKind) {
    start(probeScenario());
    flash().setKind(Kind::LteFirmware);
    ASSERT_TRUE(load(hexFile("lte.hex")));
    EXPECT_EQ(source().lteSource(), FirmwareSourceViewModel::OwnFile); // a file is the own file
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_TRUE(source().sourceSummary().startsWith(QStringLiteral("lte.hex · LTE application")));
    EXPECT_FALSE(source().modemNoteVisible());
    EXPECT_TRUE(flash().haltAvailable());
    ASSERT_TRUE(load(bundleFile()));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_TRUE(source().sourceSummary().contains(QStringLiteral("LTE bundle")));
    EXPECT_TRUE(source().modemNoteVisible());
    ASSERT_TRUE(load(modemFile()));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_EQ(source().suggestedKind(), -1);
    EXPECT_TRUE(
        source().sourceSummary().contains(QStringLiteral("Modem firmware 2.0.2 (modem only)")))
        << source().sourceSummary().toStdString();
    EXPECT_TRUE(source().modemNoteVisible());
    EXPECT_FALSE(flash().haltAvailable());
    EXPECT_FALSE(source().catalogAvailable()); // no catalog ID on the LTE card
}

TEST_F(FlashTest, CatalogIdValidation) {
    start(probeScenario());
    EXPECT_TRUE(source().catalogAvailable());
    EXPECT_FALSE(source().checkCatalogId(QStringLiteral("3f2a")));
    EXPECT_EQ(source().catalogIdError(), QStringLiteral("The ID has 32 characters 0–9 a–f."));
    EXPECT_TRUE(source().checkCatalogId(QString()));
    EXPECT_TRUE(source().catalogIdError().isEmpty());
    source().loadCatalogId(QStringLiteral("zz000000000000000000000000000000"));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::None);
    EXPECT_FALSE(source().catalogIdError().isEmpty());
    flash().setKind(Kind::LteFirmware);
    EXPECT_FALSE(source().catalogAvailable());
}

TEST_F(FlashTest, CatalogDownloadAndErrors) {
#ifndef HWIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HWIO_TEST_OVERRIDES (HWIO_CATALOG_URL)";
#endif
    hwio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    qputenv("HWIO_CATALOG_URL", http.baseUrl().toUtf8());
    const QString id = randomId();
    http.set(QStringLiteral("/chester/%1/hex").arg(id),
             hwio::firmware::test::HttpFixture::Response::ok(
                 QByteArray::fromStdString(hwio::firmware::test::toHex(image()))));
    start(probeScenario());

    source().loadCatalogId(id.toUpper());
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Downloading);
    ASSERT_TRUE(
        waitFor([this] { return source().sourceState() != FirmwareSourceViewModel::Downloading; }));
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready)
        << source().error().code().toStdString();
    EXPECT_EQ(source().sourceName(), QStringLiteral("Catalog ID %1").arg(id));
    EXPECT_EQ(source().recent()->sourceAt(0), RecentSourcesModel::catalogPrefix + id);

    // Not in the catalog (404).
    source().loadCatalogId(randomId());
    ASSERT_TRUE(
        waitFor([this] { return source().sourceState() != FirmwareSourceViewModel::Downloading; }));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Invalid);
    EXPECT_EQ(source().error().code(), QStringLiteral("firmware.not_found_in_catalog"));
    EXPECT_EQ(source().error().primaryAction(), hwio::gui::error_action::ChooseFile);
}

TEST_F(FlashTest, RecentListIsBoundedPerKindAndRemovable) {
    start(probeScenario());
    RecentSourcesModel* recent = source().recent();
    for (int i = 0; i < 12; ++i) {
        recent->add(QStringLiteral("app"), QStringLiteral("/fw/app-%1.hex").arg(i));
    }
    recent->add(QStringLiteral("modem"), QStringLiteral("/fw/mfw.zip"));
    EXPECT_EQ(recent->entries().size(), RecentSourcesModel::maxEntries);
    EXPECT_EQ(recent->count(), 9); // the modem entry is the 10th
    EXPECT_EQ(recent->sourceAt(0), QStringLiteral("/fw/app-11.hex"));
    // Re-adding moves to the front instead of duplicating.
    recent->add(QStringLiteral("app"), QStringLiteral("/fw/app-5.hex"));
    EXPECT_EQ(recent->sourceAt(0), QStringLiteral("/fw/app-5.hex"));
    EXPECT_EQ(recent->entries().size(), RecentSourcesModel::maxEntries);
    source().removeRecent(0);
    EXPECT_NE(recent->sourceAt(0), QStringLiteral("/fw/app-5.hex"));
    // 0021 (ADR-0174): stored "modem" entries are shown on the LTE card.
    flash().setKind(Kind::LteFirmware);
    EXPECT_EQ(recent->count(), 1);
    EXPECT_EQ(recent->data(recent->index(0), RecentSourcesModel::TypeRole).toString(),
              QStringLiteral("Zip archive"));
}

TEST_F(FlashTest, FlashFromADisconnectedTab) {
    start(probeScenario());
    ASSERT_EQ(tab().session(), nullptr);
    ASSERT_TRUE(load(hexFile()));
    flash().flash();
    // ensureSession() opened the console target; the page is busy while the operation runs.
    ASSERT_NE(tab().session(), nullptr);
    EXPECT_EQ(flash().opState(), FlashViewModel::Running);
    EXPECT_EQ(tab().busyPage(), FlashViewModel::pageId);
    EXPECT_EQ(tab().busyText(), QStringLiteral("Flashing…"));
    EXPECT_EQ(phaseTitles(),
              (QStringList{QStringLiteral("Connecting"), QStringLiteral("Erasing"),
                           QStringLiteral("Programming"), QStringLiteral("Verifying"),
                           QStringLiteral("Resetting")}));
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    EXPECT_TRUE(
        flash().resultText().startsWith(QStringLiteral("Firmware flashed and verified in ")));
    EXPECT_EQ(flash().chipText(), QStringLiteral("Chip: nRF52840"));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_TRUE(flash().flashOperation());
    flash().dismissResult();
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    EXPECT_TRUE(flash().canFlash()); // the source stays loaded
}

// Plan 0021 phase 2 (ADR-0166): "In the device" from `info show` after the attach, "Now in the
// device" after a successful application flash, cleared on disconnect.
TEST_F(FlashTest, TheFirmwareInTheDeviceBeforeAndAfterAFlash) {
    start(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
        "product": "Test Probe",
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}],
                "responses": {"info show": "fw-name: hio-chester-push-fm\nfw-version: v4.0.1\n"}},
        "chip": {}}]})");
    EXPECT_TRUE(flash().deviceFirmwareText().isEmpty());
    tab().connectDevice();
    ASSERT_TRUE(waitFor([&] { return !flash().deviceFirmwareText().isEmpty(); }));
    EXPECT_EQ(flash().deviceFirmwareText(),
              QStringLiteral("In the device: hio-chester-push-fm v4.0.1"));
    ASSERT_TRUE(load(hexFile()));
    flash().flash();
    ASSERT_TRUE(finished());
    ASSERT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    ASSERT_TRUE(
        waitFor([&] { return !flash().resultDeviceText().isEmpty(); }, std::chrono::seconds(10)));
    EXPECT_EQ(flash().resultDeviceText(),
              QStringLiteral("Now in the device: hio-chester-push-fm v4.0.1"));
    flash().dismissResult();
    EXPECT_TRUE(flash().resultDeviceText().isEmpty());
    tab().disconnectDevice();
    EXPECT_TRUE(flash().deviceFirmwareText().isEmpty());
}

TEST_F(FlashTest, CancelDuringVerify) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    ASSERT_TRUE(load(hexFile("big.hex", image(0x1000, std::size_t{64} * 1024))));
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    ASSERT_TRUE(waitFor([this] { return flash().progressText().contains(QStringLiteral("%")); },
                        std::chrono::seconds(30)));
    flash().cancel();
    EXPECT_EQ(flash().opState(), FlashViewModel::Cancelling);
    EXPECT_FALSE(flash().cancelAvailable());
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Cancelled);
    EXPECT_EQ(flash().resultText(),
              QStringLiteral(
                  "Flashing was cancelled. The device may not start until you flash it again."));
    EXPECT_TRUE(tab().busyPage().isEmpty());
}

// Plan 0033 (ADR-0234): LTE firmware is not flashed from the page; [Update LTE firmware…] opens
// the guided update and the Flash bar says why it is disabled.
TEST_F(FlashTest, TheLteKindOpensTheLteUpdateWizard) {
    start(probeScenario());
    flash().setKind(Kind::LteFirmware);
    ASSERT_TRUE(load(hexFile("lte.hex")));
    EXPECT_FALSE(flash().canFlash());
    EXPECT_EQ(flash().cannotFlashReason(),
              QStringLiteral("Use Update LTE firmware… above: the J-Link has to be moved between "
                             "the connectors."));
    flash().flash(); // refused
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    EXPECT_EQ(tab().session(), nullptr);
    QSignalSpy requested(&flash(), &FlashViewModel::lteUpdateRequested);
    flash().requestLteUpdate();
    EXPECT_EQ(requested.count(), 1);
    ASSERT_TRUE(app().wizard()->isOpen());
    EXPECT_EQ(app().wizard()->wizardId(), QStringLiteral("lte-update"));
    app().wizard()->cancel(false);
    EXPECT_FALSE(app().wizard()->isOpen());
}

TEST_F(FlashTest, MissingNrfUtilIsAPrerequisite) {
    start(probeScenario());
    flash().setKind(Kind::LteFirmware);
    // 0021: the designated LTE firmware (the default source) always contains modem firmware.
    ASSERT_EQ(source().lteSource(), FirmwareSourceViewModel::Designated);
    ASSERT_TRUE(waitFor([this] { return !flash().prerequisiteText().isEmpty(); }));
    EXPECT_EQ(flash().prerequisiteText(),
              QStringLiteral("nRF Util was not found. It is needed for LTE firmware."));
    ASSERT_TRUE(load(modemFile()));
    EXPECT_FALSE(flash().canFlash());
    // Application firmware does not need it.
    flash().setKind(Kind::AppFirmware);
    EXPECT_TRUE(flash().prerequisiteText().isEmpty());
}

TEST_F(FlashTest, AnotherBusyPageDisablesFlash) {
    start(probeScenario());
    ASSERT_TRUE(load(hexFile()));
    ASSERT_TRUE(flash().canFlash());
    auto* testPage = qobject_cast<hwio::gui::test::TestPageViewModel*>(
        tab().pageViewModel(hwio::gui::test::TestPageViewModel::pageId));
    ASSERT_NE(testPage, nullptr);
    testPage->startWork();
    EXPECT_FALSE(flash().canFlash());
    EXPECT_EQ(flash().cannotFlashReason(), QStringLiteral("Another operation is running"));
    EXPECT_FALSE(flash().canRunMaintenance());
    flash().flash(); // ignored
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    testPage->finishWork();
    EXPECT_TRUE(flash().canFlash());
}

TEST_F(FlashTest, UicrRecordsNeedTheOption) {
    start(probeScenario());
    std::vector<Segment> segments;
    segments.push_back(
        Segment{.address = 0x1000, .data = std::vector<std::byte>(16, std::byte{1})});
    segments.push_back(
        Segment{.address = 0x10001200, .data = std::vector<std::byte>(4, std::byte{2})});
    ASSERT_TRUE(load(hexFile("uicr.hex", *FirmwareImage::fromSegments(std::move(segments)))));
    EXPECT_TRUE(source().hasUicr());
    EXPECT_FALSE(flash().canFlash());
    EXPECT_EQ(flash().cannotFlashReason(),
              QStringLiteral("The firmware writes the UICR: allow it in Options"));
    source().setAllowUicr(true);
    EXPECT_TRUE(flash().canFlash());
    // Never carried over to another image (ADR-0036).
    ASSERT_TRUE(load(hexFile()));
    EXPECT_FALSE(source().allowUicr());
}

TEST_F(FlashTest, ConfirmationTexts) {
    start(probeScenario());
    enableExpertActions();
    QVariantMap app = flash().describeEraseAll();
    EXPECT_EQ(app.value(QStringLiteral("title")).toString(),
              QStringLiteral("Erase everything on the application MCU?"));
    EXPECT_EQ(app.value(QStringLiteral("acceptText")).toString(),
              QStringLiteral("Erase everything"));
    EXPECT_EQ(app.value(QStringLiteral("consequence")).toString(),
              QStringLiteral("This cannot be undone."));
    QStringList items = app.value(QStringLiteral("items")).toStringList();
    ASSERT_EQ(items.size(), 3);
    EXPECT_TRUE(items.at(2).startsWith(QStringLiteral("Product Information Block")));
    const QVariantMap recoverApp = flash().describeRecover();
    EXPECT_EQ(recoverApp.value(QStringLiteral("title")).toString(),
              QStringLiteral("Recover the application MCU?"));

    EXPECT_TRUE(recoverApp.value(QStringLiteral("items"))
                    .toStringList()
                    .contains(QStringLiteral(
                        "Product Information Block (serial number, hardware version, …)")));

    flash().setMaintenanceTarget(QStringLiteral("lte"));
    EXPECT_EQ(flash().maintenanceTitle(), QStringLiteral("Maintenance — LTE MCU"));
    const QVariantMap lte = flash().describeEraseAll();
    EXPECT_EQ(lte.value(QStringLiteral("title")).toString(),
              QStringLiteral("Erase everything on the LTE MCU?"));
    // ERASEALL and recover erase the PIB on the nRF91 too (review F1, ADR-0038).
    const QStringList lteItems = lte.value(QStringLiteral("items")).toStringList();
    ASSERT_EQ(lteItems.size(), 3);
    EXPECT_TRUE(lteItems.at(2).startsWith(QStringLiteral("Product Information Block")));
    const QVariantMap recoverLte = flash().describeRecover();
    EXPECT_TRUE(recoverLte.value(QStringLiteral("body"))
                    .toString()
                    .contains(QStringLiteral("small image")));
    const QStringList recoverLteItems = recoverLte.value(QStringLiteral("items")).toStringList();
    ASSERT_EQ(recoverLteItems.size(), 3);
    EXPECT_TRUE(recoverLteItems.at(2).startsWith(QStringLiteral("Product Information Block")));
}

TEST_F(FlashTest, TheLteMcuHasNoCodeOnlyErase) {
    start(probeScenario());
    enableExpertActions();
    EXPECT_TRUE(flash().eraseCodeAvailable()); // application MCU: defaultErase Code
    flash().setMaintenanceTarget(QStringLiteral("lte"));
    EXPECT_FALSE(flash().eraseCodeAvailable()); // LTE MCU: defaultErase All (review F2)
    flash().eraseCode();                        // refused
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    EXPECT_TRUE(tab().busyPage().isEmpty());
}

TEST_F(FlashTest, AConfirmationIsRefusedWhenTheMcuChanged) {
    start(probeScenario());
    enableExpertActions();
    flash().requestConfirmation(false);
    EXPECT_EQ(flash().confirmation().value(QStringLiteral("title")).toString(),
              QStringLiteral("Erase everything on the application MCU?"));
    // The dialog named the application MCU; the maintenance MCU changed meanwhile (F11).
    flash().setMaintenanceTarget(QStringLiteral("lte"));
    flash().confirm();
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_EQ(tab().session(), nullptr);
    // Not kept for later either.
    flash().setMaintenanceTarget(QStringLiteral("app"));
    flash().confirm();
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
}

TEST_F(FlashTest, MaintenanceActionsRunAsOperations) {
    start(probeScenario());
    flash().reset(true);
    EXPECT_EQ(tab().busyText(), QStringLiteral("Resetting…"));
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    EXPECT_EQ(flash().resultText(), QStringLiteral("The application MCU was reset and halted."));
    EXPECT_FALSE(flash().flashOperation());
    flash().dismissResult();

    enableExpertActions(); // ADR-0236: Erase everything and Recover
    flash().requestConfirmation(false);
    EXPECT_EQ(flash().confirmation().value(QStringLiteral("acceptText")).toString(),
              QStringLiteral("Erase everything"));
    flash().confirm();
    EXPECT_EQ(tab().busyText(), QStringLiteral("Erasing…"));
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded);
    EXPECT_EQ(flash().resultText(),
              QStringLiteral("Everything on the application MCU was erased."));
    flash().confirm(); // nothing pending
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded);
    flash().dismissResult();

    flash().eraseCode();
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded);
    flash().dismissResult();

    flash().requestConfirmation(true);
    flash().confirm();
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    EXPECT_EQ(flash().resultText(), QStringLiteral("The application MCU was recovered."));
}

TEST_F(FlashTest, AFailedDownloadIsAnInterruptedFlash) {
    start(probeScenario(R"({"writeFault": true})"));
    ASSERT_TRUE(load(hexFile()));
    flash().flash();
    ASSERT_TRUE(finished());
    ASSERT_EQ(flash().opState(), FlashViewModel::Failed);
    EXPECT_EQ(flash().error().code(), QStringLiteral("target.program_failed"));
    EXPECT_EQ(flash().resultText(),
              QStringLiteral(
                  "Flashing was interrupted. The firmware may be partly written: flash it again."));
    // [Try again] runs the same flash again.
    flash().triggerErrorAction(hwio::gui::error_action::Retry);
    EXPECT_EQ(flash().opState(), FlashViewModel::Running);
    ASSERT_TRUE(finished());
}

TEST_F(FlashTest, RetryFlashesTheStoredRequestAfterASourceChange) {
    start(probeScenario(R"({"writeFault": true})"));
    ASSERT_TRUE(load(hexFile("first.hex")));
    flash().flash();
    ASSERT_TRUE(finished());
    ASSERT_EQ(flash().opState(), FlashViewModel::Failed);
    // Another file arrives while the result is shown (F6: the page no longer accepts drops there;
    // the view model must not depend on it).
    ASSERT_TRUE(load(hexFile("second.hex", image(0x2000))));
    ASSERT_EQ(source().sourceName(), QStringLiteral("second.hex"));
    flash().triggerErrorAction(hwio::gui::error_action::Retry);
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    EXPECT_EQ(flash().operationTitle(),
              QStringLiteral("Flashing first.hex to the application MCU"));
    ASSERT_TRUE(finished());
}

TEST_F(FlashTest, ALostSessionEndsTheOperationAsFailed) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    ASSERT_TRUE(load(hexFile("big.hex", image(0x1000, std::size_t{64} * 1024))));
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    ASSERT_TRUE(waitFor([this] { return flash().progressText().contains(QStringLiteral("%")); },
                        std::chrono::seconds(30)));
    // The tab's session goes away under the running operation (shutdown).
    tab().shutdown();
    EXPECT_EQ(tab().session(), nullptr);
    EXPECT_EQ(flash().opState(), FlashViewModel::Failed);
    EXPECT_EQ(flash().error().code(), QStringLiteral("target.not_connected"));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_FALSE(flash().cancelAvailable());
}

TEST_F(FlashTest, ClosingTheTabMidOperationCancelsAndCloses) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    ASSERT_TRUE(load(hexFile("big.hex", image(0x1000, std::size_t{64} * 1024))));
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    ASSERT_TRUE(waitFor([this] { return flash().progressText().contains(QStringLiteral("%")); },
                        std::chrono::seconds(30)));
    const QPointer<DeviceTabViewModel> closing(&tab());
    const QPointer<FlashViewModel> page(&flash());
    // [Cancel and close] of the busy confirmation: cancels, then closes on closeReady.
    app().closeTabObject(closing.data(), true);
    EXPECT_EQ(page->opState(), FlashViewModel::Cancelling);
    ASSERT_TRUE(waitFor([&closing] { return closing.isNull(); }, std::chrono::seconds(30)));
    EXPECT_TRUE(page.isNull()); // deleted with its tab
}

TEST_F(FlashTest, PhaseTitlesAreTranslatedTexts) {
    EXPECT_EQ(FlashViewModel::phaseTitle(hwio::firmware::ProgramPhase::Modem),
              QStringLiteral("Updating the modem"));
    EXPECT_EQ(FlashViewModel::phaseTitle(hwio::firmware::ProgramPhase::SwitchingTarget),
              QStringLiteral("Switching MCU"));
}

// ---- FirmwareLoader ---------------------------------------------------------------------------

class LoaderTest : public hwio::gui::test::GuiTest {};

TEST_F(LoaderTest, NewestRequestOfARequesterWins) {
    hwio::gui::FirmwareLoader loader(root() / "instances");
    const fs::path missing = root() / "missing.hex";
    std::vector<std::uint64_t> delivered;
    QObject::connect(&loader, &hwio::gui::FirmwareLoader::fileLoaded, &loader,
                     [&](quint64 generation, const hwio::Result<hwio::gui::LoadedSource>&) {
                         delivered.push_back(generation);
                     });
    const int requester = 0;
    std::uint64_t last = 0;
    for (int i = 0; i < 8; ++i) {
        last = loader.loadFile(&requester, missing);
    }
    ASSERT_TRUE(waitFor([&] { return !delivered.empty() && delivered.back() == last; }));
    // At most the running request and the newest pending one deliver.
    EXPECT_LE(delivered.size(), 2U);
}

TEST_F(LoaderTest, ARequestDroppedFromTheFullQueueIsAnswered) {
    // The worker is kept busy by an nRF Util version probe that answers after 30 s.
    const fs::path log = root() / "nrfutil.log";
    qputenv("HWIO_FAKE_NRFUTIL_MODE", "slowversion");
    qputenv("HWIO_FAKE_NRFUTIL_LOG", QByteArray::fromStdString(log.string()));
    auto loader = std::make_unique<hwio::gui::FirmwareLoader>(root() / "instances");
    std::vector<std::pair<std::uint64_t, ErrorCode>> answers;
    QObject::connect(loader.get(), &hwio::gui::FirmwareLoader::fileLoaded, loader.get(),
                     [&](quint64 generation, const hwio::Result<hwio::gui::LoadedSource>& result) {
                         answers.emplace_back(generation, result ? ErrorCode::InternalUnexpected
                                                                 : result.error().code);
                     });
    hwio::nrfutil::LocatorOptions options;
    options.option = fs::path(HWIO_FAKE_NRFUTIL);
    const int prober = 0;
    (void)loader->checkNrfUtil(&prober, options);
    ASSERT_TRUE(waitFor([&log] { return fs::exists(log); })); // the probe runs

    // Five requesters: the queue holds four, the oldest is dropped and answered (F8).
    const std::vector<int> requesters(5, 0);
    std::vector<std::uint64_t> generations;
    generations.reserve(requesters.size());
    for (const int& requester : requesters) {
        generations.push_back(loader->loadFile(&requester, root() / "missing.hex"));
    }
    ASSERT_TRUE(waitFor([&answers] { return !answers.empty(); }));
    ASSERT_EQ(answers.size(), 1U);
    EXPECT_EQ(answers.front().first, generations.front());
    EXPECT_EQ(answers.front().second, ErrorCode::InternalQueueFull);

    // Quitting does not wait for the probe: it is killed (F9).
    const auto before = std::chrono::steady_clock::now();
    loader.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds(5));
    qunsetenv("HWIO_FAKE_NRFUTIL_MODE");
    qunsetenv("HWIO_FAKE_NRFUTIL_LOG");
}

// ---- 0021 (ADR-0174 ... ADR-0167) ---------------------------------------------------------------

// The designated LTE firmware: downloaded when its row is shown, verified, then flashed as an LTE
// bundle; a damaged download says so and [Try again] downloads again. The fixture serves it
// through the test-only HWIO_DESIGNATED_URL / HWIO_DESIGNATED_SHA256.
TEST_F(FlashTest, DesignatedLteFirmware) {
#ifndef HWIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HWIO_TEST_OVERRIDES (HWIO_DESIGNATED_URL)";
#endif
    hwio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    // Unique content: the firmware cache of the test data directory never has it yet.
    const QByteArray modem = hwio::firmware::test::zipBytes(
        {{.name = "firmware.update.image.segments.0.hex", .data = ":00000001FF\n"},
         {.name = "x" + randomId().toStdString() + ".txt", .data = "x"}});
    const QByteArray bundle = hwio::firmware::test::zipBytes(
        {{.name = "modem.zip", .data = modem.toStdString()},
         {.name = "application.hex", .data = hwio::firmware::test::toHex(image())}});
    const QString sha =
        QString::fromLatin1(QCryptographicHash::hash(bundle, QCryptographicHash::Sha256).toHex());
    http.set(QStringLiteral("/chester/lte.zip"),
             hwio::firmware::test::HttpFixture::Response::ok(bundle));
    qputenv("HWIO_DESIGNATED_URL", (http.baseUrl() + QStringLiteral("/lte.zip")).toUtf8());
    qputenv("HWIO_DESIGNATED_SHA256", sha.toUtf8());
    qputenv("HWIO_NRFUTIL", QByteArray(HWIO_FAKE_NRFUTIL));
    start(probeScenario(R"({"family": "nrf91"})"));
    flash().setKind(Kind::LteFirmware);
    EXPECT_TRUE(source().designatedAvailable());
    EXPECT_TRUE(source().designatedActive());
    EXPECT_EQ(source().designatedTitle(),
              QStringLiteral("HARDWARIO LTE firmware 1.7.0 (recommended)"));
    // Nothing is downloaded before the row is shown.
    EXPECT_EQ(source().designatedState(), FirmwareSourceViewModel::DesignatedIdle);
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::None);
    EXPECT_TRUE(source().modemNoteVisible());
    source().prepareDesignated();
    EXPECT_EQ(source().designatedState(), FirmwareSourceViewModel::DesignatedDownloading);
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Downloading);
    ASSERT_TRUE(waitFor([this] {
        return source().designatedState() != FirmwareSourceViewModel::DesignatedDownloading;
    }));
    ASSERT_EQ(source().designatedState(), FirmwareSourceViewModel::DesignatedReady)
        << source().designatedError().detail().toStdString();
    EXPECT_EQ(source().designatedStatus(), QStringLiteral("Ready"));
    EXPECT_EQ(source().sourceName(), QStringLiteral("HARDWARIO LTE firmware 1.7.0"));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    // Plan 0033: flashed by the LTE update wizard (LteUpdateWizard tests), not by this page.

    // "Use your own file…": the designated firmware stays downloaded.
    source().setLteSource(FirmwareSourceViewModel::OwnFile);
    EXPECT_FALSE(source().designatedActive());
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::None);
    source().setLteSource(FirmwareSourceViewModel::Designated);
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);

    // Another tab with a damaged file (another pinned hash): checksum mismatch, Try again.
    qputenv("HWIO_DESIGNATED_SHA256", QByteArray(64, 'a'));
    app().newTab();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("lte"), 801000001,
                                QStringLiteral("Test Probe")));
    flash().setKind(Kind::LteFirmware);
    source().prepareDesignated();
    ASSERT_TRUE(waitFor([this] {
        return source().designatedState() == FirmwareSourceViewModel::DesignatedFailed;
    }));
    EXPECT_EQ(source().designatedError().code(), QStringLiteral("firmware.checksum_mismatch"));
    EXPECT_EQ(source().designatedError().message(),
              QStringLiteral("The downloaded file is damaged. Try again."));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Invalid);
    source().prepareDesignated(); // not after a failure: that needs Try again
    EXPECT_EQ(source().designatedState(), FirmwareSourceViewModel::DesignatedFailed);
    qputenv("HWIO_DESIGNATED_SHA256", sha.toUtf8());
    source().retryDesignated();
    ASSERT_TRUE(waitFor([this] {
        return source().designatedState() != FirmwareSourceViewModel::DesignatedDownloading;
    }));
    EXPECT_EQ(source().designatedState(), FirmwareSourceViewModel::DesignatedReady);
    EXPECT_FALSE(source().designatedError().valid());

    // A failed download says that an own file works too.
    qputenv("HWIO_DESIGNATED_URL", (http.baseUrl() + QStringLiteral("/missing.zip")).toUtf8());
    qputenv("HWIO_DESIGNATED_SHA256", QByteArray(64, 'b'));
    app().newTab();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("lte"), 801000001,
                                QStringLiteral("Test Probe")));
    flash().setKind(Kind::LteFirmware);
    source().prepareDesignated();
    ASSERT_TRUE(waitFor([this] {
        return source().designatedState() == FirmwareSourceViewModel::DesignatedFailed;
    }));
    EXPECT_EQ(source().designatedError().code(), QStringLiteral("network.download_failed"));
    EXPECT_TRUE(source().designatedError().suggestion().endsWith(
        QStringLiteral("You can also choose your own file.")));
    qunsetenv("HWIO_DESIGNATED_URL");
    qunsetenv("HWIO_DESIGNATED_SHA256");
}

// The catalog page of a valid ID (ADR-0166): name, version and upload date; "Unknown firmware"
// when the page cannot be read; its SHA-256 verifies the download.
TEST_F(FlashTest, CatalogInfoOfAnId) {
#ifndef HWIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HWIO_TEST_OVERRIDES (HWIO_CATALOG_URL)";
#endif
    hwio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    qputenv("HWIO_CATALOG_URL", http.baseUrl().toUtf8());
    const QString id = randomId();
    const QByteArray hex = QByteArray::fromStdString(hwio::firmware::test::toHex(image()));
    const auto page = [](const QByteArray& sha) {
        return QByteArrayLiteral(
                   "<h2>hio-chester-push-fm:v4.0.1</h2><div>download the HEX file:</div>"
                   "<div>SHA256 checksum: ") +
               sha +
               QByteArrayLiteral("</div><div>Upload date/time</div><div>2026-09-07 12:13:53</div>");
    };
    http.set(QStringLiteral("/chester/%1").arg(id),
             hwio::firmware::test::HttpFixture::Response::ok(
                 page(QCryptographicHash::hash(hex, QCryptographicHash::Sha256).toHex())));
    http.set(QStringLiteral("/chester/%1/hex").arg(id),
             hwio::firmware::test::HttpFixture::Response::ok(hex));
    start(probeScenario());
    EXPECT_EQ(source().catalogInfo().value(QStringLiteral("lookupState")).toString(),
              QStringLiteral("none"));
    EXPECT_TRUE(source().checkCatalogId(id));
    EXPECT_EQ(source().catalogInfo().value(QStringLiteral("lookupState")).toString(),
              QStringLiteral("loading"));
    EXPECT_EQ(source().catalogInfo().value(QStringLiteral("text")).toString(),
              QStringLiteral("Looking up the firmware…"));
    ASSERT_TRUE(waitFor([this] {
        return source().catalogInfo().value(QStringLiteral("lookupState")).toString() !=
               QStringLiteral("loading");
    }));
    const QVariantMap info = source().catalogInfo();
    EXPECT_EQ(info.value(QStringLiteral("lookupState")).toString(), QStringLiteral("found"));
    EXPECT_EQ(info.value(QStringLiteral("name")).toString(), QStringLiteral("hio-chester-push-fm"));
    EXPECT_EQ(info.value(QStringLiteral("version")).toString(), QStringLiteral("v4.0.1"));
    EXPECT_EQ(info.value(QStringLiteral("uploaded")).toString(), QStringLiteral("2026-09-07"));
    EXPECT_EQ(info.value(QStringLiteral("text")).toString(),
              QStringLiteral("hio-chester-push-fm v4.0.1 · uploaded 2026-09-07"));
    // The download is verified and named after the page.
    source().loadCatalogId(id);
    ASSERT_TRUE(
        waitFor([this] { return source().sourceState() != FirmwareSourceViewModel::Downloading; }));
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready)
        << source().error().code().toStdString();
    EXPECT_EQ(source().sourceName(), QStringLiteral("hio-chester-push-fm v4.0.1"));
    EXPECT_EQ(flash().flashSummary(),
              QStringLiteral("Application firmware · hio-chester-push-fm v4.0.1"));

    // A page that lists another hash: the download is refused as damaged.
    const QString other = randomId();
    http.set(QStringLiteral("/chester/%1").arg(other),
             hwio::firmware::test::HttpFixture::Response::ok(page(QByteArray(64, 'c'))));
    http.set(QStringLiteral("/chester/%1/hex").arg(other),
             hwio::firmware::test::HttpFixture::Response::ok(hex));
    EXPECT_TRUE(source().checkCatalogId(other));
    ASSERT_TRUE(waitFor([this] {
        return source().catalogInfo().value(QStringLiteral("lookupState")).toString() ==
               QStringLiteral("found");
    }));
    source().loadCatalogId(other);
    ASSERT_TRUE(
        waitFor([this] { return source().sourceState() != FirmwareSourceViewModel::Downloading; }));
    EXPECT_EQ(source().error().code(), QStringLiteral("firmware.checksum_mismatch"));

    // No page: unknown firmware (not an error); an invalid ID clears the info.
    EXPECT_TRUE(source().checkCatalogId(randomId()));
    ASSERT_TRUE(waitFor([this] {
        return source().catalogInfo().value(QStringLiteral("lookupState")).toString() !=
               QStringLiteral("loading");
    }));
    EXPECT_EQ(source().catalogInfo().value(QStringLiteral("text")).toString(),
              QStringLiteral("Unknown firmware — it can still be flashed."));
    EXPECT_FALSE(source().checkCatalogId(QStringLiteral("12")));
    EXPECT_EQ(source().catalogInfo().value(QStringLiteral("lookupState")).toString(),
              QStringLiteral("none"));
}

// Review F1: a download started while the page lookup still runs waits for it, so the page's hash
// verifies it (the Recent list, a paste + Enter); a cached HEX is checked against the page too.
TEST_F(FlashTest, ACatalogDownloadWaitsForTheLookup) {
#ifndef HWIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HWIO_TEST_OVERRIDES (HWIO_CATALOG_URL)";
#endif
    hwio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    qputenv("HWIO_CATALOG_URL", http.baseUrl().toUtf8());
    const QString id = randomId();
    const QByteArray hex = QByteArray::fromStdString(hwio::firmware::test::toHex(image()));
    const auto page = [](const QByteArray& sha) {
        return QByteArrayLiteral("<h2>hio-chester-push-fm:v4.0.1</h2><div>the HEX file:</div>"
                                 "<div>SHA256 checksum: ") +
               sha + QByteArrayLiteral("</div>");
    };
    http.set(QStringLiteral("/chester/%1/hex").arg(id),
             hwio::firmware::test::HttpFixture::Response::ok(hex));
    start(probeScenario());

    // No page yet (unknown firmware): the download waits for the lookup, then runs unverified.
    source().loadCatalogId(id);
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Downloading);
    EXPECT_EQ(source().catalogInfo().value(QStringLiteral("lookupState")).toString(),
              QStringLiteral("loading"));
    ASSERT_TRUE(
        waitFor([this] { return source().sourceState() != FirmwareSourceViewModel::Downloading; }));
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready)
        << source().error().code().toStdString();
    EXPECT_EQ(http.requests(QStringLiteral("/chester/%1").arg(id)), 1);

    // The page now lists another file: loading the ID again (from Recent: the info is read
    // anew) waits for the page and refuses the cached HEX as well as the download.
    http.set(QStringLiteral("/chester/%1").arg(id),
             hwio::firmware::test::HttpFixture::Response::ok(page(QByteArray(64, 'd'))));
    EXPECT_TRUE(source().checkCatalogId(QString())); // clears the info of the field
    ASSERT_EQ(source().recent()->sourceAt(0), RecentSourcesModel::catalogPrefix + id);
    source().loadRecent(0);
    EXPECT_EQ(source().catalogInfo().value(QStringLiteral("lookupState")).toString(),
              QStringLiteral("loading"));
    ASSERT_TRUE(
        waitFor([this] { return source().sourceState() != FirmwareSourceViewModel::Downloading; }));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Invalid);
    EXPECT_EQ(source().error().code(), QStringLiteral("firmware.checksum_mismatch"));

    // Typing another ID while a download waits for its lookup drops that download.
    const QString other = randomId();
    source().loadCatalogId(other);
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Downloading);
    EXPECT_TRUE(source().checkCatalogId(randomId()));
    EXPECT_NE(source().sourceState(), FirmwareSourceViewModel::Downloading);
    EXPECT_EQ(http.requests(QStringLiteral("/chester/%1/hex").arg(other)), 0);
}

// ADR-0167: the documentation page of application firmware in the UI language (English here: no
// LanguageManager in the view model tests).
TEST_F(FlashTest, DocsUrlFollowsTheLanguage) {
    start(probeScenario());
    EXPECT_EQ(source().docsUrl().toString(),
              QStringLiteral("https://docs.hardwario.com/chester/catalog-applications/"
                             "catalog-applications/#application-firmware"));
}

TEST_F(LoaderTest, LoadErrorsAreHWioErrors) {
    const auto result = hwio::gui::loadFirmwareFile(root() / "missing.hex");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::IoFileNotFound);
}

// ---- plan 0033: expert maintenance actions (ADR-0236), Expert mode since plan 0045 (ADR-0360)
// --------------------------------------------

TEST_F(FlashTest, ExpertActionsFollowTheMode) {
    start(probeScenario());
    EXPECT_FALSE(flash().expertActions());
    EXPECT_FALSE(flash().eraseAllAvailable());
    EXPECT_FALSE(flash().recoverAvailable());
    EXPECT_TRUE(flash().maintenanceTargets().isEmpty());
    EXPECT_TRUE(flash().eraseCodeAvailable());
    EXPECT_EQ(flash().maintenanceTitle(), QStringLiteral("Maintenance — application MCU"));
    flash().setMaintenanceTarget(QStringLiteral("lte")); // ignored
    EXPECT_EQ(flash().maintenanceTarget(), QStringLiteral("app"));
    flash().requestConfirmation(false); // refused
    EXPECT_TRUE(flash().confirmation().isEmpty());
    flash().eraseAll();
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);

    QSignalSpy changed(&flash(), &FlashViewModel::maintenanceChanged);
    enableExpertActions();
    EXPECT_GE(changed.count(), 1);
    EXPECT_TRUE(flash().eraseAllAvailable());
    EXPECT_TRUE(flash().recoverAvailable());
    ASSERT_EQ(flash().maintenanceTargets().size(), 2);
    EXPECT_EQ(flash().maintenanceTargets().at(1).toMap().value(QStringLiteral("title")).toString(),
              QStringLiteral("LTE MCU"));
    flash().setMaintenanceTarget(QStringLiteral("lte"));
    EXPECT_EQ(flash().maintenanceTitle(), QStringLiteral("Maintenance — LTE MCU"));
    // Advanced again: back to the application MCU.
    ASSERT_TRUE(
        app().uiMode()->choose(hwio::gui::UiMode::Advanced)); // the Settings segmented control
    EXPECT_EQ(flash().maintenanceTarget(), QStringLiteral("app"));
    EXPECT_FALSE(flash().recoverAvailable());
    EXPECT_FALSE(flash().expertActions());
    // Basic hides them too.
    ASSERT_TRUE(app().uiMode()->choose(hwio::gui::UiMode::Basic));
    EXPECT_FALSE(flash().eraseAllAvailable());
}

// The target.protected hand-off (plan 0033 E): the Flash page with Maintenance unfolded and
// [Recover…] for that MCU, once, in every mode (ADR-0360); the mode is not changed.
TEST_F(FlashTest, TheProtectedHandOffShowsRecoverOnce) {
    start(probeScenario());
    EXPECT_FALSE(flash().maintenanceExpanded());
    emit app().wizard() -> handOff(QStringLiteral("open-flash-page:recover:app"), &tab());
    EXPECT_EQ(tab().currentPage(), FlashViewModel::pageId);
    EXPECT_TRUE(flash().maintenanceExpanded());
    EXPECT_TRUE(flash().recoverAvailable());
    EXPECT_FALSE(flash().eraseAllAvailable());
    EXPECT_FALSE(flash().expertActions());
    flash().requestConfirmation(true);
    EXPECT_EQ(flash().confirmation().value(QStringLiteral("title")).toString(),
              QStringLiteral("Recover the application MCU?"));
    flash().confirm();
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    EXPECT_FALSE(flash().recoverAvailable()); // this once
}

// Plan 0045 (ADR-0360): the protected-MCU hand-off works in Basic mode too.
TEST_F(FlashTest, TheProtectedHandOffWorksInBasicMode) {
    start(probeScenario());
    ASSERT_TRUE(app().uiMode()->choose(hwio::gui::UiMode::Basic));
    emit app().wizard() -> handOff(QStringLiteral("open-flash-page:recover:app"), &tab());
    EXPECT_TRUE(flash().recoverAvailable());
    EXPECT_FALSE(flash().eraseAllAvailable());
    EXPECT_EQ(app().uiMode()->mode(), hwio::gui::UiMode::Basic);
}

// ---- plan 0033: the LTE firmware update wizard (ADR-0234)
// ----------------------------------------

TEST_F(FlashTest, LteUpdateWizardRunsABundleThroughEveryStep) {
    qputenv("HWIO_NRFUTIL", QByteArray(HWIO_FAKE_NRFUTIL));
    startBoard();
    hwio::gui::LteUpdateWizard* wizard = openLteWizard();
    ASSERT_NE(wizard, nullptr);
    EXPECT_EQ(wizard->stage(), hwio::gui::LteUpdateWizard::Firmware);
    EXPECT_EQ(wizard->plannedSteps().size(), 8U);
    EXPECT_FALSE(wizard->canBack());
    wizard->lteSource()->setLteSource(FirmwareSourceViewModel::OwnFile);
    ASSERT_TRUE(loadInto(*wizard->lteSource(), bundleFile()));
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); })); // nRF Util detected
    wizard->primary();
    EXPECT_TRUE(tab().isBusy());
    EXPECT_EQ(tab().busyText(), QStringLiteral("Paused for the LTE firmware update"));
    // Plan 0046 (ADR-0366): on APP it says what will happen and waits for [Continue].
    ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Confirm));
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Ready to update the LTE MCU"));
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Continue"));
    EXPECT_EQ(wizard->promptText(),
              QStringLiteral("The application on this CHESTER will be erased. Then you move the "
                             "J-Link cable to the LTE connector, and at the end back to APP; the "
                             "application is written back."));
    EXPECT_TRUE(wizard->canPrimary());
    EXPECT_TRUE(wizard->canCancel());
    EXPECT_TRUE(wizard->busyCancelText().isEmpty()); // nothing changed yet: no question
    EXPECT_FALSE(wizard->procedure()->applicationErased());
    wizard->primary();
    // The copy and the erase, then the LTE connector. Cancel now asks first.
    ASSERT_TRUE(waitFor([&] {
        return wizard->procedure()->step() == hwio::device::LteUpdateProcedure::Step::ConnectLte;
    }));
    EXPECT_EQ(wizard->illustration(), QStringLiteral("chester-lte-cable"));
    EXPECT_EQ(wizard->current().illustration, QStringLiteral("chester-lte-cable"));
    EXPECT_TRUE(wizard->busyCancelText().startsWith(QStringLiteral(
        "The application MCU is erased. The device will not run until you flash an application.")));
    EXPECT_EQ(wizard->cancelAcceptText(), QStringLiteral("Cancel anyway"));
    EXPECT_EQ(wizard->cancelRejectText(), QStringLiteral("Back"));
    moveCable("lte");
    bool sawSecondStep = false;
    ASSERT_TRUE(waitFor(
        [&] {
            if (wizard->stage() == hwio::gui::LteUpdateWizard::Working) {
                EXPECT_FALSE(wizard->canCancel()); // not during a flash
                sawSecondStep =
                    sawSecondStep || wizard->stepText() == QStringLiteral("Step 2 of 2");
            }
            return wizard->procedure()->step() ==
                   hwio::device::LteUpdateProcedure::Step::ConnectAppAgain;
        },
        std::chrono::seconds(60)));
    EXPECT_TRUE(sawSecondStep);
    moveCable("app");
    ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Application));
    EXPECT_TRUE(wizard->hasBackup());
    EXPECT_TRUE(wizard->useBackup());
    EXPECT_TRUE(wizard->backupText().startsWith(QStringLiteral("Copy: ")));
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Write back"));
    wizard->primary();
    ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Summary));
    EXPECT_EQ(wizard->current().kind, hwio::gui::StepKind::Done);
    EXPECT_FALSE(tab().isBusy());
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    app().wizard()->action(QStringLiteral("open-console"));
    EXPECT_EQ(tab().currentPage(), QStringLiteral("console"));
    EXPECT_FALSE(app().wizard()->isOpen()); // the guide closes with the hand-off
}

TEST_F(FlashTest, LteUpdateWizardModemFailureCanBeRetriedOrCancelled) {
    qputenv("HWIO_NRFUTIL", QByteArray(HWIO_FAKE_NRFUTIL));
    qputenv("HWIO_FAKE_NRFUTIL_MODE", "fail");
    startBoard();
    hwio::gui::LteUpdateWizard* wizard = openLteWizard();
    ASSERT_NE(wizard, nullptr);
    wizard->lteSource()->setLteSource(FirmwareSourceViewModel::OwnFile);
    ASSERT_TRUE(loadInto(*wizard->lteSource(), modemFile()));
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Confirm));
    wizard->primary(); // [Continue]
    ASSERT_TRUE(waitFor([&] {
        return wizard->procedure()->step() == hwio::device::LteUpdateProcedure::Step::ConnectLte;
    }));
    moveCable("lte");
    ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Failed));
    EXPECT_EQ(wizard->error().code(), QStringLiteral("firmware.modem_update_failed"));
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Try again"));
    EXPECT_TRUE(wizard->canPrimary());
    EXPECT_TRUE(tab().isBusy()); // still in the procedure
    // Cancel asks (the application is erased); [Cancel anyway] closes the wizard.
    app().wizard()->cancel(false);
    EXPECT_TRUE(app().wizard()->isOpen());
    app().wizard()->cancel(true);
    EXPECT_FALSE(app().wizard()->isOpen());
    EXPECT_FALSE(tab().isBusy());
}

// Plan 0046 (ADR-0366): [Cancel] at "Ready to update the LTE MCU" closes the wizard without a
// question; the application stays and the console reconnects.
TEST_F(FlashTest, LteUpdateWizardCancelBeforeContinueChangesNothing) {
    startBoard();
    hwio::gui::LteUpdateWizard* wizard = openLteWizard();
    ASSERT_NE(wizard, nullptr);
    wizard->lteSource()->setLteSource(FirmwareSourceViewModel::OwnFile);
    ASSERT_TRUE(loadInto(*wizard->lteSource(), hexFile("lte.hex")));
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Confirm));
    EXPECT_EQ(wizard->current().id, QStringLiteral("app-connector"));
    EXPECT_TRUE(wizard->busyCancelText().isEmpty());
    app().wizard()->cancel(false); // no question: closes at once
    EXPECT_FALSE(app().wizard()->isOpen());
    ASSERT_TRUE(waitFor([this] { return !tab().isBusy(); })); // the closed wizard is deleted later
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
}

// ADR-0368: in a remote LTE update support answers Continue; the customer's wizard shows the
// explanation without a button, and the copy of the application is written back automatically.
// A no cancels before anything changed.
TEST_F(FlashTest, RemoteLteUpdateQuestionsGoToSupport) {
#ifndef HWIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HWIO_TEST_OVERRIDES (HWIO_DESIGNATED_URL)";
#endif
    hwio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    const QByteArray modem = hwio::firmware::test::zipBytes(
        {{.name = "firmware.update.image.segments.0.hex", .data = ":00000001FF\n"},
         {.name = "x" + randomId().toStdString() + ".txt", .data = "x"}});
    const QByteArray bundle = hwio::firmware::test::zipBytes(
        {{.name = "modem.zip", .data = modem.toStdString()},
         {.name = "application.hex", .data = hwio::firmware::test::toHex(image())}});
    http.set(QStringLiteral("/chester/lte.zip"),
             hwio::firmware::test::HttpFixture::Response::ok(bundle));
    qputenv("HWIO_DESIGNATED_URL", (http.baseUrl() + QStringLiteral("/lte.zip")).toUtf8());
    qputenv("HWIO_DESIGNATED_SHA256",
            QCryptographicHash::hash(bundle, QCryptographicHash::Sha256).toHex());
    qputenv("HWIO_NRFUTIL", QByteArray(HWIO_FAKE_NRFUTIL));
    startBoard();
    using hwio::remote::SupportAnswer;
    for (const SupportAnswer given :
         {SupportAnswer::No, SupportAnswer::NoAnswer, SupportAnswer::Yes}) {
        const bool yes = given == SupportAnswer::Yes;
        std::vector<hwio::remote::tunnel::OpQuestionKind> questions;
        hwio::remote::SupportAnswerFn answer;
        std::optional<hwio::Status> result;
        ASSERT_TRUE(tab()
                        .remoteShare()
                        ->start([](const hwio::remote::OperationUpdate&) {},
                                [&](hwio::Status status) { result = std::move(status); },
                                [&](hwio::remote::tunnel::OpQuestionKind kind,
                                    hwio::remote::SupportAnswerFn a) {
                                    questions.push_back(kind);
                                    answer = std::move(a);
                                })
                        .has_value());
        ASSERT_TRUE(waitFor([&] { return app().wizard()->isOpen(); }));
        auto* wizard = qobject_cast<hwio::gui::LteUpdateWizard*>(app().wizard()->wizard());
        ASSERT_NE(wizard, nullptr);
        const QPointer<hwio::gui::LteUpdateWizard> guard(wizard);
        ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Confirm));
        ASSERT_TRUE(waitFor([&] { return !questions.empty(); }));
        EXPECT_EQ(questions.front(), hwio::remote::tunnel::OpQuestionKind::Continue);
        EXPECT_TRUE(wizard->supportDecides());
        EXPECT_TRUE(wizard->current().primaryText.isEmpty()); // no [Continue] for the customer
        EXPECT_FALSE(wizard->canPrimary());
        EXPECT_TRUE(wizard->promptText().endsWith(
            QStringLiteral("HARDWARIO Support confirms the update. Keep the J-Link cable on the "
                           "APP connector for now.")))
            << wizard->promptText().toStdString();
        answer(given);
        if (!yes) {
            ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
            const hwio::Status cancelled = result.value_or(hwio::Status{});
            ASSERT_FALSE(cancelled.has_value());
            EXPECT_EQ(cancelled.error().code, ErrorCode::Cancelled);
            // Confirmed 2026-11-12: the customer is told that support cancelled, or did not answer.
            const QString told =
                given == SupportAnswer::No
                    ? QStringLiteral("Cancelled by support. Nothing was changed.")
                    : QStringLiteral("Support did not answer. Nothing was changed.");
            ASSERT_TRUE(waitFor([&] { return app().toasts().contains(told); }));
            // The cancelled guide may already have closed (and deleted) the wizard.
            EXPECT_FALSE(!guard.isNull() && guard->procedure() != nullptr &&
                         guard->procedure()->applicationErased());
            if (app().wizard()->isOpen()) {
                app().wizard()->cancel(true);
            }
            ASSERT_TRUE(waitFor([this] { return !app().wizard()->isOpen() && !tab().isBusy(); }));
            continue;
        }
        ASSERT_TRUE(waitFor([&] {
            return wizard->procedure()->step() ==
                   hwio::device::LteUpdateProcedure::Step::ConnectLte;
        }));
        moveCable("lte");
        ASSERT_TRUE(waitFor(
            [&] {
                return wizard->procedure()->step() ==
                       hwio::device::LteUpdateProcedure::Step::ConnectAppAgain;
            },
            std::chrono::seconds(60)));
        moveCable("app");
        // Written back without [Write back].
        ASSERT_TRUE(waitFor([&] { return result.has_value(); }, std::chrono::seconds(60)));
        const hwio::Status written = result.value_or(
            std::unexpected(hwio::makeError(ErrorCode::InternalUnexpected, "no result")));
        EXPECT_TRUE(written.has_value()) << (written ? std::string() : written.error().detail);
        EXPECT_EQ(questions.size(), 1U);
        ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Summary));
        app().wizard()->cancel(true);
    }
    qunsetenv("HWIO_DESIGNATED_URL");
    qunsetenv("HWIO_DESIGNATED_SHA256");
}

// Review F5 / ADR-0114 (moved here by plan 0033): the nRF Util path is read from the settings when
// the modem update is requested, not when the session was opened.
TEST_F(FlashTest, AChangedNrfUtilPathReachesTheLteUpdate) {
    qunsetenv("HWIO_NRFUTIL"); // the setting decides (the environment would win over it)
    startBoard();
    hwio::gui::SettingsStore& settings = *app().settingsStore();
    ASSERT_TRUE(
        settings.setValue(QStringLiteral("nrfutil/path"),
                          QString::fromStdString((root() / "no-nrfutil" / "nrfutil").string())));
    tab().connectDevice();
    const QPointer<hwio::device::DeviceSession> opened(tab().session());
    ASSERT_FALSE(opened.isNull());
    hwio::gui::LteUpdateWizard* wizard = openLteWizard();
    ASSERT_NE(wizard, nullptr);
    wizard->lteSource()->setLteSource(FirmwareSourceViewModel::OwnFile);
    ASSERT_TRUE(loadInto(*wizard->lteSource(), modemFile()));
    ASSERT_TRUE(waitFor([&] { return !wizard->prerequisiteText().isEmpty(); }));
    EXPECT_FALSE(wizard->canPrimary());
    ASSERT_TRUE(
        settings.setValue(QStringLiteral("nrfutil/path"), QString::fromUtf8(HWIO_FAKE_NRFUTIL)));
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    EXPECT_EQ(tab().session(), opened.data()); // no new session
    ASSERT_TRUE(reachStage(*wizard, hwio::gui::LteUpdateWizard::Confirm));
    wizard->primary(); // [Continue]
    ASSERT_TRUE(waitFor([&] {
        return wizard->procedure()->step() == hwio::device::LteUpdateProcedure::Step::ConnectLte;
    }));
    moveCable("lte");
    ASSERT_TRUE(waitFor(
        [&] {
            return wizard->procedure()->step() ==
                   hwio::device::LteUpdateProcedure::Step::ConnectAppAgain;
        },
        std::chrono::seconds(60)))
        << wizard->error().detail().toStdString();
    app().wizard()->cancel(true);
    EXPECT_FALSE(app().wizard()->isOpen());
}

// ADR-0233 (D8): the J-Link on the LTE connector - [Update LTE firmware…] of the banner.
TEST_F(FlashTest, TheOtherConnectorBannerOpensTheLteUpdate) {
    startBoard();
    moveCable("lte");
    tab().connectDevice();
    ASSERT_TRUE(waitFor([this] { return tab().connection()->otherConnector(); }));
    tab().connection()->updateLteFirmware();
    ASSERT_TRUE(app().wizard()->isOpen());
    EXPECT_EQ(app().wizard()->wizardId(), QStringLiteral("lte-update"));
    app().wizard()->cancel(false);
}

} // namespace
