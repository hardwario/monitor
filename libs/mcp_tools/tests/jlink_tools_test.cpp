// The device tools on real sessions (plan/features/0013 "Tests: Tools"): DeviceSessions on the
// fake J-Link library and BleSessions on the fake BLE backend (HWIO_TEST_OVERRIDES builds).
// connect / disconnect, send_command by prompt / silence / timeout with the fake's responses and
// echo, the session control tools, every target tool, flash (path, catalog id, both, UICR with
// and without allow_uicr, cancel, stall; plan 0054: the catalog page before the download, its
// name in the consent details, an unreadable page, a checksum mismatch, a cancelled lookup),
// erase, recover, internal.busy with the kind, the device closed mid-call, a BLE session
// (mcp.capability_missing), a missing optional J-Link export.

#include "hex_builder.h"
#include "http_fixture.h"
#include "hwio/device/console_line.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/limits.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/tools_context.h"
#include "session_provider.h"
#include "tools_test_support.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace hwio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;
namespace fs = std::filesystem;

constexpr const char* device = "jlink:801000001";

// py-hardwario 1.7.2 PIB vector (libs/device/tests/pib_test.cpp).
constexpr const char* pibHex =
    "babecafe027b48415244574152494f0000000000000000434845535445522d4d00000000000000004344474c53"
    "00000000000052332e3200000032313539303138323437003031323334353637383961626364656630313233343536"
    "373839616263646566003132333435360000000000000000000000a8d83379ffffffffff";

QJsonObject scenario(const QJsonObject& rttExtra = {}, const QJsonObject& chipExtra = {}) {
    QJsonObject rtt{
        {QStringLiteral("output"),
         QJsonArray{QJsonObject{{QStringLiteral("atMs"), 0},
                                {QStringLiteral("buffer"), 0},
                                {QStringLiteral("text"), QStringLiteral("booted\n")}}}},
        {QStringLiteral("responses"),
         QJsonObject{{QStringLiteral("version"), QStringLiteral("Firmware version: v3\n")}}}};
    for (auto it = rttExtra.begin(); it != rttExtra.end(); ++it) {
        rtt.insert(it.key(), it.value());
    }
    QJsonObject chip{
        {QStringLiteral("ram"), QJsonArray{QJsonObject{{QStringLiteral("address"), 0x20000000},
                                                       {QStringLiteral("size"), 4096}}}},
        {QStringLiteral("registers"),
         QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("R0")},
                                {QStringLiteral("value"), 1}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("PC")},
                                {QStringLiteral("value"), 0x1000}}}},
        {QStringLiteral("zones"),
         QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("AHB-AP")},
                                {QStringLiteral("description"), QStringLiteral("AHB memory")},
                                {QStringLiteral("virtualAddress"), 0}}}},
        {QStringLiteral("coreId"), 0x2BA01477},
        {QStringLiteral("memory"),
         QJsonArray{QJsonObject{{QStringLiteral("address"), 0x10001080},
                                {QStringLiteral("hex"), QString::fromLatin1(pibHex)}}}}};
    for (auto it = chipExtra.begin(); it != chipExtra.end(); ++it) {
        chip.insert(it.key(), it.value());
    }
    return QJsonObject{
        {QStringLiteral("probes"),
         QJsonArray{QJsonObject{{QStringLiteral("serialNumber"), 801000001},
                                {QStringLiteral("product"), QStringLiteral("Test Probe")},
                                {QStringLiteral("rtt"), rtt},
                                {QStringLiteral("chip"), chip}}}}};
}

std::string hexFile(const fs::path& path, std::vector<hwio::firmware::Segment> segments) {
    auto image = hwio::firmware::FirmwareImage::fromSegments(std::move(segments));
    const std::string text = hwio::firmware::test::toHex(*image);
    QFile file(QString::fromStdString(path.string()));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text.data(), static_cast<qint64>(text.size()));
    }
    return path.string();
}

class JLinkTools : public ::testing::Test {
protected:
    void SetUp() override { provider().scenario(QJsonDocument(scenario()).toJson()); }

    static hwio::mcp::Limits limits() {
        hwio::mcp::Limits shrunk;
        shrunk.tools.sendCommandSilence = 150ms;
        return shrunk;
    }

    // connect + wait until the console is connected.
    static void connectDevice(Harness& harness) {
        auto call = harness.call(QStringLiteral("connect"), args({{"device", device}}));
        ASSERT_FALSE(call.isError()) << call.errorCode() << " " << call.errorDetail();
        auto connected =
            harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}));
        ASSERT_TRUE(connected.structured().value(QStringLiteral("connected")).toBool());
        harness.consent.requests.clear(); // the connect request
    }

    [[nodiscard]] std::string imagePath(std::uint32_t address, std::size_t size,
                                        const char* name = "app.hex") {
        return hexFile(provider().root() / name,
                       {hwio::firmware::Segment{.address = address,
                                                .data = hwio::firmware::test::filled(size, 0x5A)}});
    }

    [[nodiscard]] SessionProvider& provider() { return m_provider; }

private:
    SessionProvider m_provider;
};

TEST_F(JLinkTools, ConnectStatusAndDisconnect) {
    provider().settings().available.push_back(DeviceEntry{.id = "jlink:801000009",
                                                          .kind = "jlink",
                                                          .displayName = "J-Link 801000009",
                                                          .profileId = {},
                                                          .state = DeviceEntry::State::Available,
                                                          .owner = std::nullopt,
                                                          .connection = std::nullopt,
                                                          .openedByMcp = false});
    Harness harness(provider(), limits());
    auto connect = harness.call(QStringLiteral("connect"), args({{"device", device}}));
    ASSERT_FALSE(connect.isError()) << connect.errorCode() << " " << connect.errorDetail();
    EXPECT_FALSE(connect.structured().value(QStringLiteral("already_open")).toBool());
    EXPECT_EQ(connect.structured()
                  .value(QStringLiteral("connection"))
                  .toObject()
                  .value(QStringLiteral("state"))
                  .toString(),
              QStringLiteral("connected"));
    auto again = harness.call(QStringLiteral("connect"), args({{"device", device}}));
    EXPECT_TRUE(again.structured().value(QStringLiteral("already_open")).toBool());

    auto list = harness.call(QStringLiteral("list_devices"), args({{"refresh", true}}));
    EXPECT_EQ(provider().refreshes(), 1);
    EXPECT_EQ(list.structured().value(QStringLiteral("devices")).toArray().size(), 2);

    auto disconnect = harness.call(QStringLiteral("disconnect"));
    EXPECT_TRUE(disconnect.structured().value(QStringLiteral("closed")).toBool());
    EXPECT_EQ(harness.call(QStringLiteral("status"))
                  .structured()
                  .value(QStringLiteral("devices"))
                  .toArray()
                  .size(),
              0);
}

TEST_F(JLinkTools, ConnectFailureClosesTheSession) {
    QJsonObject failing = scenario();
    QJsonArray probes = failing.value(QStringLiteral("probes")).toArray();
    QJsonObject probe = probes[0].toObject();
    probe.insert(QStringLiteral("openError"), QStringLiteral("USB error"));
    probes[0] = probe;
    failing.insert(QStringLiteral("probes"), probes);
    provider().scenario(QJsonDocument(failing).toJson());
    Harness harness(provider());
    auto call = harness.call(QStringLiteral("connect"), args({{"device", device}}));
    EXPECT_TRUE(call.isError());
    EXPECT_TRUE(runUntil([&] { return provider().list().empty(); }));
}

