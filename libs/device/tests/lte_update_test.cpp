// Plan 0033 (ADR-0233, ADR-0234) through the real session against the fake J-Link library with a
// two-connector board ("connectors": the test moves the cable by writing the connector file):
// the family check at connect (target.other_connector), identifyTarget(), SessionHold,
// ApplicationBackup and every path of LteUpdateProcedure (happy, wrong connector, cancel before
// and after the erase, no copy, failures and retries of each flash).

#include "hwio/core/error.h"
#include "hwio/device/application_backup.h"
#include "hwio/device/device_session.h"
#include "hwio/device/lte_update_procedure.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/device/session_registry.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/intel_hex.h"
#include "hwio/hw/jlink/library_locator.h"
#include "session_test_support.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>
#include <QtSystemDetection>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hwio::ErrorCode;
using hwio::device::ApplicationBackup;
using hwio::device::ConnectionState;
using hwio::device::DeviceSession;
using hwio::device::LteUpdateProcedure;
using hwio::device::SessionOptions;
using hwio::device::SessionRegistry;
using hwio::device::test::waitFor;
using Step = LteUpdateProcedure::Step;
using namespace std::chrono_literals;

// The application bytes the fake app chip starts with (at 0x0 and at 0x1000).
constexpr const char* appMemory =
    R"([{"address": 0, "hex": "00000420c1000000"}, {"address": 4096, "hex": "a5a5a5a5"}])";

class LteUpdate : public ::testing::Test {
protected:
    void SetUp() override { resetRegistry(); }
    void TearDown() override {
        m_procedure.reset();
        m_session.reset();
        qunsetenv("HWIO_FAKE_JLINK_SCENARIO");
    }

    // One probe on a two-connector CHESTER; `appChip` / `lteChip` extend the chips.
    void useBoard(const std::string& initial = "app", const std::string& appChip = "",
                  const std::string& lteChip = "") {
        moveCable(initial);
        const std::string json = std::format(
            R"({{"probes": [{{"serialNumber": 33, "product": "Test Probe",
                "rtt": {{"output": [{{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}}]}},
                "connectors": {{"file": "{}", "initial": "app", "chips": {{
                  "app": {{"memory": {}{}}},
                  "lte": {{"family": "nrf91"{}}}}}}}}}]}})",
            connectorFile().generic_string(), appMemory, appChip, lteChip);
        const QString path = m_dir.filePath(QStringLiteral("scenario.json"));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray::fromStdString(json));
        file.close();
        qputenv("HWIO_FAKE_JLINK_SCENARIO", path.toUtf8());
    }

    void moveCable(const std::string& connector) const {
        QFile file(QString::fromStdString(connectorFile().string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray::fromStdString(connector));
    }

    void resetRegistry() {
        m_registry = std::make_unique<SessionRegistry>(
            hwio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                          .lockDirectory = root() / "locks",
                                          .consoleDirectory = root() / "console"});
    }

    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }
    [[nodiscard]] fs::path connectorFile() const { return root() / "connector"; }
    [[nodiscard]] fs::path backups() const { return root() / "backups"; }

    DeviceSession& open(bool autoReconnect = true) {
        SessionOptions options;
        options.profile = hwio::device::ProfileRegistry::find("chester");
        options.targetId = "app";
        options.probeSn = 33;
        options.speedKhz = 2000;
        options.latencyMs = 10;
        options.autoReconnect = autoReconnect;
        options.consoleFile = fs::path();
        auto session = DeviceSession::open(
            *m_registry, hwio::jlink::LibraryLocation{.path = fs::path(HWIO_FAKE_JLINK_LIBRARY)},
            options);
        EXPECT_TRUE(session) << (session ? std::string() : session.error().message);
        m_session = std::move(*session);
        return *m_session;
    }

    LteUpdateProcedure& procedure(bool autoRestore = false) {
        m_procedure = std::make_unique<LteUpdateProcedure>(
            *m_session, LteUpdateProcedure::Options{.pollInterval = 20ms,
                                                    .backupDirectory = backups(),
                                                    .autoRestore = autoRestore,
                                                    .now = {}});
        return *m_procedure;
    }

    // The LTE MCU image of the tests (nRF91 flash).
    static LteUpdateProcedure::Source lteImage() {
        std::vector<hwio::firmware::Segment> segments;
        segments.push_back(hwio::firmware::Segment{
            .address = 0x0, .data = std::vector<std::byte>(8192, std::byte{0x91})});
        return LteUpdateProcedure::Source{
            .kind = LteUpdateProcedure::Source::Kind::Image,
            .image = std::make_shared<const hwio::firmware::FirmwareImage>(
                *hwio::firmware::FirmwareImage::fromSegments(std::move(segments))),
            .bundle = nullptr,
            .modemZip = {},
            .name = "lte.hex",
            .options = {}};
    }

    bool reach(Step step, std::chrono::milliseconds timeout = 20s) {
        return waitFor([&] { return m_procedure->step() == step; }, timeout);
    }

    std::vector<std::byte> read(const char* target, std::uint32_t address, std::size_t size) {
        // A cancelled identify of the procedure may still be ending (internal.busy otherwise).
        EXPECT_TRUE(waitFor([this] { return !m_session->runningOperation().has_value(); }));
        auto id = m_session->readMemory(target, address, size);
        EXPECT_TRUE(id);
        if (!id) {
            return {};
        }
        const auto result = hwio::device::waitForOperation(*m_session, *id);
        EXPECT_TRUE(result.status) << result.status.error().detail;
        return result.data;
    }

    void closeProcedure() { m_procedure.reset(); }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<SessionRegistry> m_registry;
    std::unique_ptr<DeviceSession> m_session;
    std::unique_ptr<LteUpdateProcedure> m_procedure;
};

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> out;
    for (const std::uint8_t v : values) {
        out.push_back(std::byte{v});
    }
    return out;
}

