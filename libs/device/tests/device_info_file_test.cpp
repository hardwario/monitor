// Plan 0063 (ADR-0440): the device information file - the collector over silent shell queries
// (answers, no answer, a cut answer, a disconnect, cancel, refusals), the pure formatting and file
// name, the secrets hidden or included, and the owner-only file.

#include "fake_console_session.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/device_info_file.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/device/shell_query.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using hwio::ErrorCode;
using hwio::device::DeviceInfoCollector;
using hwio::device::DeviceInfoCollectorOptions;
using hwio::device::DeviceInfoDocument;
using hwio::device::DeviceInfoSection;
using hwio::device::ShellQueryQueue;
using hwio::device::test::FakeSession;
using hwio::device::test::runFor;
using hwio::device::test::runUntil;
using namespace std::chrono_literals;

// 2026-11-25 14:15:02 at UTC+01:00.
QDateTime fixedTime() {
    return {QDate(2026, 11, 25), QTime(14, 15, 2), QTimeZone::fromSecondsAheadOfUtc(3600)};
}

std::vector<std::string> infoShow(bool withSerial = true) {
    std::vector<std::string> lines{"vendor name: HARDWARIO a.s.", "product name: CHESTER-M",
                                   "hardware variant: CDGLS", "firmware name: CHESTER Clime",
                                   "firmware version: v3.0.0"};
    if (withSerial) {
        lines.emplace_back("serial number: 2159018267");
    }
    lines.emplace_back("claim token: 0123456789abcdef0123456789abcdef");
    lines.emplace_back("ble passkey: 654321");
    return lines;
}

std::vector<std::string> configShow() {
    return {"app config interval-sample 60", "lrw config appkey 00112233445566778899aabbccddeeff",
            "lte config password s3cr3t", "lte config apn iot.example"};
}

DeviceInfoCollectorOptions fastOptions() {
    return DeviceInfoCollectorOptions{.commandTimeout = 400ms, .clock = fixedTime};
}

// A J-Link session (probe 801000001) answering `info show` and `config show` by default.
struct Rig {
    FakeSession session;
    ShellQueryQueue queries;