TEST_F(JLinkTools, SendCommandEndsAtTheRttPromptAndDropsTheShellEcho) {
    provider().settings().profile = &promptProfile();
    provider().scenario(
        QJsonDocument(scenario(QJsonObject{
                          {QStringLiteral("echo"), true},
                          {QStringLiteral("responses"),
                           QJsonObject{{QStringLiteral("version"),
                                        QStringLiteral("Firmware version: v3\nuart:~$ ")}}}}))
            .toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto call = harness.call(QStringLiteral("send_command"), args({{"command", "version"}}));
    ASSERT_FALSE(call.isError()) << call.errorCode();
    EXPECT_EQ(call.structured().value(QStringLiteral("end")).toString(), QStringLiteral("prompt"));
    const QJsonArray output = call.structured().value(QStringLiteral("output")).toArray();
    ASSERT_EQ(output.size(), 1);
    EXPECT_EQ(output[0].toObject().value(QStringLiteral("text")).toString(),
              QStringLiteral("Firmware version: v3"));
}

TEST_F(JLinkTools, SendCommandEndsBySilenceAndTimeout) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto silence = harness.call(QStringLiteral("send_command"), args({{"command", "version"}}));
    EXPECT_EQ(silence.structured().value(QStringLiteral("end")).toString(),
              QStringLiteral("silence"));
    EXPECT_EQ(silence.structured().value(QStringLiteral("output")).toArray().size(), 1);
    auto timeout = harness.call(QStringLiteral("send_command"),
                                args({{"command", "unknown"}, {"timeout", 0.5}}));
    EXPECT_EQ(timeout.structured().value(QStringLiteral("end")).toString(),
              QStringLiteral("timeout"));
}

TEST_F(JLinkTools, SessionControlTools) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto stop = harness.call(QStringLiteral("stop"));
    ASSERT_FALSE(stop.isError()) << stop.errorCode();
    EXPECT_TRUE(stop.structured().value(QStringLiteral("was_reading")).toBool());
    ASSERT_TRUE(runUntil([&] {
        return harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 1}}))
                   .structured()
                   .value(QStringLiteral("state"))
                   .toString() == QStringLiteral("stopped");
    }));
    auto start = harness.call(QStringLiteral("start"));
    EXPECT_FALSE(start.structured().value(QStringLiteral("already_reading")).toBool());
    EXPECT_TRUE(harness.call(QStringLiteral("wait_for_connection"))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
    auto reconnect = harness.call(QStringLiteral("reconnect"), args({{"rtt_address", "auto"}}));
    EXPECT_FALSE(reconnect.isError()) << reconnect.errorCode();
    auto bad = harness.call(QStringLiteral("reconnect"), args({{"rtt_address", "zzz"}}));
    EXPECT_EQ(bad.rpcError(), -32602);
    auto release = harness.call(QStringLiteral("release_probe"));
    EXPECT_TRUE(release.structured().value(QStringLiteral("released")).toBool());
    auto reacquire = harness.call(QStringLiteral("reacquire_probe"));
    EXPECT_FALSE(reacquire.isError()) << reacquire.errorCode();
    // ADR-0375: it waits for the console.
    EXPECT_EQ(reacquire.structured()
                  .value(QStringLiteral("connection"))
                  .toObject()
                  .value(QStringLiteral("state"))
                  .toString(),
              QStringLiteral("connected"));
    EXPECT_TRUE(reacquire.structured().value(QStringLiteral("waited")).isDouble());
    EXPECT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
    // A marker for every control tool (ADR-0105), none for the reads.
    EXPECT_EQ(provider().record().markers.size(), 5U);
}

// ADR-0375: reacquire_probe waits (bounded) for the console; a slow attach (every J-Link
// connect takes 0.5 s) is waited for, a wait shorter than the attach returns the current state
// without an error, and a cancelled wait leaves the reacquire running.
class ReacquireProbe : public JLinkTools {
protected:
    void SetUp() override {
        QJsonObject slow = scenario();
        QJsonArray probes = slow.value(QStringLiteral("probes")).toArray();
        QJsonObject probe = probes.at(0).toObject();
        probe.insert(QStringLiteral("connectDelayMs"), 500);
        probes.replace(0, probe);
        slow.insert(QStringLiteral("probes"), probes);
        provider().scenario(QJsonDocument(slow).toJson());
    }

    // release_probe, then wait until the session reports stopped (the release is queued).
    static void release(Harness& harness) {
        ASSERT_TRUE(harness.call(QStringLiteral("release_probe"))
                        .structured()
                        .value(QStringLiteral("released"))
                        .toBool());
        ASSERT_TRUE(runUntil([&] {
            return harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 1}}))
                       .structured()
                       .value(QStringLiteral("state"))
                       .toString() == QStringLiteral("stopped");
        }));
    }

    static QString stateOf(const QJsonObject& result) {
        return result.value(QStringLiteral("connection"))
            .toObject()
            .value(QStringLiteral("state"))
            .toString();
    }
};

TEST_F(ReacquireProbe, WaitsForTheConsole) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    release(harness);
    auto reacquire = harness.call(QStringLiteral("reacquire_probe"));
    ASSERT_FALSE(reacquire.isError()) << reacquire.errorCode();
    EXPECT_EQ(stateOf(reacquire.structured()), QStringLiteral("connected"));
    const double waited = reacquire.structured().value(QStringLiteral("waited")).toDouble();
    EXPECT_GE(waited, 0.4);
    EXPECT_LT(waited, 10.0);
}

TEST_F(ReacquireProbe, ShortWaitReturnsTheCurrentState) {
    hwio::mcp::Limits shrunk = limits();
    shrunk.tools.reacquireWait = 100ms; // shorter than the 0.5 s connect
    Harness harness(provider(), shrunk);
    connectDevice(harness);
    release(harness);
    auto reacquire = harness.call(QStringLiteral("reacquire_probe"));
    ASSERT_FALSE(reacquire.isError()) << reacquire.errorCode();
    EXPECT_NE(stateOf(reacquire.structured()), QStringLiteral("connected"));
    EXPECT_LT(reacquire.structured().value(QStringLiteral("waited")).toDouble(), 0.45);
    // The reacquire goes on; wait_for_connection sees it connect.
    EXPECT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
}

TEST_F(ReacquireProbe, CancelStopsTheWaitOnly) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    release(harness);
    auto reacquire = harness.start(QStringLiteral("reacquire_probe"));
    harness.cancel(harness.lastId());
    runFor(50ms);
    EXPECT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
}