// ---- the family check at connect and identifyTarget (D1) --------------------------------------

TEST_F(LteUpdate, AConsoleSessionOnTheLteConnectorStopsWithOtherConnector) {
    useBoard("lte");
    DeviceSession& session = open();
    ASSERT_TRUE(waitFor([&] { return session.connection().state == ConnectionState::Stopped; }));
    ASSERT_TRUE(session.connection().error.has_value());
    const hwio::Error error = session.connection().error.value_or(hwio::Error{});
    EXPECT_EQ(error.code, ErrorCode::TargetOtherConnector);
    EXPECT_EQ(error.message, "The J-Link is connected to the LTE MCU.");
    EXPECT_EQ(error.wizard, hwio::WizardId::LteUpdate);
    EXPECT_NE(error.detail.find("lte"), std::string::npos);
    // No auto-reconnect in this state, even after the cable moved: the user acts (D8).
    moveCable("app");
    hwio::device::test::spin(500ms);
    EXPECT_EQ(session.connection().state, ConnectionState::Stopped);
    ASSERT_TRUE(session.reconnect()); // [Retry]
    EXPECT_TRUE(waitFor([&] { return session.connection().state == ConnectionState::Connected; }));
}

TEST_F(LteUpdate, IdentifyTargetNamesTheMcuBehindTheProbe) {
    useBoard("app");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    const auto identify = [&](const char* target) {
        auto id = session.identifyTarget(target);
        EXPECT_TRUE(id);
        return hwio::device::waitForOperation(session, *id);
    };
    auto app = identify("app");
    ASSERT_TRUE(app.status);
    ASSERT_TRUE(app.familyMatch.has_value());
    EXPECT_TRUE(app.familyMatch.value_or(hwio::device::ChipFamilyMatch{}).matches());
    // A light operation on the console target: the console stays Connected (ADR-0157).
    EXPECT_EQ(session.connection().state, ConnectionState::Connected);
    auto lte = identify("lte");
    ASSERT_TRUE(lte.familyMatch.has_value());
    const hwio::device::ChipFamilyMatch match =
        lte.familyMatch.value_or(hwio::device::ChipFamilyMatch{});
    EXPECT_EQ(match.expectedTargetId, "lte");
    EXPECT_EQ(match.foundTargetId, "app");
    EXPECT_FALSE(match.matches());
}

