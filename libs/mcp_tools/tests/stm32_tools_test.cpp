// The STM32 tools (plan/features/0039 B2, ADR-0280 D9) on real DeviceSessions with the fake J-Link
// library's STM32L0 model: stm32_info, stm32_flash (.bin, .hex, wrong chip, EEPROM consent),
// stm32_erase, stm32_unlock (confirm=true and the erase permission), and the refusal on a Nordic
// target.

#include "hwio/device/profile.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/limits.h"
#include "session_provider.h"
#include "tools_test_support.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace hwio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;

constexpr const char* device = "jlink:801000001";

QByteArray scenario(int rdp, int devId = 0x417, int flashKib = 64) {
    return QStringLiteral(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
        "chip": {"family": "stm32l0", "devId": %1, "flashKib": %2, "eepromBytes": 2048,
                 "rdp": %3}}]})")
        .arg(devId)
        .arg(flashKib)
        .arg(rdp)
        .toUtf8();
}

class Stm32Tools : public ::testing::Test {
protected:
    void SetUp() override {
        provider().settings().profile = hwio::device::ProfileRegistry::find("stm32");
        provider().scenario(scenario(0));
    }

    static void connectDevice(Harness& harness, const char* chip = "stm32l0-cat3-64k") {
        auto call =
            harness.call(QStringLiteral("connect"),
                         args({{"device", device}, {"profile", "stm32"}, {"target", chip}}));
        ASSERT_FALSE(call.isError()) << call.errorCode() << " " << call.errorDetail();
        harness.consent.requests.clear();
    }

    [[nodiscard]] std::string file(const char* name, const QByteArray& bytes) {
        const auto path = provider().root() / name;
        QFile out(QString::fromStdString(path.string()));
        EXPECT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(bytes);
        return path.string();
    }

    [[nodiscard]] SessionProvider& provider() { return m_provider; }

private:
    SessionProvider m_provider;
};

TEST_F(Stm32Tools, InfoReportsTheChip) {
    provider().scenario(scenario(1));
    Harness harness(provider(), hwio::mcp::Limits{});
    connectDevice(harness);
    auto info = harness.call(QStringLiteral("stm32_info"));
    ASSERT_FALSE(info.isError()) << info.errorCode() << " " << info.errorDetail();
    const QJsonObject json = info.structured();
    EXPECT_EQ(json.value(QStringLiteral("dev_id")).toInt(), 0x417);
    EXPECT_EQ(json.value(QStringLiteral("flash_kib")).toInt(), 64);
    EXPECT_EQ(json.value(QStringLiteral("rdp")).toInt(), 1);
    EXPECT_TRUE(json.value(QStringLiteral("matches")).toBool());
    EXPECT_EQ(json.value(QStringLiteral("chip")).toString(), QStringLiteral("stm32l0-cat3-64k"));
    EXPECT_TRUE(harness.consent.requests.empty());
}

TEST_F(Stm32Tools, FlashBinAndHex) {
    Harness harness(provider(), hwio::mcp::Limits{});
    connectDevice(harness);
    const std::string bin = file("fw.bin", QByteArray(1024, '\x5A'));
    auto flashed = harness.call(QStringLiteral("stm32_flash"),
                                args({{"path", QString::fromStdString(bin)}}), 30s, true);
    ASSERT_FALSE(flashed.isError()) << flashed.errorCode() << " " << flashed.errorDetail();
    EXPECT_EQ(flashed.structured().value(QStringLiteral("address")).toString(),
              QStringLiteral("0x08000000"));
    EXPECT_TRUE(flashed.structured().value(QStringLiteral("verified")).toBool());
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::Flash));
    // erase_all also asks for erase.
    auto erased = harness.call(QStringLiteral("stm32_flash"),
                               args({{"path", QString::fromStdString(bin)},
                                     {"address", "0x08001000"},
                                     {"erase_all", true}}),
                               30s);
    ASSERT_FALSE(erased.isError()) << erased.errorCode() << " " << erased.errorDetail();
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategory::Flash | hwio::mcp::ConsentCategory::Erase);
}