TEST_F(JLinkTools, HaltGoStatusRegistersZones) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto status = harness.call(QStringLiteral("target_status"));
    ASSERT_FALSE(status.isError()) << status.errorCode() << " " << status.errorDetail();
    EXPECT_FALSE(status.structured().value(QStringLiteral("halted")).toBool());
    EXPECT_EQ(status.structured().value(QStringLiteral("core_id")).toString(),
              QStringLiteral("0x2BA01477"));
    auto notHalted = harness.call(QStringLiteral("read_registers"));
    EXPECT_EQ(notHalted.errorCode(), "target.not_halted");
    auto halt = harness.call(QStringLiteral("halt"));
    EXPECT_TRUE(halt.structured().value(QStringLiteral("halted")).toBool());
    auto registers = harness.call(QStringLiteral("read_registers"));
    ASSERT_FALSE(registers.isError()) << registers.errorCode();
    EXPECT_EQ(registers.structured().value(QStringLiteral("registers")).toObject(),
              (QJsonObject{{QStringLiteral("R0"), QStringLiteral("0x00000001")},
                           {QStringLiteral("PC"), QStringLiteral("0x00001000")}}));
    auto go = harness.call(QStringLiteral("go"));
    EXPECT_FALSE(go.structured().value(QStringLiteral("halted")).toBool());
    auto zones = harness.call(QStringLiteral("memory_zones"));
    const QJsonArray list = zones.structured().value(QStringLiteral("zones")).toArray();
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].toObject().value(QStringLiteral("virtual_address")).toString(),
              QStringLiteral("0x00000000"));
    auto badTarget = harness.call(QStringLiteral("halt"), args({{"target", "modem"}}));
    EXPECT_EQ(badTarget.errorCode(), "usage.invalid_argument");
    // halt and go wrote markers; the console stays connected (ADR-0157).
    EXPECT_EQ(provider().record().markers.size(), 2U);
}

TEST_F(JLinkTools, WriteAndReadMemory) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto write = harness.call(QStringLiteral("write_memory"),
                              args({{"address", "0x20000010"}, {"data_hex", "deadbeef"}}));
    ASSERT_FALSE(write.isError()) << write.errorCode() << " " << write.errorDetail();
    EXPECT_EQ(write.structured().value(QStringLiteral("units_written")).toInteger(), 4);
    EXPECT_EQ(write.structured().value(QStringLiteral("address")).toString(),
              QStringLiteral("0x20000010"));
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    EXPECT_EQ(harness.consent.requests[0].categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::MemoryWrite));
    EXPECT_EQ(harness.consent.requests[0].details.value(QStringLiteral("range")).toString(),
              QStringLiteral("0x20000010-0x20000013"));

    auto read = harness.call(QStringLiteral("read_memory"),
                             args({{"address", "0x20000010"}, {"length", 8}, {"width", 32}}));
    ASSERT_FALSE(read.isError()) << read.errorCode() << " " << read.errorDetail();
    EXPECT_EQ(read.structured().value(QStringLiteral("hex")).toString(),
              QStringLiteral("deadbeef00000000"));
    EXPECT_EQ(
        read.structured().value(QStringLiteral("dump")).toArray().at(0).toString(),
        QStringLiteral("0x20000010: de ad be ef 00 00 00 00                         |........|"));

    // Review F4: the range is checked before the consent request (no request is recorded);
    // ADR-0164: the System Control Space (VTOR, STIR) and the FPB are denied too.
    const std::size_t requestsBefore = harness.consent.requests.size();
    for (const char* address : {"0x00001000", "0xE000ED08", "0xE000EF00", "0xE0002000"}) {
        auto outOfRange = harness.call(QStringLiteral("write_memory"),
                                       args({{"address", address}, {"data_hex", "00000000"}}));
        EXPECT_EQ(outOfRange.errorCode(), "target.address_out_of_range") << address;
        EXPECT_TRUE(outOfRange.isError()) << address;
    }
    EXPECT_EQ(harness.consent.requests.size(), requestsBefore);
    auto both = harness.call(QStringLiteral("write_memory"),
                             args({{"address", "0x20000000"}, {"data_hex", "00"}, {"path", "/x"}}));
    EXPECT_EQ(both.rpcError(), -32602);
    auto neither = harness.call(QStringLiteral("write_memory"), args({{"address", "0x20000000"}}));
    EXPECT_EQ(neither.rpcError(), -32602);
    auto badHex = harness.call(QStringLiteral("write_memory"),
                               args({{"address", "0x20000000"}, {"data_hex", "abc"}}));
    EXPECT_EQ(badHex.rpcError(), -32602);
    auto badWidth = harness.call(QStringLiteral("read_memory"),
                                 args({{"address", "0x20000000"}, {"width", 7}}));
    EXPECT_EQ(badWidth.rpcError(), -32602);
    auto badAddress = harness.call(QStringLiteral("read_memory"), args({{"address", "0xZZ"}}));
    EXPECT_EQ(badAddress.rpcError(), -32602);
    auto tooLong =
        harness.call(QStringLiteral("read_memory"), args({{"address", "0x0"}, {"length", 65537}}));
    EXPECT_EQ(tooLong.rpcError(), -32602);

    harness.consent.mode = hwio::mcp::test::FakeConsentBroker::Mode::Deny;
    auto denied = harness.call(QStringLiteral("write_memory"),
                               args({{"address", "0x20000000"}, {"data_hex", "00"}}));
    EXPECT_EQ(denied.errorCode(), "mcp.tool_not_allowed");
}

TEST_F(JLinkTools, ReadMemoryToFile) {
    ToolsOptions options;
    options.memoryDumpDirectory = provider().root() / "dumps";
    ASSERT_TRUE(fs::create_directories(options.memoryDumpDirectory)); // --dump-dir must exist
    Harness harness(provider(), limits(), nullptr, options);
    connectDevice(harness);
    auto dump =
        harness.call(QStringLiteral("read_memory"),
                     args({{"address", "0x0"}, {"length", 70000}, {"to_file", "flash.bin"}}));
    ASSERT_FALSE(dump.isError()) << dump.errorCode() << " " << dump.errorDetail();
    const QJsonObject result = dump.structured();
    const QString file = result.value(QStringLiteral("file")).toString();
    EXPECT_EQ(fs::path(file.toStdString()), provider().root() / "dumps" / "flash.bin");
    EXPECT_EQ(result.value(QStringLiteral("length")).toInteger(), 70000);
    QFile bytes(file);
    ASSERT_TRUE(bytes.open(QIODevice::ReadOnly));
    const QByteArray content = bytes.readAll();
    EXPECT_EQ(content.size(), 70000);
    EXPECT_EQ(
        result.value(QStringLiteral("sha256")).toString(),
        QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()));
    EXPECT_EQ(result.value(QStringLiteral("preview")).toArray().size(), 4);

    auto exists =
        harness.call(QStringLiteral("read_memory"),
                     args({{"address", "0x0"}, {"length", 16}, {"to_file", "flash.bin"}}));
    EXPECT_EQ(exists.errorCode(), "io.file_exists");
    EXPECT_TRUE(QFile::exists(file)); // never overwritten, never removed
}