// ---- SessionHold (D4) --------------------------------------------------------------------------

TEST_F(LteUpdate, AHeldSessionDoesNotReconnectUntilReleased) {
    useBoard("app");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    {
        auto hold = session.hold();
        ASSERT_TRUE(hold);
        EXPECT_TRUE(session.held());
        ASSERT_TRUE(
            waitFor([&] { return session.connection().state == ConnectionState::Stopped; }));
        EXPECT_FALSE(session.connection().error.has_value());
        ASSERT_TRUE(session.reconnect()); // ignored while held
        hwio::device::test::spin(300ms);
        EXPECT_EQ(session.connection().state, ConnectionState::Stopped);
        // Operations still run, and nothing is attached after them.
        EXPECT_EQ(read("app", 0x1000, 4), bytes({0xa5, 0xa5, 0xa5, 0xa5}));
        hwio::device::test::spin(200ms);
        EXPECT_EQ(session.connection().state, ConnectionState::Stopped);
        auto moved = std::move(*hold);
        EXPECT_TRUE(moved.active());
        EXPECT_FALSE(hold->active());
    } // released: the console reconnects
    EXPECT_FALSE(session.held());
    EXPECT_TRUE(waitFor([&] { return session.connection().state == ConnectionState::Connected; }));
}

// ---- ApplicationBackup (D3) --------------------------------------------------------------------

TEST(ApplicationBackupTest, RangeIsTheProfilesCodeFlashWithinTheBound) {
    const auto* profile = hwio::device::ProfileRegistry::find("chester");
    ASSERT_NE(profile, nullptr);
    const auto range = ApplicationBackup::rangeOf(*hwio::device::findTarget(*profile, "app"));
    ASSERT_TRUE(range);
    EXPECT_EQ(range->start, 0U);
    EXPECT_EQ(range->size(), 1024U * 1024U);
    hwio::device::TargetProfile big = *hwio::device::findTarget(*profile, "app");
    big.flash = hwio::firmware::AddressRange{.start = 0, .end = 8ULL * 1024 * 1024};
    EXPECT_EQ(ApplicationBackup::rangeOf(big).error().code, ErrorCode::UsageInvalidArgument);
}

TEST(ApplicationBackupTest, TrailingErasedPagesAreTrimmed) {
    std::vector<std::byte> data(5 * ApplicationBackup::pageBytes, std::byte{0xFF});
    data[10] = std::byte{1};
    data[(2 * ApplicationBackup::pageBytes) + 5] = std::byte{2};
    EXPECT_EQ(ApplicationBackup::trimmed(data).size(), 3 * ApplicationBackup::pageBytes);
    EXPECT_TRUE(ApplicationBackup::trimmed(std::vector<std::byte>(8192, std::byte{0xFF})).empty());
    EXPECT_EQ(ApplicationBackup::trimmed(std::vector<std::byte>(10, std::byte{0})).size(), 10U);
}

