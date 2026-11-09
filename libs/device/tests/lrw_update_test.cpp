// Plan 0040 A5 (ADR-0283): LrwUpdateProcedure through the real session against the fake J-Link
// library with a two-connector CHESTER ("connectors": app = nRF52840, lrw = STM32L0 cat. 5 192 KiB
// that answers only while the app chip is halted, "needsHalted"; the test moves the cable by
// writing the connector file): hold the app MCU, flash / erase / unlock the module, the unlock
// confirmation of an RDP 1 module, cancel and retry rules.

#include "hwio/core/error.h"
#include "hwio/device/device_session.h"
#include "hwio/device/lrw_update_procedure.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_registry.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/hw/jlink/library_locator.h"
#include "session_test_support.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hwio::ErrorCode;
using hwio::device::ConnectionState;
using hwio::device::DeviceSession;
using hwio::device::LrwUpdateProcedure;
using hwio::device::SessionOptions;
using hwio::device::SessionRegistry;
using hwio::device::test::waitFor;
using Step = LrwUpdateProcedure::Step;
using Action = LrwUpdateProcedure::Action;
using namespace std::chrono_literals;

class LrwUpdate : public ::testing::Test {
protected:
    void SetUp() override {
        m_registry = std::make_unique<SessionRegistry>(
            hwio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                          .lockDirectory = root() / "locks",
                                          .consoleDirectory = root() / "console"});
    }
    void TearDown() override {
        m_procedure.reset();
        m_session.reset();
        qunsetenv("HWIO_FAKE_JLINK_SCENARIO");
    }

    // One probe on a CHESTER with the APP and the LoRaWAN connector; `lrwChip` extends the module.
    void useBoard(int rdp, const std::string& lrwChip = "") {
        moveCable("app");
        const std::string json = std::format(
            R"({{"probes": [{{"serialNumber": 34, "product": "Test Probe",
                "rtt": {{"output": [{{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}}]}},
                "connectors": {{"file": "{}", "initial": "app", "chips": {{
                  "app": {{}},
                  "lrw": {{"family": "stm32l0", "devId": 1095, "revId": 8200, "flashKib": 192,
                          "eepromBytes": 6144, "rdp": {}, "needsHalted": "app"{}}}}}}}}}]}})",
            connectorFile().generic_string(), rdp, lrwChip);
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

    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }
    [[nodiscard]] fs::path connectorFile() const { return root() / "connector"; }

    DeviceSession& open() {
        SessionOptions options;
        options.profile = hwio::device::ProfileRegistry::find("chester");
        options.targetId = "app";
        options.probeSn = 34;
        options.speedKhz = 2000;
        options.latencyMs = 10;
        options.autoReconnect = true;
        options.consoleFile = fs::path();
        auto session = DeviceSession::open(
            *m_registry, hwio::jlink::LibraryLocation{.path = fs::path(HWIO_FAKE_JLINK_LIBRARY)},
            options);
        EXPECT_TRUE(session) << (session ? std::string() : session.error().message);
        m_session = std::move(*session);
        return *m_session;
    }

    LrwUpdateProcedure& procedure() {
        m_procedure = std::make_unique<LrwUpdateProcedure>(
            *m_session, LrwUpdateProcedure::Options{.pollInterval = 20ms});
        return *m_procedure;
    }

    // 1 KiB of 0x5A at the module's flash start.
    static LrwUpdateProcedure::Request flashRequest() {
        std::vector<hwio::firmware::Segment> segments;
        segments.push_back(hwio::firmware::Segment{
            .address = 0x08000000, .data = std::vector<std::byte>(1024, std::byte{0x5A})});
        return LrwUpdateProcedure::Request{
            .action = Action::Flash,
            .image = std::make_shared<const hwio::firmware::FirmwareImage>(
                *hwio::firmware::FirmwareImage::fromSegments(std::move(segments))),
            .name = "lora-modem for CHESTER 1.4.1",
            .options = {}};
    }

    static LrwUpdateProcedure::Request request(Action action) {
        return LrwUpdateProcedure::Request{
            .action = action, .image = nullptr, .name = {}, .options = {}};
    }

    bool reach(Step step, std::chrono::milliseconds timeout = 20s) {
        return waitFor([&] { return m_procedure->step() == step; }, timeout);
    }

    // Runs one operation of the session after the procedure's last identify has ended.
    hwio::device::OperationResult run(const hwio::Result<hwio::device::OperationId>& id) {
        EXPECT_TRUE(id) << (id ? std::string() : id.error().detail);
        if (!id) {
            return {};
        }
        return hwio::device::waitForOperation(*m_session, *id);
    }

    void idle() {
        EXPECT_TRUE(waitFor([this] { return !m_session->runningOperation().has_value(); }));
    }

    // After Done: hold the app MCU again and read the module's first bytes.
    std::vector<std::byte> moduleBytes(std::size_t size) {
        idle();
        EXPECT_TRUE(run(m_session->reset("app", hwio::device::ResetMode::Halt)).status);
        moveCable("lrw");
        const auto result = run(m_session->readMemory("lrw", 0x08000000, size));
        EXPECT_TRUE(result.status) << result.status.error().detail;
        return result.data;
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<SessionRegistry> m_registry;
    std::unique_ptr<DeviceSession> m_session;
    std::unique_ptr<LrwUpdateProcedure> m_procedure;
};