// ADR-0182 (review H-1): to_file is a plain name in the dump folder; absolute, nested, dot and
// over-long names are refused with mcp.to_file_not_allowed before the device is touched (no
// consent prompt, no file, no folder).
TEST_F(JLinkTools, ReadMemoryToFileRefusesPaths) {
    ToolsOptions options;
    options.memoryDumpDirectory = provider().root() / "dumps";
    ASSERT_TRUE(fs::create_directories(options.memoryDumpDirectory));
    Harness harness(provider(), limits(), nullptr, options);
    connectDevice(harness);
    harness.consent.mode = hwio::mcp::test::FakeConsentBroker::Mode::Deny;
    const fs::path absolute = provider().root() / "authorized_keys";
    const std::string longName(129, 'a');
    for (const std::string& name :
         {absolute.string(), std::string("sub/x.bin"), std::string("../x.bin"),
          std::string(".bashrc"), std::string(".."), std::string("."), std::string("a\\b.bin"),
          std::string("C:foo.bin"), std::string("dump.bin:stream"), longName}) {
        auto refused = harness.call(
            QStringLiteral("read_memory"),
            args({{"address", "0x0"}, {"length", 16}, {"to_file", QString::fromStdString(name)}}));
        EXPECT_EQ(refused.errorCode(), "mcp.to_file_not_allowed") << name;
        EXPECT_NE(refused.errorMessage().find(options.memoryDumpDirectory.string()),
                  std::string::npos)
            << refused.errorMessage();
    }
    EXPECT_FALSE(fs::exists(absolute));
    EXPECT_FALSE(fs::exists(provider().root() / "x.bin"));
    EXPECT_TRUE(harness.consent.requests.empty());
    // 128 characters is still a plain name.
    auto longest = harness.call(
        QStringLiteral("read_memory"),
        args({{"address", "0x0"}, {"length", 16}, {"to_file", QString(128, QLatin1Char('b'))}}));
    EXPECT_FALSE(longest.isError()) << longest.errorCode();
    EXPECT_TRUE(fs::exists(options.memoryDumpDirectory / std::string(128, 'b')));
}

// ADR-0182: without --dump-dir the default cache folder is created on first use.
TEST_F(JLinkTools, ReadMemoryToFileDefaultFolderIsCreated) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const fs::path directory = harness.tools->memoryDumpDirectory();
    std::error_code ec;
    fs::remove_all(directory, ec);
    auto dump =
        harness.call(QStringLiteral("read_memory"),
                     args({{"address", "0x0"}, {"length", 16}, {"to_file", "default.bin"}}));
    ASSERT_FALSE(dump.isError()) << dump.errorCode() << " " << dump.errorDetail();
    EXPECT_TRUE(fs::exists(directory / "default.bin"));
    fs::remove_all(directory, ec);
}

// Review R15 / ADR-0182: a --dump-dir folder that does not exist is never created; Windows
// device names are refused by the validate hook.
TEST_F(JLinkTools, ReadMemoryToFilePathRules) {
    ToolsOptions options;
    options.memoryDumpDirectory = provider().root() / "cache" / "mcp"; // does not exist
    Harness harness(provider(), limits(), nullptr, options);
    connectDevice(harness);
    auto noFolder = harness.call(QStringLiteral("read_memory"),
                                 args({{"address", "0x0"}, {"length", 16}, {"to_file", "d.bin"}}));
    EXPECT_EQ(noFolder.errorCode(), "io.write_failed");
    EXPECT_NE(noFolder.errorDetail().find(options.memoryDumpDirectory.string()), std::string::npos)
        << noFolder.errorDetail();
    EXPECT_FALSE(fs::exists(options.memoryDumpDirectory));
    ASSERT_TRUE(fs::create_directories(options.memoryDumpDirectory));
    // Review F5: Windows device names (any case, with an extension) and trailing dots / spaces.
    for (const char* name :
         {"CONIN$",    "conout$.txt", "COM0",  "lpt0.bin", "COM\u00b9", "lpt\u00b2.bin",
          "COM\u00b3", "NUL",         "nul",   "nul.txt",  "Con",       "prn.bin",
          "AUX",       "COM1.bin",    "com9",  "LPT1",     "lpt5.dump", "nul .txt",
          "dump.",     "dump.bin ",   "dump. "}) {
        auto refused = harness.call(QStringLiteral("read_memory"),
                                    args({{"address", "0x0"}, {"to_file", name}}));
        EXPECT_EQ(refused.rpcError(), -32602) << name;
        EXPECT_EQ(refused.rpcPointer(), QStringLiteral("/arguments/to_file")) << name;
    }
    // Not device names: no digit / a letter / longer stems.
    for (const char* name :
         {"COM.bin", "COMX.bin", "LPT10.bin", "console.bin", "nul_dump.bin", "auxiliary"}) {
        auto accepted = harness.call(QStringLiteral("read_memory"),
                                     args({{"address", "0x0"}, {"length", 16}, {"to_file", name}}));
        EXPECT_FALSE(accepted.isError()) << name << " " << accepted.errorCode();
    }
    auto bare = harness.call(QStringLiteral("read_memory"),
                             args({{"address", "0x0"}, {"length", 16}, {"to_file", "d.bin"}}));
    ASSERT_FALSE(bare.isError()) << bare.errorCode() << " " << bare.errorDetail();
    EXPECT_TRUE(fs::exists(provider().root() / "cache" / "mcp" / "d.bin"));
}

// Review R13: a path with "%1" appears unchanged in the message (no chained arg()).
TEST_F(JLinkTools, PathWithPercentPlaceholderInMessages) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const fs::path big = provider().root() / "big%1%2.bin";
    {
        QFile file(QString::fromStdString(big.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray((64 * 1024) + 1, 'x'));
    }
    auto tooBig = harness.call(
        QStringLiteral("write_memory"),
        args({{"address", "0x20000000"}, {"path", QString::fromStdString(big.string())}}));
    EXPECT_EQ(tooBig.errorCode(), "usage.invalid_argument");
    EXPECT_EQ(tooBig.error().value(QStringLiteral("message")).toString(),
              QStringLiteral("The file %1 is larger than 65536 bytes.")
                  .arg(QString::fromStdString(big.string())));
}