    Rig() {
        session.setIdentity({.transport = hwio::device::Transport::JLink,
                             .id = "801000001",
                             .displayName = "J-Link 801000001"});
        session.answer("info show", infoShow());
        session.answer("config show", configShow());
        queries.setSession(&session);
    }
    ~Rig() { queries.setSession(nullptr); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;
};

hwio::Result<DeviceInfoDocument> collect(DeviceInfoCollector& collector) {
    EXPECT_TRUE(collector.start());
    EXPECT_TRUE(runUntil([&] { return collector.result().has_value(); }, 10s));
    return collector.result().value_or(
        std::unexpected(hwio::makeError(ErrorCode::Timeout, "no result")));
}

DeviceInfoDocument sampleDocument() {
    return DeviceInfoDocument{
        .serial = "2159018267",
        .connectionId = "801000001",
        .deviceName = "CHESTER-M",
        .firmware = "CHESTER Clime v3.0.0",
        .time = fixedTime(),
        .monitorVersion = "2.0.0-rc.5",
        .profileId = "chester",
        .targetId = "app",
        .sections =
            {DeviceInfoSection{
                 .command = "info show", .answered = true, .truncated = false, .lines = infoShow()},
             DeviceInfoSection{.command = "config show",
                               .answered = true,
                               .truncated = false,
                               .lines = configShow()},
             DeviceInfoSection{
                 .command = "lte state", .answered = false, .truncated = false, .lines = {}},
             DeviceInfoSection{
                 .command = "log", .answered = true, .truncated = true, .lines = {"line 1"}}},
    };
}

QByteArray readAll(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

TEST(DeviceInfoFileTest, ChesterListsItsCommands) {
    const auto* chester = hwio::device::ProfileRegistry::find("chester");
    ASSERT_NE(chester, nullptr);
    ASSERT_EQ(chester->infoFileCommands.size(), 2U);
    EXPECT_EQ(chester->infoFileCommands[0], "info show");
    EXPECT_EQ(chester->infoFileCommands[1], "config show");
    EXPECT_LE(chester->infoFileCommands.size(), DeviceInfoCollector::maxCommands);
    EXPECT_TRUE(hwio::device::genericConsoleProfile().infoFileCommands.empty());
}

TEST(DeviceInfoFileTest, FormatHeaderSectionsNoAnswerAndCut) {
    const std::string text = hwio::device::formatDeviceInfo(sampleDocument());
    EXPECT_EQ(text, "HARDWARIO Monitor - device information\n"
                    "Device: CHESTER-M\n"
                    "Serial number: 2159018267\n"
                    "Firmware: CHESTER Clime v3.0.0\n"
                    "Date: 2026-11-25 14:15:02 +01:00\n"
                    "HARDWARIO Monitor: 2.0.0-rc.5\n"
                    "\n### info show\n"
                    "vendor name: HARDWARIO a.s.\n"
                    "product name: CHESTER-M\n"
                    "hardware variant: CDGLS\n"
                    "firmware name: CHESTER Clime\n"
                    "firmware version: v3.0.0\n"
                    "serial number: 2159018267\n"
                    "claim token: 0123456789abcdef0123456789abcdef\n"
                    "ble passkey: 654321\n"
                    "\n### config show\n"
                    "app config interval-sample 60\n"
                    "lrw config appkey 00112233445566778899aabbccddeeff\n"
                    "lte config password s3cr3t\n"
                    "lte config apn iot.example\n"
                    "\n### lte state\n"
                    "(no answer)\n"
                    "\n### log\n"
                    "line 1\n"
                    "(the answer was cut here: at most 2000 lines or 256 KiB per command)\n");
    DeviceInfoDocument unknown;
    EXPECT_EQ(hwio::device::formatDeviceInfo(unknown), "HARDWARIO Monitor - device information\n"
                                                       "Device: unknown\n"
                                                       "Serial number: unknown\n"
                                                       "Firmware: unknown\n"
                                                       "Date: unknown\n"
                                                       "HARDWARIO Monitor: unknown\n");
}

TEST(DeviceInfoFileTest, SecretsAreHiddenWithTheConsoleFileRules) {
    const DeviceInfoDocument hidden = hwio::device::withoutSecrets(sampleDocument());
    const std::string text = hwio::device::formatDeviceInfo(hidden);
    EXPECT_NE(text.find("claim token: <redacted>\n"), std::string::npos) << text;
    EXPECT_NE(text.find("ble passkey: <redacted>\n"), std::string::npos) << text;
    EXPECT_NE(text.find("lrw config appkey <redacted>\n"), std::string::npos) << text;
    EXPECT_NE(text.find("lte config password <redacted>\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("0123456789abcdef"), std::string::npos) << text;
    EXPECT_EQ(text.find("654321"), std::string::npos) << text;
    EXPECT_EQ(text.find("00112233"), std::string::npos) << text;
    EXPECT_EQ(text.find("s3cr3t"), std::string::npos) << text;
    // The rest unchanged.
    EXPECT_NE(text.find("serial number: 2159018267\n"), std::string::npos);
    EXPECT_NE(text.find("lte config apn iot.example\n"), std::string::npos);
    EXPECT_NE(text.find("app config interval-sample 60\n"), std::string::npos);
}

TEST(DeviceInfoFileTest, FileNameBySerialOrConnection) {
    DeviceInfoDocument document = sampleDocument();
    EXPECT_EQ(hwio::device::deviceInfoFileName(document), "2159018267_2026-11-25_141502.txt");
    document.serial.clear();
    EXPECT_EQ(hwio::device::deviceInfoFileName(document),
              "unknown-801000001_2026-11-25_141502.txt");
    document.connectionId = "F1:22:33:44:55:66"; // Bluetooth on Linux: ':' is not a file name byte
    EXPECT_EQ(hwio::device::deviceInfoFileName(document),
              "unknown-F1-22-33-44-55-66_2026-11-25_141502.txt");
    document.serial = "../x y\\z";
    EXPECT_EQ(hwio::device::deviceInfoFileName(document), "-.-x-y-z_2026-11-25_141502.txt");
    document.serial = std::string(100, '7');
    EXPECT_EQ(hwio::device::deviceInfoFileName(document),
              std::string(hwio::device::maxNamePartBytes, '7') + "_2026-11-25_141502.txt");
}

TEST(DeviceInfoFileTest, CollectsTheProfilesCommandsSilently) {
    Rig rig;
    DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
    int progress = 0;
    QObject::connect(&collector, &DeviceInfoCollector::progressChanged, [&] { ++progress; });
    const auto document = collect(collector);
    ASSERT_TRUE(document) << document.error().detail;
    EXPECT_EQ(rig.session.written(), (std::vector<std::string>{"info show", "config show"}));
    EXPECT_EQ(rig.session.log().size(hwio::device::Channel::Shell), 0U); // silent queries
    EXPECT_EQ(collector.done(), 0U); // the document moved to the result
    EXPECT_GE(progress, 3);          // start, two answers
    EXPECT_FALSE(collector.running());
    EXPECT_EQ(document->serial, "2159018267");
    EXPECT_EQ(document->connectionId, "801000001");
    EXPECT_EQ(document->deviceName, "CHESTER-M");
    EXPECT_EQ(document->firmware, "CHESTER Clime v3.0.0");
    EXPECT_EQ(document->time, fixedTime());
    EXPECT_EQ(document->profileId, "chester");
    EXPECT_EQ(document->targetId, "app");
    ASSERT_EQ(document->sections.size(), 2U);
    EXPECT_TRUE(document->sections[0].answered);
    EXPECT_EQ(document->sections[0].lines, infoShow()); // secrets kept until written
    EXPECT_EQ(document->sections[1].lines, configShow());
    EXPECT_TRUE(document->unanswered().empty());
    EXPECT_EQ(hwio::device::deviceInfoFileName(*document), "2159018267_2026-11-25_141502.txt");
}

TEST(DeviceInfoFileTest, ACommandWithoutAnAnswerIsWrittenAsNoAnswer) {
    Rig rig;
    rig.session.answer("config show", {}); // nothing printed: the query times out
    DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
    const auto document = collect(collector);
    ASSERT_TRUE(document) << document.error().detail;
    ASSERT_EQ(document->sections.size(), 2U);
    EXPECT_TRUE(document->sections[0].answered);
    EXPECT_FALSE(document->sections[1].answered);
    EXPECT_EQ(document->unanswered(), std::vector<std::string>{"config show"});
    EXPECT_NE(hwio::device::formatDeviceInfo(*document).find("### config show\n(no answer)\n"),
              std::string::npos);
}

TEST(DeviceInfoFileTest, ALongAnswerIsCutWithAMarker) {
    Rig rig;
    std::vector<std::string> many;
    many.reserve(hwio::device::ShellQuery::defaultMaxLines + 50);
    for (std::size_t i = 0; i < hwio::device::ShellQuery::defaultMaxLines + 50; ++i) {
        many.push_back("app config value-" + std::to_string(i) + " 1");
    }
    rig.session.answer("config show", many);
    DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
    const auto document = collect(collector);
    ASSERT_TRUE(document) << document.error().detail;
    ASSERT_EQ(document->sections.size(), 2U);
    EXPECT_TRUE(document->sections[1].answered);
    EXPECT_TRUE(document->sections[1].truncated);
    EXPECT_EQ(document->sections[1].lines.size(), hwio::device::ShellQuery::defaultMaxLines);
    const std::string text = hwio::device::formatDeviceInfo(*document);
    EXPECT_TRUE(
        text.ends_with("app config value-1999 1\n"
                       "(the answer was cut here: at most 2000 lines or 256 KiB per command)\n"))
        << text.substr(text.size() - 200);
}

TEST(DeviceInfoFileTest, WithoutASerialNumberTheNameUsesTheConnection) {
    Rig rig;
    rig.session.answer("info show", infoShow(false));
    DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
    const auto document = collect(collector);
    ASSERT_TRUE(document) << document.error().detail;
    EXPECT_TRUE(document->serial.empty());
    EXPECT_EQ(hwio::device::deviceInfoFileName(*document),
              "unknown-801000001_2026-11-25_141502.txt");
}

TEST(DeviceInfoFileTest, ADisconnectEndsWithoutADocument) {
    Rig rig;
    rig.session.setDelayEcho(true); // `info show` waits for its echo
    DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
    ASSERT_TRUE(collector.start());
    ASSERT_TRUE(runUntil([&] { return rig.session.written().size() == 1U; }));
    EXPECT_TRUE(collector.running());
    EXPECT_EQ(collector.total(), 2U);
    rig.session.setState(hwio::device::ConnectionState::Disconnected);
    ASSERT_TRUE(collector.result().has_value());
    const auto result = collector.result().value_or(DeviceInfoDocument{});
    ASSERT_FALSE(result);
    const hwio::Error& error = result.error();
    EXPECT_EQ(error.code, ErrorCode::TargetNotConnected);
    EXPECT_EQ(error.messageEn, "The device disconnected while its information was read.");
    EXPECT_FALSE(collector.running());
    runFor(200ms);
    EXPECT_EQ(rig.session.written().size(), 1U); // `config show` was never sent
}

// The owner releases the session from the queue (the GUI tab's Disconnect): the pending query is
// answered with nullopt while the console is still Connected - a disconnect, not "(no answer)".
TEST(DeviceInfoFileTest, AReleasedSessionEndsWithoutADocument) {
    Rig rig;
    rig.session.setDelayEcho(true);
    DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
    ASSERT_TRUE(collector.start());
    ASSERT_TRUE(runUntil([&] { return rig.session.written().size() == 1U; }));
    rig.queries.setSession(nullptr);
    ASSERT_TRUE(collector.result().has_value());
    const auto result = collector.result().value_or(DeviceInfoDocument{});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetNotConnected);
    // A queue without this session refuses to start.
    EXPECT_EQ(collector.start().error().code, ErrorCode::TargetNotConnected);
}

TEST(DeviceInfoFileTest, CancelGivesNoResultAndAnotherRunWorks) {
    Rig rig;
    rig.session.setDelayEcho(true);
    DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
    int finished = 0;
    QObject::connect(&collector, &DeviceInfoCollector::finished, [&] { ++finished; });
    ASSERT_TRUE(collector.start());
    EXPECT_EQ(collector.start().error().code, ErrorCode::InternalBusy);
    ASSERT_TRUE(runUntil([&] { return rig.session.written().size() == 1U; }));
    collector.cancel();
    EXPECT_FALSE(collector.running());
    rig.session.setDelayEcho(false);
    rig.session.releaseEchoes(); // the first query's answer is not ours any more
    ASSERT_TRUE(runUntil([&] { return !rig.queries.running(); }));
    runFor(100ms);
    EXPECT_EQ(finished, 0);
    EXPECT_FALSE(collector.result().has_value());

    const auto document = collect(collector);
    ASSERT_TRUE(document) << document.error().detail;
    EXPECT_EQ(finished, 1);
    EXPECT_EQ(document->sections.size(), 2U);
}

TEST(DeviceInfoFileTest, RefusedWhenNotConnectedRemoteOrWithoutCommands) {
    Rig rig;
    {
        rig.session.setState(hwio::device::ConnectionState::Disconnected);
        DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
        EXPECT_EQ(collector.start().error().code, ErrorCode::TargetNotConnected);
        rig.session.setState(hwio::device::ConnectionState::Connected);
    }
    {
        rig.session.setIdentity({.transport = hwio::device::Transport::Remote,
                                 .id = "remote",
                                 .displayName = "Session 123 456"});
        DeviceInfoCollector collector(rig.session, rig.queries, fastOptions());
        const hwio::Status started = collector.start();
        ASSERT_FALSE(started);
        EXPECT_EQ(started.error().code, ErrorCode::UsageInvalidArgument);
        EXPECT_FALSE(started.error().messageEn.empty());
    }
    FakeSession generic(hwio::device::genericConsoleProfile());
    ShellQueryQueue queries;
    queries.setSession(&generic);
    DeviceInfoCollector collector(generic, queries, fastOptions());
    EXPECT_EQ(collector.start().error().code, ErrorCode::UsageInvalidArgument);
    queries.setSession(nullptr);
    EXPECT_TRUE(rig.session.written().empty()); // nothing was ever sent
}

TEST(DeviceInfoFileTest, CollectDeviceInfoRunsItsOwnEventLoop) {
    Rig rig;
    const auto document =
        hwio::device::collectDeviceInfo(rig.session, rig.queries, {}, fastOptions());
    ASSERT_TRUE(document) << document.error().detail;
    EXPECT_EQ(document->sections.size(), 2U);
}

TEST(DeviceInfoFileTest, WrittenOwnerOnlyHiddenOrWithSecrets) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    namespace fs = std::filesystem;
    const fs::path folder(dir.path().toStdString());
    const DeviceInfoDocument document = sampleDocument();

    const fs::path hidden = folder / hwio::device::deviceInfoFileName(document);
    ASSERT_TRUE(hwio::device::writeDeviceInfoFile(hidden, document, {}));
    const QByteArray bytes = readAll(hidden);
    EXPECT_EQ(bytes.toStdString(),
              hwio::device::formatDeviceInfo(hwio::device::withoutSecrets(document)));
    EXPECT_FALSE(bytes.contains('\r')); // LF on every system
    EXPECT_FALSE(bytes.contains("654321"));

    const fs::path full = folder / "with-secrets.txt";
    ASSERT_TRUE(hwio::device::writeDeviceInfoFile(full, document, {.withSecrets = true}));
    EXPECT_EQ(readAll(full).toStdString(), hwio::device::formatDeviceInfo(document));
#ifndef _WIN32
    // ADR-0426: owner-only both ways (Windows: the folder's ACL, see owner_only.h).
    EXPECT_EQ(fs::status(hidden).permissions(), fs::perms::owner_read | fs::perms::owner_write);
    EXPECT_EQ(fs::status(full).permissions(), fs::perms::owner_read | fs::perms::owner_write);
#endif

    const hwio::Status failed =
        hwio::device::writeDeviceInfoFile(folder / "missing" / "a.txt", document, {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, ErrorCode::IoWriteFailed);
    EXPECT_NE(failed.error().detail.find("a.txt"), std::string::npos);
}

} // namespace