TEST(ApplicationBackupTest, SavesIntelHexOwnerOnlyAndKeepsTheNewestTen) {
    const QTemporaryDir dir;
    const fs::path directory = fs::path(dir.path().toStdString()) / "backups";
    const std::vector<std::byte> data = bytes({1, 2, 3, 4, 5});
    const auto start = std::chrono::sys_days{std::chrono::year{2026} / 11 / 4};
    fs::path first;
    fs::path last;
    for (int i = 0; i < 12; ++i) {
        auto saved = ApplicationBackup::save(directory, "2159018/x", start + std::chrono::hours(i),
                                             0x1000, data);
        ASSERT_TRUE(saved) << saved.error().detail;
        if (i == 0) {
            first = *saved;
        }
        last = *saved;
    }
    EXPECT_EQ(last.filename().string(), "2159018_x-20261104-110000.hex");
    EXPECT_FALSE(fs::exists(first)); // the oldest two are gone
    EXPECT_EQ(QDir(dir.path() + QStringLiteral("/backups")).entryList(QDir::Files).size(),
              static_cast<qsizetype>(ApplicationBackup::keepFiles));
    const auto image = hwio::firmware::readIntelHexFile(last);
    ASSERT_TRUE(image);
    ASSERT_EQ(image->segments().size(), 1U);
    EXPECT_EQ(image->segments().front().address, 0x1000U);
    EXPECT_EQ(image->segments().front().data, data);
#ifndef Q_OS_WIN
    const QFileDevice::Permissions permissions =
        QFile::permissions(QString::fromStdString(last.string()));
    EXPECT_FALSE(permissions & (QFileDevice::ReadGroup | QFileDevice::ReadOther));
#endif
    EXPECT_EQ(ApplicationBackup::save(directory, "", start, 0, {}).error().code,
              ErrorCode::UsageInvalidArgument);
}

// ---- LteUpdateProcedure (E2) --------------------------------------------------------------------

TEST_F(LteUpdate, HappyPathBacksUpErasesUpdatesAndWritesTheApplicationBack) {
    useBoard("app");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LteUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(lteImage()));
    EXPECT_TRUE(session.held());
    // On APP already: the copy and the erase run by themselves, then it waits for LTE.
    ASSERT_TRUE(reach(Step::ConnectLte));
    EXPECT_TRUE(update.hasBackup());
    EXPECT_TRUE(update.applicationErased());
    EXPECT_EQ(update.connectorLabel(), QStringLiteral("LTE"));
    // The cable is still on APP: said after the first check.
    ASSERT_TRUE(waitFor([&] { return update.wrongConnector(); }));
    EXPECT_EQ(update.promptText(),
              QStringLiteral("This is the APP connector. Move the cable to the LTE connector."));
    // The copy holds what was in the device (trimmed to its last written page).
    const auto copy = hwio::firmware::readIntelHexFile(update.backupFile());
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy->totalBytes(), 2U * ApplicationBackup::pageBytes);
    EXPECT_TRUE(update.backupFile().filename().string().starts_with("unknown-"));

    moveCable("lte");
    ASSERT_TRUE(reach(Step::ConnectAppAgain));
    ASSERT_TRUE(waitFor([&] { return update.wrongConnector(); }));
    EXPECT_EQ(update.promptText(),
              QStringLiteral("This is the LTE connector. Move the cable to the APP connector."));
    moveCable("app");
    ASSERT_TRUE(reach(Step::ChooseApplication));
    EXPECT_EQ(update.promptText(),
              QStringLiteral("Write back the application that was in the device."));
    ASSERT_TRUE(update.restoreApplication());
    ASSERT_TRUE(reach(Step::Done));
    EXPECT_FALSE(update.applicationErased());
    EXPECT_FALSE(session.held());
    // The application is back and the console reconnects on it.
    EXPECT_TRUE(waitFor([&] { return session.connection().state == ConnectionState::Connected; }));
    EXPECT_EQ(read("app", 0x1000, 4), bytes({0xa5, 0xa5, 0xa5, 0xa5}));
    EXPECT_EQ(read("app", 0x0, 4), bytes({0x00, 0x00, 0x04, 0x20}));
}

TEST_F(LteUpdate, StartsOnTheWrongConnectorAndWaits) {
    useBoard("app");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    moveCable("lte");
    LteUpdateProcedure& update = procedure(true);
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(waitFor([&] { return update.wrongConnector(); }));
    EXPECT_EQ(update.step(), Step::ConnectApp);
    EXPECT_EQ(update.promptText(),
              QStringLiteral("This is the LTE connector. Move the cable to the APP connector."));
    hwio::device::test::spin(200ms);
    EXPECT_FALSE(update.applicationErased());
    moveCable("app");
    ASSERT_TRUE(reach(Step::ConnectLte));
    moveCable("lte");
    ASSERT_TRUE(reach(Step::ConnectAppAgain));
    moveCable("app");
    // autoRestore (the CLI): the copy is written back without waiting.
    ASSERT_TRUE(reach(Step::Done));
}