// ADR-0162 / review R11 (TSan): the Intel HEX parse of `flash` and the classification of
// `flash_lte` run on the server's offloader; the call goes on on the owner thread afterwards.
TEST_F(JLinkTools, FlashParseAndLteClassificationAreOffloaded) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const QString path = QString::fromStdString(imagePath(0x0, 8192));
    auto flash = harness.start(QStringLiteral("flash"), args({{"path", path}}));
    // Submitted from the call's constructor; its result is only delivered by the event loop.
    EXPECT_EQ(harness.server->offloader().running(), 1U);
    EXPECT_TRUE(harness.consent.requests.empty()); // consent follows the parse
    ASSERT_TRUE(runUntil([&] { return flash.done(); }, 30s));
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    EXPECT_EQ(harness.server->offloader().running(), 0U);

    auto lte = harness.start(QStringLiteral("flash_lte"), args({{"path", path}}));
    EXPECT_EQ(harness.server->offloader().running(), 1U);
    ASSERT_TRUE(runUntil([&] { return lte.done(); }, 30s));
    // The fake chip is an nRF52, so the LTE target cannot be programmed - but the input was
    // classified (an application .hex) and the operation was asked for after consent.
    ASSERT_FALSE(harness.consent.requests.empty());
    EXPECT_EQ(harness.consent.requests.back().details.value(QStringLiteral("kind")).toString(),
              QStringLiteral("hex"));

    // A parse error comes back as the tool error.
    const fs::path broken = provider().root() / "broken.hex";
    {
        QFile file(QString::fromStdString(broken.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(":zz\n");
    }
    auto bad = harness.call(QStringLiteral("flash"),
                            args({{"path", QString::fromStdString(broken.string())}}), 30s);
    EXPECT_EQ(bad.errorCode(), "firmware.hex_parse_error");
}

// ADR-0162: a full offloader (running and queued) answers mcp.too_many_requests; a cancelled
// call drops its job.
TEST_F(JLinkTools, OffloaderFullIsTooManyRequests) {
    hwio::mcp::Limits shrunk = limits();
    shrunk.maxOffloadRunning = 1;
    shrunk.maxOffloadQueued = 0;
    Harness harness(provider(), shrunk);
    connectDevice(harness);
    const QString path = QString::fromStdString(imagePath(0x0, 4096));
    auto first = harness.start(QStringLiteral("flash"), args({{"path", path}}));
    const int firstId = harness.lastId();
    auto second = harness.call(QStringLiteral("flash"), args({{"path", path}}));
    EXPECT_EQ(second.errorCode(), "mcp.too_many_requests");
    harness.cancel(firstId); // cancelled while parsing: no consent, no flash
    runFor(300ms);
    EXPECT_TRUE(harness.consent.requests.empty());
    EXPECT_TRUE(provider().record().busy.empty());
    EXPECT_EQ(harness.server->offloader().running(), 0U);
    (void)first;
}

// Review R14: a cancel that arrives after the operation already succeeded reports the success.
TEST_F(JLinkTools, CancelRacingCompletionReportsTheSuccess) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("readDelayMs"), 600}})).toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto read = harness.start(QStringLiteral("read_memory"),
                              args({{"address", "0x20000000"}, {"length", 4}}));
    const int id = harness.lastId();
    runFor(200ms); // the read runs on the worker (one ReadMemEx, not interruptible)
    harness.cancel(id);
    ASSERT_TRUE(runUntil(
        [&] {
            return !harness.activity.entries.empty() &&
                   harness.activity.entries.back().tool == "read_memory";
        },
        10s));
    EXPECT_EQ(harness.activity.entries.back().result, "ok");
    (void)read;
}

TEST_F(JLinkTools, PibAndUicr) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto pib = harness.call(QStringLiteral("read_pib"));
    ASSERT_FALSE(pib.isError()) << pib.errorCode() << " " << pib.errorDetail();
    EXPECT_EQ(pib.structured().value(QStringLiteral("signature")).toString(),
              QStringLiteral("0xbabecafe"));
    EXPECT_EQ(pib.structured().value(QStringLiteral("product_name")).toString(),
              QStringLiteral("CHESTER-M"));
    auto uicr = harness.call(QStringLiteral("read_uicr"));
    EXPECT_EQ(uicr.structured().value(QStringLiteral("bytes")).toInteger(), 128);
    EXPECT_TRUE(uicr.structured()
                    .value(QStringLiteral("hex"))
                    .toString()
                    .startsWith(QStringLiteral("babecafe")));
}

TEST_F(JLinkTools, ResetWritesMarkersAndCursors) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto reset = harness.call(QStringLiteral("reset"));
    ASSERT_FALSE(reset.isError()) << reset.errorCode();
    EXPECT_FALSE(reset.structured().value(QStringLiteral("halted")).toBool());
    const qint64 cursor = reset.structured().value(QStringLiteral("log_cursor_end")).toInteger();
    EXPECT_GT(cursor, 0);
    EXPECT_LE(static_cast<std::uint64_t>(cursor), provider().logOf(device)->lastSeq());
    ASSERT_EQ(provider().record().markers.size(), 2U);
    EXPECT_TRUE(provider().record().markers[0].second.contains(QStringLiteral("reset app")));
    EXPECT_TRUE(provider().record().markers[1].second.contains(QStringLiteral("done in")));
    EXPECT_EQ(provider().record().busy.size(), 1U);
    EXPECT_EQ(provider().record().endBusy, 1);
}

TEST_F(JLinkTools, FlashFromAPath) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const std::string path = imagePath(0x0, 8192);
    auto flash = harness.call(QStringLiteral("flash"),
                              args({{"path", QString::fromStdString(path)}}), 30s, true);
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    const QJsonObject result = flash.structured();
    EXPECT_EQ(result.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
    EXPECT_EQ(result.value(QStringLiteral("target")).toString(), QStringLiteral("app"));
    EXPECT_EQ(result.value(QStringLiteral("file")).toString(), QString::fromStdString(path));
    EXPECT_EQ(result.value(QStringLiteral("chip")).toString(), QStringLiteral("nRF52840"));
    EXPECT_TRUE(result.contains(QStringLiteral("log_cursor_end")));
    EXPECT_GT(flash.progressCount(), 0U);
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    EXPECT_EQ(harness.consent.requests[0].categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::Flash));
    EXPECT_EQ(harness.consent.requests[0].details.value(QStringLiteral("size")).toInteger(), 8192);
    ASSERT_EQ(provider().record().markers.size(), 2U);
    EXPECT_TRUE(provider().record().markers[0].second.contains(QStringLiteral("flash app.hex")));
    EXPECT_EQ(provider().record().busy.at(0).second, QStringLiteral("AI: Flashing…"));
    // The ConsoleLog is not cleared (ADR-0099 note): the boot line is still there.
    EXPECT_GT(provider().logOf(device)->size(hwio::device::Channel::Shell), 0U);
    EXPECT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
}

// Plan 0050 (ADR-0376, ADR-0377): with the fake loader's V9.84 messages, the second flash of the
// same image says nothing was programmed; the result says how the flash was allowed.
TEST_F(JLinkTools, FlashReportsUnchangedContents) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("downloadReport"), true}}))
            .toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    const QString path = QString::fromStdString(imagePath(0x0, 8192));
    auto first = harness.call(QStringLiteral("flash"), args({{"path", path}}), 30s, true);
    ASSERT_FALSE(first.isError()) << first.errorCode() << " " << first.errorDetail();
    EXPECT_FALSE(first.structured().value(QStringLiteral("unchanged")).toBool(true));
    EXPECT_EQ(first.structured().value(QStringLiteral("affected_bytes")).toInteger(), 8192);
    EXPECT_EQ(first.structured().value(QStringLiteral("consent")).toObject(),
              (QJsonObject{{QStringLiteral("flash"), QStringLiteral("prompted")}}));
    ASSERT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
    auto second = harness.call(QStringLiteral("flash"), args({{"path", path}}), 30s, true);
    ASSERT_FALSE(second.isError()) << second.errorCode() << " " << second.errorDetail();
    EXPECT_TRUE(second.structured().value(QStringLiteral("unchanged")).toBool());
    EXPECT_EQ(second.structured().value(QStringLiteral("affected_bytes")).toInteger(), 0);
    ASSERT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
    // An erase of the code flash reports too.
    auto erase = harness.call(QStringLiteral("erase"), args({{"scope", "code"}}), 30s, true);
    ASSERT_FALSE(erase.isError()) << erase.errorCode() << " " << erase.errorDetail();
    EXPECT_FALSE(erase.structured().value(QStringLiteral("unchanged")).toBool(true));
    EXPECT_GT(erase.structured().value(QStringLiteral("affected_bytes")).toInteger(), 0);
}

// Without the loader's messages (another J-Link wording) the fields are left out.
TEST_F(JLinkTools, FlashWithoutLoaderMessagesOmitsTheFields) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto flash =
        harness.call(QStringLiteral("flash"),
                     args({{"path", QString::fromStdString(imagePath(0x0, 4096))}}), 30s, true);
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    EXPECT_FALSE(flash.structured().contains(QStringLiteral("unchanged")));
    EXPECT_FALSE(flash.structured().contains(QStringLiteral("affected_bytes")));
}