// The fake mirrors the hardware: the module answers only while the application MCU is held.
TEST_F(LrwUpdate, TheModuleAnswersOnlyWhileTheApplicationIsHeld) {
    useBoard(0);
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    auto hold = session.hold();
    ASSERT_TRUE(hold);
    idle();
    moveCable("lrw");
    const auto running = run(session.identifyTarget("lrw"));
    ASSERT_TRUE(running.familyMatch.has_value());
    EXPECT_FALSE(running.familyMatch.value_or(hwio::device::ChipFamilyMatch{}).matches());
    moveCable("app");
    EXPECT_TRUE(run(session.reset("app", hwio::device::ResetMode::Halt)).status);
    moveCable("lrw");
    const auto held = run(session.identifyTarget("lrw"));
    EXPECT_TRUE(held.familyMatch.value_or(hwio::device::ChipFamilyMatch{}).matches());
    ASSERT_TRUE(held.chip.has_value());
    EXPECT_EQ(held.chip.value_or(hwio::jlink::ChipInfo{}).devId, 0x447U);
}

TEST_F(LrwUpdate, HappyPathHoldsTheAppFlashesTheModuleAndStartsTheAppAgain) {
    useBoard(0);
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LrwUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(flashRequest()));
    EXPECT_TRUE(session.held());
    // On APP already: the application MCU is stopped, then it waits for the module.
    ASSERT_TRUE(reach(Step::ConnectLrw));
    EXPECT_TRUE(update.applicationHeld());
    EXPECT_EQ(update.connectorLabel(), QStringLiteral("LoRaWAN"));
    EXPECT_EQ(update.promptText(),
              QStringLiteral("The application MCU is stopped. Move the J-Link cable to the "
                             "LoRaWAN connector of the LoRaWAN module."));
    hwio::device::test::spin(100ms);
    EXPECT_EQ(update.step(), Step::ConnectLrw); // the cable is still on APP
    moveCable("lrw");
    ASSERT_TRUE(reach(Step::ConnectAppAgain));
    ASSERT_TRUE(update.module().has_value());
    EXPECT_EQ(update.module().value_or(hwio::jlink::ChipInfo{}).rdpLevel, 0);
    EXPECT_FALSE(update.unlocked());
    EXPECT_EQ(update.connectorLabel(), QStringLiteral("APP"));
    EXPECT_EQ(update.promptText(),
              QStringLiteral("Move the J-Link cable back to the APP connector."));
    moveCable("app");
    ASSERT_TRUE(reach(Step::Done));
    EXPECT_FALSE(update.applicationHeld());
    EXPECT_FALSE(session.held());
    EXPECT_EQ(update.promptText(),
              QStringLiteral("The LoRaWAN module was flashed with lora-modem for CHESTER 1.4.1 "
                             "and the application is running again."));
    EXPECT_TRUE(waitFor([&] { return session.connection().state == ConnectionState::Connected; }));
    EXPECT_EQ(moduleBytes(4), std::vector<std::byte>(4, std::byte{0x5A}));
}

TEST_F(LrwUpdate, AProtectedModuleNeedsTheUnlockConfirmation) {
    useBoard(1);
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LrwUpdateProcedure& update = procedure();
    EXPECT_EQ(update.confirmUnlock().error().code, ErrorCode::UsageInvalidArgument);
    ASSERT_TRUE(update.start(flashRequest()));
    ASSERT_TRUE(reach(Step::ConnectLrw));
    moveCable("lrw");
    ASSERT_TRUE(reach(Step::NeedsUnlockConfirmation));
    EXPECT_EQ(update.module().value_or(hwio::jlink::ChipInfo{}).rdpLevel, 1);
    EXPECT_EQ(update.promptText(),
              QStringLiteral("The LoRaWAN module is read-out protected. Unlocking erases it "
                             "completely; the module's original Murata firmware is removed."));
    // Nothing happens without the confirmation.
    hwio::device::test::spin(200ms);
    EXPECT_EQ(update.step(), Step::NeedsUnlockConfirmation);
    EXPECT_TRUE(update.canCancel());
    ASSERT_TRUE(update.confirmUnlock());
    ASSERT_TRUE(reach(Step::ConnectAppAgain, 60s));
    EXPECT_TRUE(update.unlocked());
    EXPECT_EQ(update.module().value_or(hwio::jlink::ChipInfo{}).rdpLevel, 0);
    moveCable("app");
    ASSERT_TRUE(reach(Step::Done));
    EXPECT_EQ(moduleBytes(4), std::vector<std::byte>(4, std::byte{0x5A}));
}