TEST_F(Stm32Tools, EepromNeedsAllowEepromBeforeTheConsent) {
    Harness harness(provider(), hwio::mcp::Limits{});
    connectDevice(harness);
    const std::string bin = file("ee.bin", QByteArray(16, '\x01'));
    auto refused =
        harness.call(QStringLiteral("stm32_flash"),
                     args({{"path", QString::fromStdString(bin)}, {"address", "0x08080000"}}), 30s);
    EXPECT_EQ(refused.errorCode(), "firmware.out_of_range");
    EXPECT_TRUE(harness.consent.requests.empty());
    auto allowed = harness.call(QStringLiteral("stm32_flash"),
                                args({{"path", QString::fromStdString(bin)},
                                      {"address", "0x08080000"},
                                      {"allow_eeprom", true}}),
                                30s);
    EXPECT_FALSE(allowed.isError()) << allowed.errorCode() << " " << allowed.errorDetail();
}

TEST_F(Stm32Tools, WrongChipAndProtectedChip) {
    provider().scenario(scenario(0, 0x447, 192));
    Harness harness(provider(), hwio::mcp::Limits{});
    connectDevice(harness);
    const std::string bin = file("fw.bin", QByteArray(64, '\x5A'));
    auto wrong = harness.call(QStringLiteral("stm32_flash"),
                              args({{"path", QString::fromStdString(bin)}}), 30s);
    EXPECT_EQ(wrong.errorCode(), "target.wrong_chip");
}

TEST_F(Stm32Tools, ProtectedChipIsReported) {
    provider().scenario(scenario(1));
    Harness harness(provider(), hwio::mcp::Limits{});
    connectDevice(harness);
    const std::string bin = file("fw.bin", QByteArray(64, '\x5A'));
    auto flashed = harness.call(QStringLiteral("stm32_flash"),
                                args({{"path", QString::fromStdString(bin)}}), 30s);
    EXPECT_EQ(flashed.errorCode(), "target.protected");
}

// ADR-0280 D9: confirm=true is required, and the user's permission (erase).
TEST_F(Stm32Tools, UnlockNeedsConfirmAndPermission) {
    provider().scenario(scenario(1));
    Harness harness(provider(), hwio::mcp::Limits{});
    connectDevice(harness);
    auto missing = harness.call(QStringLiteral("stm32_unlock"));
    EXPECT_EQ(missing.rpcError(), -32602);
    auto notTrue = harness.call(QStringLiteral("stm32_unlock"), args({{"confirm", false}}));
    EXPECT_EQ(notTrue.rpcError(), -32602);
    EXPECT_EQ(notTrue.rpcPointer(), QStringLiteral("/arguments/confirm"));
    EXPECT_TRUE(harness.consent.requests.empty());
    harness.consent.mode = hwio::mcp::test::FakeConsentBroker::Mode::Deny;
    auto denied = harness.call(QStringLiteral("stm32_unlock"), args({{"confirm", true}}), 30s);
    EXPECT_EQ(denied.errorCode(), "mcp.tool_not_allowed");
    harness.consent.mode = hwio::mcp::test::FakeConsentBroker::Mode::Allow;
    auto unlocked = harness.call(QStringLiteral("stm32_unlock"), args({{"confirm", true}}), 60s);
    ASSERT_FALSE(unlocked.isError()) << unlocked.errorCode() << " " << unlocked.errorDetail();
    EXPECT_EQ(unlocked.structured().value(QStringLiteral("rdp")).toInt(), 0);
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::Erase));
}

TEST_F(Stm32Tools, EraseAsksForErase) {
    Harness harness(provider(), hwio::mcp::Limits{});
    connectDevice(harness);
    auto erased = harness.call(QStringLiteral("stm32_erase"), {}, 30s);
    ASSERT_FALSE(erased.isError()) << erased.errorCode() << " " << erased.errorDetail();
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::Erase));
}