TEST_F(JLinkTools, FlashArgumentRules) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto both = harness.call(
        QStringLiteral("flash"),
        args({{"path", "/a.hex"}, {"catalog_id", "0123456789abcdef0123456789abcdef"}}));
    EXPECT_EQ(both.rpcError(), -32602);
    auto neither = harness.call(QStringLiteral("flash"));
    EXPECT_EQ(neither.rpcError(), -32602);
    // Review Q4: there is no `verify` argument (images are always verified); the schema rejects
    // unknown arguments.
    auto noVerify =
        harness.call(QStringLiteral("flash"), args({{"path", "/a.hex"}, {"verify", false}}));
    EXPECT_EQ(noVerify.rpcError(), -32602);
    EXPECT_EQ(noVerify.rpcPointer(), QStringLiteral("/arguments/verify"));
    const QJsonArray tools = harness.rpc(QStringLiteral("tools/list"))
                                 .value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("tools"))
                                 .toArray();
    bool listed = false;
    for (const auto& tool : tools) {
        if (tool.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("flash")) {
            listed = true;
            EXPECT_FALSE(tool.toObject()
                             .value(QStringLiteral("inputSchema"))
                             .toObject()
                             .value(QStringLiteral("properties"))
                             .toObject()
                             .contains(QStringLiteral("verify")));
        }
    }
    EXPECT_TRUE(listed);
    auto relative = harness.call(QStringLiteral("flash"), args({{"path", "a.hex"}}));
    EXPECT_EQ(relative.errorCode(), "usage.invalid_argument");
    // Absolute on every OS ("/nonexistent" has no drive letter on Windows, so it is relative).
    const fs::path absent = provider().root() / "nonexistent" / "a.hex";
    auto missing = harness.call(QStringLiteral("flash"),
                                args({{"path", QString::fromStdString(absent.string())}}));
    EXPECT_EQ(missing.errorCode(), "io.file_not_found");
    auto noCatalog = harness.call(QStringLiteral("flash"),
                                  args({{"catalog_id", "0123456789abcdef0123456789abcdef"}}));
    EXPECT_EQ(noCatalog.errorCode(), "usage.invalid_argument");
    EXPECT_TRUE(provider().record().markers.empty());
}

TEST_F(JLinkTools, FlashUicrImageNeedsAllowUicr) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const std::string path = hexFile(
        provider().root() / "uicr.hex",
        {hwio::firmware::Segment{.address = 0x0, .data = hwio::firmware::test::filled(4096, 0x11)},
         hwio::firmware::Segment{.address = 0x10001000,
                                 .data = hwio::firmware::test::filled(4, 0x22)}});
    auto refused =
        harness.call(QStringLiteral("flash"), args({{"path", QString::fromStdString(path)}}));
    EXPECT_EQ(refused.errorCode(), "firmware.uicr_in_image");
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    EXPECT_EQ(harness.consent.requests[0].categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::Flash));

    auto allowed =
        harness.call(QStringLiteral("flash"),
                     args({{"path", QString::fromStdString(path)}, {"allow_uicr", true}}), 30s);
    EXPECT_FALSE(allowed.isError()) << allowed.errorCode() << " " << allowed.errorDetail();
    ASSERT_EQ(harness.consent.requests.size(), 2U);
    EXPECT_EQ(harness.consent.requests[1].categories,
              hwio::mcp::ConsentCategory::Flash | hwio::mcp::ConsentCategory::Uicr);
}

TEST_F(JLinkTools, FlashFromTheCatalog) {
    hwio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    const std::string id = "0123456789abcdef0123456789abcdef";
    auto image = hwio::firmware::FirmwareImage::fromSegments(
        {hwio::firmware::Segment{.address = 0, .data = hwio::firmware::test::filled(4096, 0x33)}});
    http.set(QStringLiteral("/chester/%1/hex").arg(QString::fromStdString(id)),
             hwio::firmware::test::HttpFixture::Response::ok(
                 QByteArray::fromStdString(hwio::firmware::test::toHex(*image))));
    hwio::firmware::FirmwareCache cache(provider().root() / "cache");
    hwio::firmware::FirmwareCatalog catalog(
        hwio::firmware::CatalogOptions{
            .baseUrl = QUrl(http.baseUrl()), .transferTimeout = 5s, .maxBodyBytes = 1024ULL * 1024},
        &cache);
    Harness harness(provider(), limits(), &catalog);
    connectDevice(harness);
    auto flash = harness.call(QStringLiteral("flash"),
                              args({{"catalog_id", QString::fromStdString(id)}}), 30s);
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    EXPECT_EQ(flash.structured().value(QStringLiteral("catalog_id")).toString(),
              QString::fromStdString(id));
    EXPECT_EQ(harness.consent.requests.at(0).details.value(QStringLiteral("catalog_id")).toString(),
              QString::fromStdString(id));
    // Plan 0054 D-5: no catalog page (HTTP 404 here): the flash goes on, the prompt shows the ID.
    EXPECT_EQ(http.requests(QStringLiteral("/chester/%1").arg(QString::fromStdString(id))), 1);
    EXPECT_FALSE(harness.consent.requests.at(0).details.contains(QStringLiteral("firmware_name")));
    EXPECT_FALSE(
        harness.consent.requests.at(0).details.contains(QStringLiteral("firmware_version")));
}

// ---- plan 0054 D-5 (ADR-0389): the catalog page before the download, like the CLI -----------

class CatalogFlash : public JLinkTools {
protected:
    static constexpr const char* id = "0123456789abcdef0123456789abcdef";

    CatalogFlash()
        : m_hex(QByteArray::fromStdString(hwio::firmware::test::toHex(
              *hwio::firmware::FirmwareImage::fromSegments({hwio::firmware::Segment{
                  .address = 0, .data = hwio::firmware::test::filled(4096, 0x44)}})))) {}