TEST_F(LrwUpdate, CancelAtTheUnlockConfirmationLeavesTheModuleAndSaysTheAppIsStopped) {
    useBoard(1);
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LrwUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(request(Action::Erase)));
    ASSERT_TRUE(reach(Step::ConnectLrw));
    moveCable("lrw");
    ASSERT_TRUE(reach(Step::NeedsUnlockConfirmation));
    EXPECT_TRUE(update.cancel());
    EXPECT_EQ(update.step(), Step::Cancelled);
    EXPECT_FALSE(session.held());
    EXPECT_TRUE(update.applicationHeld());
    EXPECT_EQ(update.promptText(),
              QStringLiteral("The LoRaWAN module operation was cancelled. The application MCU "
                             "stays stopped until CHESTER is reset or powered off and on."));
    idle();
    const auto info = run(session.identifyTarget("lrw"));
    EXPECT_EQ(info.chip.value_or(hwio::jlink::ChipInfo{}).rdpLevel, 1); // not unlocked
}

TEST_F(LrwUpdate, TheUnlockActionNeedsNoSecondConfirmation) {
    useBoard(1);
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LrwUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(request(Action::Unlock)));
    ASSERT_TRUE(reach(Step::ConnectLrw));
    moveCable("lrw");
    ASSERT_TRUE(reach(Step::ConnectAppAgain, 60s));
    EXPECT_TRUE(update.unlocked());
    moveCable("app");
    ASSERT_TRUE(reach(Step::Done));
    EXPECT_EQ(update.promptText(), QStringLiteral("The LoRaWAN module was unlocked and the "
                                                  "application is running again."));
}

TEST_F(LrwUpdate, EraseErasesTheModule) {
    useBoard(0, R"(, "memory": [{"address": 134217728, "hex": "11223344"}])");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LrwUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(request(Action::Erase)));
    ASSERT_TRUE(reach(Step::ConnectLrw));
    moveCable("lrw");
    ASSERT_TRUE(reach(Step::ConnectAppAgain));
    moveCable("app");
    ASSERT_TRUE(reach(Step::Done));
    EXPECT_EQ(update.promptText(), QStringLiteral("The LoRaWAN module was erased and the "
                                                  "application is running again."));
    EXPECT_EQ(moduleBytes(4), std::vector<std::byte>(4, std::byte{0x00})); // STM32 erased
}

TEST_F(LrwUpdate, AFailedFlashCanBeRetriedAndNothingIsCancelledDuringIt) {
    useBoard(0, R"(, "writeFault": true, "downloadDelayMs": 300)");
    DeviceSession& session = open();
    ASSERT_TRUE(hwio::device::waitUntilConnected(session, 10s));
    LrwUpdateProcedure& update = procedure();
    ASSERT_TRUE(update.start(flashRequest()));
    ASSERT_TRUE(reach(Step::ConnectLrw));
    moveCable("lrw");
    ASSERT_TRUE(reach(Step::Flash));
    EXPECT_FALSE(update.canCancel());
    EXPECT_FALSE(update.cancel());
    ASSERT_TRUE(reach(Step::Failed));
    EXPECT_EQ(update.failedStep(), Step::Flash);
    EXPECT_EQ(update.error().value_or(hwio::Error{}).code, ErrorCode::TargetProgramFailed);
    EXPECT_TRUE(update.canRetry());
    EXPECT_TRUE(session.held()); // still in the procedure
    update.retry();
    EXPECT_EQ(update.step(), Step::ConnectLrw);
    ASSERT_TRUE(reach(Step::Failed)); // the fake keeps failing
    EXPECT_TRUE(update.cancel());
    EXPECT_FALSE(session.held());
}

TEST_F(LrwUpdate, StartIsRefusedTwiceAndWithoutFirmware) {
    useBoard(0);
    open();
    LrwUpdateProcedure& update = procedure();
    EXPECT_EQ(update.start(request(Action::Flash)).error().code, ErrorCode::UsageInvalidArgument);
    ASSERT_TRUE(update.start(request(Action::Erase)));
    EXPECT_EQ(update.start(request(Action::Erase)).error().code, ErrorCode::UsageInvalidArgument);
}

} // namespace