TEST_F(LteUpdate, CancelBeforeTheEraseLeavesTheApplication) {
    useBoard("lte");
    DeviceSession& session = open();
    ASSERT_TRUE(waitFor([&] { return session.connection().state == ConnectionState::Stopped; }));
    LteUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(waitFor([&] { return update.wrongConnector(); }));
    EXPECT_TRUE(update.canCancel());
    EXPECT_TRUE(update.cancel());
    EXPECT_EQ(update.step(), Step::Cancelled);
    EXPECT_EQ(update.promptText(), QStringLiteral("The LTE firmware update was cancelled."));
    EXPECT_FALSE(session.held());
    moveCable("app");
    ASSERT_TRUE(session.reconnect());
    ASSERT_TRUE(waitFor([&] { return session.connection().state == ConnectionState::Connected; }));
    EXPECT_EQ(read("app", 0x1000, 4), bytes({0xa5, 0xa5, 0xa5, 0xa5}));
}

TEST_F(LteUpdate, CancelAfterTheEraseSaysSoAndKeepsTheCopy) {
    useBoard("app");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LteUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(reach(Step::ConnectLte));
    EXPECT_TRUE(update.cancel());
    EXPECT_EQ(update.promptText(), QStringLiteral("The application MCU is erased. The device will "
                                                  "not run until you flash an application."));
    EXPECT_TRUE(fs::exists(update.backupFile()));
    EXPECT_EQ(read("app", 0x1000, 4), std::vector<std::byte>(4, std::byte{0xFF}));
}

TEST_F(LteUpdate, AProtectedApplicationGoesOnWithoutACopy) {
    useBoard("app", R"(, "protected": true)");
    DeviceSession& session = open();
    ASSERT_TRUE(waitFor([&] { return session.connection().state != ConnectionState::Connecting; }));
    LteUpdateProcedure& update = procedure(true);
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(reach(Step::BackupFailed));
    ASSERT_TRUE(update.error().has_value());
    EXPECT_EQ(update.error().value_or(hwio::Error{}).code, ErrorCode::TargetProtected);
    EXPECT_FALSE(update.applicationErased());
    update.continueWithoutBackup();
    // The code-only erase cannot work on a protected MCU: a retryable failure.
    ASSERT_TRUE(reach(Step::Failed));
    EXPECT_TRUE(update.canRetry());
    EXPECT_TRUE(update.cancel());
}

TEST_F(LteUpdate, WithoutACopyTheApplicationMustBeChosen) {
    useBoard("app");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    // An empty application flash: nothing to copy, nothing to restore.
    LteUpdateProcedure& update = procedure(true);
    auto erase = session.erase("app", hwio::device::EraseScope::Code);
    ASSERT_TRUE(erase);
    ASSERT_TRUE(hwio::device::waitForOperation(session, *erase).status);
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(reach(Step::ConnectLte));
    EXPECT_FALSE(update.hasBackup());
    moveCable("lte");
    ASSERT_TRUE(reach(Step::ConnectAppAgain));
    moveCable("app");
    ASSERT_TRUE(reach(Step::ChooseApplication)); // autoRestore needs a copy
    EXPECT_EQ(update.promptText(), QStringLiteral("Choose the application firmware to flash."));
    EXPECT_EQ(update.restoreApplication().error().code, ErrorCode::UsageInvalidArgument);
    std::vector<hwio::firmware::Segment> segments;
    segments.push_back(hwio::firmware::Segment{
        .address = 0x2000, .data = std::vector<std::byte>(16, std::byte{0x42})});
    ASSERT_TRUE(update.restoreApplication(std::make_shared<const hwio::firmware::FirmwareImage>(
        *hwio::firmware::FirmwareImage::fromSegments(std::move(segments)))));
    ASSERT_TRUE(reach(Step::Done));
    EXPECT_EQ(read("app", 0x2000, 2), bytes({0x42, 0x42}));
}