    static QString pagePath() { return QStringLiteral("/chester/%1").arg(QString::fromLatin1(id)); }
    static QString hexPath() { return pagePath() + QStringLiteral("/hex"); }
    static QString sha256Hex(const QByteArray& bytes) {
        return QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    // The public catalog page as the CLI tests serve it (app/cli/tests/flash_e2e_test.cpp).
    static QByteArray page(const QString& sha) {
        return QStringLiteral("<html><head><title> CHESTER Firmware: hio-chester-push-fm:v4.0.1"
                              "</title></head><body><h2>hio-chester-push-fm:v4.0.1</h2>"
                              "<div>For a firmware update via the J-Link USB debugger, download "
                              "the HEX file:</div><div>SHA256 checksum: %1</div>"
                              "<div>Upload date/time</div><div>2026-09-07 12:13:53</div>"
                              "</body></html>")
            .arg(sha)
            .toUtf8();
    }

    [[nodiscard]] hwio::firmware::test::HttpFixture& http() { return m_http; }
    [[nodiscard]] const QByteArray& hex() const { return m_hex; }

private:
    hwio::firmware::test::HttpFixture m_http;
    QByteArray m_hex;
};

// The page was read: the consent details carry its name and version; the tool result does not
// change (ADR-0377); the download was verified against the page's SHA-256 (see the mismatch test).
TEST_F(CatalogFlash, ThePageNamesTheFirmwareInTheConsentDetails) {
    ASSERT_TRUE(http().listening());
    http().set(pagePath(), hwio::firmware::test::HttpFixture::Response::ok(page(sha256Hex(hex()))));
    http().set(hexPath(), hwio::firmware::test::HttpFixture::Response::ok(hex()));
    hwio::firmware::FirmwareCatalog catalog(
        hwio::firmware::CatalogOptions{.baseUrl = QUrl(http().baseUrl()),
                                       .transferTimeout = 5s,
                                       .maxBodyBytes = 1024ULL * 1024},
        nullptr);
    Harness harness(provider(), limits(), &catalog);
    connectDevice(harness);
    auto flash = harness.call(QStringLiteral("flash"), args({{"catalog_id", id}}), 30s);
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    EXPECT_EQ(http().requests(pagePath()), 1);
    EXPECT_EQ(http().requests(hexPath()), 1);
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    const QJsonObject& details = harness.consent.requests.at(0).details;
    EXPECT_EQ(details.value(QStringLiteral("catalog_id")).toString(), QString::fromLatin1(id));
    EXPECT_EQ(details.value(QStringLiteral("firmware_name")).toString(),
              QStringLiteral("hio-chester-push-fm"));
    EXPECT_EQ(details.value(QStringLiteral("firmware_version")).toString(),
              QStringLiteral("v4.0.1"));
    EXPECT_FALSE(flash.structured().contains(QStringLiteral("firmware_name")));
    EXPECT_FALSE(flash.structured().contains(QStringLiteral("firmware_version")));
}

// An unreadable page (not a catalog page): the flash goes on with the ID-only details.
TEST_F(CatalogFlash, AnUnreadablePageFlashesWithTheIdOnly) {
    ASSERT_TRUE(http().listening());
    http().set(pagePath(),
               hwio::firmware::test::HttpFixture::Response::ok("<html>maintenance</html>"));
    http().set(hexPath(), hwio::firmware::test::HttpFixture::Response::ok(hex()));
    hwio::firmware::FirmwareCatalog catalog(
        hwio::firmware::CatalogOptions{.baseUrl = QUrl(http().baseUrl()),
                                       .transferTimeout = 5s,
                                       .maxBodyBytes = 1024ULL * 1024},
        nullptr);
    Harness harness(provider(), limits(), &catalog);
    connectDevice(harness);
    auto flash = harness.call(QStringLiteral("flash"), args({{"catalog_id", id}}), 30s);
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    const QJsonObject& details = harness.consent.requests.at(0).details;
    EXPECT_EQ(details.value(QStringLiteral("catalog_id")).toString(), QString::fromLatin1(id));
    EXPECT_FALSE(details.contains(QStringLiteral("firmware_name")));
    EXPECT_FALSE(details.contains(QStringLiteral("firmware_version")));
}

// The HEX file does not have the page's SHA-256: firmware.checksum_mismatch (as in the CLI),
// no consent prompt, nothing flashed.
TEST_F(CatalogFlash, AChecksumMismatchFailsBeforeTheConsent) {
    ASSERT_TRUE(http().listening());
    http().set(pagePath(),
               hwio::firmware::test::HttpFixture::Response::ok(page(sha256Hex("another file"))));
    http().set(hexPath(), hwio::firmware::test::HttpFixture::Response::ok(hex()));
    hwio::firmware::FirmwareCatalog catalog(
        hwio::firmware::CatalogOptions{.baseUrl = QUrl(http().baseUrl()),
                                       .transferTimeout = 5s,
                                       .maxBodyBytes = 1024ULL * 1024},
        nullptr);
    Harness harness(provider(), limits(), &catalog);
    connectDevice(harness);
    auto flash = harness.call(QStringLiteral("flash"), args({{"catalog_id", id}}), 30s);
    EXPECT_EQ(flash.errorCode(), "firmware.checksum_mismatch");
    EXPECT_TRUE(harness.consent.requests.empty());
    EXPECT_TRUE(provider().record().busy.empty());
}

// Cancelled while the page is read: no download, no consent prompt, no flash.
TEST_F(CatalogFlash, ACancelDuringTheLookupEndsTheCall) {
    ASSERT_TRUE(http().listening());
    http().set(pagePath(), hwio::firmware::test::HttpFixture::Response::hanging());
    http().set(hexPath(), hwio::firmware::test::HttpFixture::Response::ok(hex()));
    hwio::firmware::FirmwareCatalog catalog(
        hwio::firmware::CatalogOptions{.baseUrl = QUrl(http().baseUrl()),
                                       .transferTimeout = 5s,
                                       .maxBodyBytes = 1024ULL * 1024},
        nullptr);
    Harness harness(provider(), limits(), &catalog);
    connectDevice(harness);
    auto flash = harness.start(QStringLiteral("flash"), args({{"catalog_id", id}}));
    const int callId = harness.lastId();
    ASSERT_TRUE(runUntil([&] { return http().requests(pagePath()) == 1; }, 5s));
    harness.cancel(callId);
    runFor(300ms);
    EXPECT_FALSE(flash.done()); // a cancelled call gets no response
    EXPECT_EQ(http().requests(hexPath()), 0);
    EXPECT_TRUE(harness.consent.requests.empty());
    EXPECT_TRUE(provider().record().busy.empty());
}

TEST_F(JLinkTools, BusyCancelAndDeviceClosedMidFlash) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("downloadDelayMs"), 1500}}))
            .toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    const QString path = QString::fromStdString(imagePath(0x0, 4096));

    auto flash = harness.start(QStringLiteral("flash"), args({{"path", path}}));
    ASSERT_TRUE(runUntil([&] { return !provider().record().busy.empty(); }));
    auto busy = harness.call(QStringLiteral("erase"));
    EXPECT_EQ(busy.errorCode(), "internal.busy");
    EXPECT_EQ(busy.errorDetail(), "flash");
    auto status = harness.call(QStringLiteral("status"));
    EXPECT_EQ(status.structured()
                  .value(QStringLiteral("devices"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("busy_operation"))
                  .toString(),
              QStringLiteral("flash"));
    // The user presses Cancel on the busy tab (ADR-0105).
    ASSERT_TRUE(provider().record().busyCancel);
    provider().record().busyCancel();
    ASSERT_TRUE(runUntil([&] { return flash.done(); }, 30s));
    EXPECT_EQ(flash.errorCode(), "cancelled");
    EXPECT_TRUE(provider().record().markers.back().second.contains(QStringLiteral("cancelled")));
    EXPECT_TRUE(runUntil(
        [&] {
            return harness.call(QStringLiteral("status"))
                .structured()
                .value(QStringLiteral("devices"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("busy_operation"))
                .isNull();
        },
        30s));

    // The device goes away while flashing (the GUI tab closes).
    auto second = harness.start(QStringLiteral("flash"), args({{"path", path}}));
    ASSERT_TRUE(runUntil([&] { return provider().record().busy.size() == 2; }));
    ASSERT_TRUE(provider().close(device, {}));
    ASSERT_TRUE(runUntil([&] { return second.done(); }, 30s));
    EXPECT_EQ(second.errorCode(), "mcp.device_not_open");
    EXPECT_EQ(provider().record().endBusy, 2);
}

TEST_F(JLinkTools, ClientCancelMidFlash) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("downloadDelayMs"), 1500}}))
            .toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto flash = harness.start(QStringLiteral("flash"),
                               args({{"path", QString::fromStdString(imagePath(0x0, 4096))}}));
    const int id = harness.lastId();
    ASSERT_TRUE(runUntil([&] { return !provider().record().busy.empty(); }));
    harness.cancel(id);
    ASSERT_TRUE(runUntil([&] { return provider().record().endBusy == 1; }, 30s));
    EXPECT_FALSE(flash.done()); // a cancelled call gets no response
    ASSERT_FALSE(harness.activity.entries.empty());
    EXPECT_EQ(harness.activity.entries.back().result, "cancelled");
}