TEST_F(Stm32Tools, NordicTargetsAreRefused) {
    provider().settings().profile = hwio::device::ProfileRegistry::find("chester");
    provider().scenario(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
        "chip": {}, "rtt": {}}]})");
    Harness harness(provider(), hwio::mcp::Limits{});
    auto call = harness.call(QStringLiteral("connect"), args({{"device", device}}));
    ASSERT_FALSE(call.isError()) << call.errorCode();
    auto info = harness.call(QStringLiteral("stm32_info"));
    EXPECT_EQ(info.errorCode(), "usage.invalid_argument");
}

// Plan 0040: the CHESTER LoRaWAN module (target "lrw") through the STM32 tools and the generic
// flash / erase tools; the cable is on the module's connector and the application MCU is held.
TEST_F(Stm32Tools, ChesterLrwTargetIsAccepted) {
    provider().settings().profile = hwio::device::ProfileRegistry::find("chester");
    const QString connector = QString::fromStdString((provider().root() / "connector").string());
    QFile cable(connector);
    ASSERT_TRUE(cable.open(QIODevice::WriteOnly));
    cable.write("lrw");
    cable.close();
    provider().scenario(QStringLiteral(R"({"probes": [{"serialNumber": 801000001,
        "product": "Test Probe", "connectors": {"file": "%1", "initial": "app", "chips": {
          "app": {"halted": true},
          "lrw": {"family": "stm32l0", "devId": 1095, "flashKib": 192, "eepromBytes": 6144,
                  "rdp": 0, "needsHalted": "app"}}}}]})")
                            .arg(connector)
                            .toUtf8());
    Harness harness(provider(), hwio::mcp::Limits{});
    auto call =
        harness.call(QStringLiteral("connect"), args({{"device", device}, {"profile", "chester"}}));
    ASSERT_FALSE(call.isError()) << call.errorCode() << " " << call.errorDetail();
    auto info = harness.call(QStringLiteral("stm32_info"), args({{"target", "lrw"}}), 30s);
    ASSERT_FALSE(info.isError()) << info.errorCode() << " " << info.errorDetail();
    EXPECT_EQ(info.structured().value(QStringLiteral("dev_id")).toInt(), 0x447);
    EXPECT_EQ(info.structured().value(QStringLiteral("chip")).toString(),
              QStringLiteral("stm32l0-cat5-192k"));
    EXPECT_TRUE(info.structured().value(QStringLiteral("matches")).toBool());
    const std::string bin = file("lrw.bin", QByteArray(256, '\x5A'));
    auto flashed =
        harness.call(QStringLiteral("stm32_flash"),
                     args({{"target", "lrw"}, {"path", QString::fromStdString(bin)}}), 30s, true);
    ASSERT_FALSE(flashed.isError()) << flashed.errorCode() << " " << flashed.errorDetail();
    const std::string hex =
        file("lrw.hex", QByteArrayLiteral(":020000040800F2\n:0400000001020304F2\n:00000001FF\n"));
    auto generic =
        harness.call(QStringLiteral("flash"),
                     args({{"target", "lrw"}, {"path", QString::fromStdString(hex)}}), 30s, true);
    ASSERT_FALSE(generic.isError()) << generic.errorCode() << " " << generic.errorDetail();
    auto erased = harness.call(QStringLiteral("erase"), args({{"target", "lrw"}}), 30s, true);
    ASSERT_FALSE(erased.isError()) << erased.errorCode() << " " << erased.errorDetail();
    EXPECT_EQ(erased.structured().value(QStringLiteral("scope")).toString(), QStringLiteral("all"));
    auto unlocked = harness.call(QStringLiteral("stm32_unlock"),
                                 args({{"target", "lrw"}, {"confirm", true}}), 60s, true);
    EXPECT_FALSE(unlocked.isError()) << unlocked.errorCode() << " " << unlocked.errorDetail();
}

} // namespace
} // namespace hwio::mcp::tools::test