TEST_F(LteUpdate, AFailedLteFlashCanBeRetried) {
    useBoard("app", "", R"(, "writeFault": true)");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LteUpdateProcedure& update = procedure(true);
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(reach(Step::ConnectLte));
    moveCable("lte");
    ASSERT_TRUE(reach(Step::Failed));
    ASSERT_TRUE(update.error().has_value());
    EXPECT_EQ(update.error().value_or(hwio::Error{}).code, ErrorCode::TargetProgramFailed);
    EXPECT_TRUE(update.canRetry());
    EXPECT_TRUE(session.held()); // still in the procedure
    update.retry();
    EXPECT_EQ(update.step(), Step::ConnectLte);
    ASSERT_TRUE(reach(Step::Failed)); // the fake keeps failing
    EXPECT_TRUE(update.cancel());
    EXPECT_FALSE(session.held());
}

TEST_F(LteUpdate, AFailedRestoreCanBeRetried) {
    useBoard("app", R"(, "verifyMismatchAt": 4096)");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LteUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(reach(Step::ConnectLte));
    moveCable("lte");
    ASSERT_TRUE(reach(Step::ConnectAppAgain));
    moveCable("app");
    ASSERT_TRUE(reach(Step::ChooseApplication));
    ASSERT_TRUE(update.restoreApplication());
    ASSERT_TRUE(reach(Step::Failed));
    EXPECT_EQ(update.error().value_or(hwio::Error{}).code, ErrorCode::FirmwareVerifyFailed);
    EXPECT_TRUE(update.applicationErased()); // nothing usable was written back
    update.retry();
    EXPECT_EQ(update.step(), Step::ConnectAppAgain);
    ASSERT_TRUE(reach(Step::ChooseApplication));
}

TEST_F(LteUpdate, NothingCanBeCancelledDuringAFlashAndAClosedSessionFails) {
    useBoard("app", "", R"(, "downloadDelayMs": 400)");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LteUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(reach(Step::ConnectLte));
    moveCable("lte");
    ASSERT_TRUE(reach(Step::UpdateLte));
    EXPECT_FALSE(update.canCancel());
    EXPECT_FALSE(update.cancel());
    ASSERT_TRUE(reach(Step::ConnectAppAgain));
    session.close();
    closeProcedure(); // the procedure ends with the session's owner (the wizard)
}

// The cable moves between the connector check and the operation: the step waits again instead of
// failing (the operation found the other MCU before it wrote anything).
TEST_F(LteUpdate, ACableMovedAfterTheCheckWaitsForTheConnectorAgain) {
    useBoard("app", R"(, "readDelayMs": 30)");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LteUpdateProcedure& update = procedure(true);
    ASSERT_TRUE(update.start(lteImage()));
    ASSERT_TRUE(reach(Step::Backup));
    moveCable("lte"); // during the copy
    ASSERT_TRUE(
        waitFor([&] { return update.step() == Step::ConnectApp && update.wrongConnector(); }));
    EXPECT_FALSE(update.applicationErased());
    EXPECT_FALSE(update.error().has_value());
    moveCable("app");
    ASSERT_TRUE(reach(Step::ConnectLte));
    EXPECT_TRUE(update.hasBackup());
}

TEST_F(LteUpdate, StartIsRefusedTwiceAndWithoutFirmware) {
    useBoard("app");
    open();
    LteUpdateProcedure& update = procedure();
    LteUpdateProcedure::Source empty;
    EXPECT_EQ(update.start(empty).error().code, ErrorCode::UsageInvalidArgument);
    ASSERT_TRUE(update.start(lteImage()));
    EXPECT_EQ(update.start(lteImage()).error().code, ErrorCode::UsageInvalidArgument);
}

} // namespace