TEST_F(JLinkTools, StalledOperationIsCancelledWithTimeout) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("downloadDelayMs"), 2000}}))
            .toJson());
    hwio::mcp::Limits shrunk = limits();
    shrunk.tools.operationStall = 300ms;
    Harness harness(provider(), shrunk);
    connectDevice(harness);
    auto flash = harness.call(QStringLiteral("flash"),
                              args({{"path", QString::fromStdString(imagePath(0x0, 4096))}}), 30s);
    EXPECT_EQ(flash.errorCode(), "timeout");
    EXPECT_TRUE(provider().record().markers.back().second.contains(QStringLiteral("failed")));
}

TEST_F(JLinkTools, WriteFlashEraseRecover) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto write = harness.call(QStringLiteral("write_flash"),
                              args({{"address", "0x00010000"}, {"data_hex", "0102030405060708"}}),
                              30s, true);
    ASSERT_FALSE(write.isError()) << write.errorCode() << " " << write.errorDetail();
    EXPECT_EQ(write.structured().value(QStringLiteral("bytes")).toInteger(), 8);
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::Flash));
    auto read = harness.call(QStringLiteral("read_memory"),
                             args({{"address", "0x00010000"}, {"length", 8}}));
    EXPECT_EQ(read.structured().value(QStringLiteral("hex")).toString(),
              QStringLiteral("0102030405060708"));

    // Review S2 / F4: UICR bytes without allow_uicr, the PIB and bytes outside flash and UICR are
    // refused before the consent request (checkFlashWrite): no request is recorded.
    const std::size_t requestsBefore = harness.consent.requests.size();
    auto uicr = harness.call(QStringLiteral("write_flash"),
                             args({{"address", "0x10001000"}, {"data_hex", "00"}}));
    EXPECT_EQ(uicr.errorCode(), "firmware.uicr_in_image");
    auto pib =
        harness.call(QStringLiteral("write_flash"),
                     args({{"address", "0x10001080"}, {"data_hex", "00"}, {"allow_uicr", true}}));
    EXPECT_EQ(pib.errorCode(), "firmware.out_of_range");
    auto outside = harness.call(QStringLiteral("write_flash"),
                                args({{"address", "0x20000000"}, {"data_hex", "00"}}));
    EXPECT_EQ(outside.errorCode(), "firmware.out_of_range");
    EXPECT_EQ(harness.consent.requests.size(), requestsBefore);
    // With allow_uicr: flash + uicr.
    auto allowed = harness.call(
        QStringLiteral("write_flash"),
        args({{"address", "0x10001200"}, {"data_hex", "00"}, {"allow_uicr", true}}), 30s);
    ASSERT_FALSE(allowed.isError()) << allowed.errorCode() << " " << allowed.errorDetail();
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategory::Flash | hwio::mcp::ConsentCategory::Uicr);
    EXPECT_TRUE(harness.consent.requests.back().details.value(QStringLiteral("uicr")).toBool());
    // allow_uicr with bytes outside the UICR: flash only.
    auto plain = harness.call(
        QStringLiteral("write_flash"),
        args({{"address", "0x00010010"}, {"data_hex", "01"}, {"allow_uicr", true}}), 30s);
    ASSERT_FALSE(plain.isError()) << plain.errorCode() << " " << plain.errorDetail();
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::Flash));

    // ADR-0161: erase scope=code needs erase; scope=all and recover need erase + uicr.
    auto erase = harness.call(QStringLiteral("erase"), {}, 30s);
    ASSERT_FALSE(erase.isError()) << erase.errorCode() << " " << erase.errorDetail();
    EXPECT_EQ(erase.structured().value(QStringLiteral("scope")).toString(), QStringLiteral("code"));
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategories(hwio::mcp::ConsentCategory::Erase));
    auto eraseAll = harness.call(QStringLiteral("erase"), args({{"scope", "all"}}), 30s);
    ASSERT_FALSE(eraseAll.isError()) << eraseAll.errorCode() << " " << eraseAll.errorDetail();
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategory::Erase | hwio::mcp::ConsentCategory::Uicr);
    auto recover = harness.call(QStringLiteral("recover"), {}, 60s);
    ASSERT_FALSE(recover.isError()) << recover.errorCode() << " " << recover.errorDetail();
    EXPECT_TRUE(recover.structured().contains(QStringLiteral("duration")));
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategory::Erase | hwio::mcp::ConsentCategory::Uicr);
}

// ADR-0161 with the CLI broker rule: a server allowed to erase only cannot erase everything.
TEST_F(JLinkTools, EraseAllNeedsUicrConsent) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    harness.consent.mode = hwio::mcp::test::FakeConsentBroker::Mode::Deny;
    auto eraseAll = harness.call(QStringLiteral("erase"), args({{"scope", "all"}}), 30s);
    EXPECT_EQ(eraseAll.errorCode(), "mcp.tool_not_allowed");
    EXPECT_EQ(harness.consent.requests.back().categories,
              hwio::mcp::ConsentCategory::Erase | hwio::mcp::ConsentCategory::Uicr);
    EXPECT_EQ(harness.consent.requests.back().details.value(QStringLiteral("scope")).toString(),
              QStringLiteral("all"));
}

TEST_F(JLinkTools, MissingOptionalExport) {
    provider().settings().library = HWIO_FAKE_JLINK_BASIC_LIBRARY;
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto status = harness.call(QStringLiteral("target_status"));
    EXPECT_EQ(status.errorCode(), "dependency.jlink_function_missing");
}

TEST_F(JLinkTools, BleSession) {
    Harness harness(provider(), limits());
    auto connect =
        harness.call(QStringLiteral("connect"), args({{"device", "ble:F1:22:33:44:55:66"}}));
    ASSERT_FALSE(connect.isError()) << connect.errorCode() << " " << connect.errorDetail();
    ASSERT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
    auto command = harness.call(QStringLiteral("send_command"), args({{"command", "version"}}));
    ASSERT_FALSE(command.isError()) << command.errorCode();
    EXPECT_EQ(command.structured().value(QStringLiteral("end")).toString(),
              QStringLiteral("prompt"));
    EXPECT_EQ(command.structured().value(QStringLiteral("output")).toArray().size(), 1);
    for (const char* tool : {"target_status", "stop", "release_probe", "read_pib"}) {
        EXPECT_EQ(harness.call(QString::fromLatin1(tool)).errorCode(), "mcp.capability_missing")
            << tool;
    }
}

} // namespace
} // namespace hwio::mcp::tools::test
