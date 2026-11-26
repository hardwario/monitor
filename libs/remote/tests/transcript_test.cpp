#include "hwio/remote/transcript.h"

#include <QByteArray>
#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

namespace hwio::remote {
namespace {

using namespace std::chrono_literals;

std::chrono::system_clock::time_point at(std::int64_t ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

// What the web host (console.tsx handleWriteLocal / handleReadLocal) builds, compared as JSON.
TEST(Transcript, SameFormatAsTheWebTerminal) {
    Transcript transcript;
    transcript.addOutput(QStringLiteral("boot"), at(1762768800000));
    transcript.addCommand(QStringLiteral("info show"), at(1762768801234));
    transcript.addOutput(QStringLiteral("serial: 1"), at(1762768801300));
    const QByteArray expected = R"json([
      {"id":"system-2025-11-10T10:00:00.000Z","timestamp":"2025-11-10T10:00:00.000Z",
       "command":"(device output)","index":0,"response":{"outputs":["boot"],"result":0}},
      {"id":"2025-11-10T10:00:01.234Z-1","timestamp":"2025-11-10T10:00:01.234Z",
       "command":"info show","index":1,"response":{"outputs":["serial: 1"],"result":0}}
    ])json";
    EXPECT_EQ(QJsonDocument(transcript.toJson()), QJsonDocument::fromJson(expected));
}

TEST(Transcript, BoundedByEntriesAndBytes) {
    Transcript transcript(TranscriptLimits{.maxEntries = 3, .maxBytes = 100});
    for (int i = 0; i < 10; ++i) {
        transcript.addCommand(QStringLiteral("c%1").arg(i), at(i));
    }
    ASSERT_EQ(transcript.entries().size(), 3U);
    EXPECT_EQ(transcript.entries().front().command, QStringLiteral("c7"));
    EXPECT_EQ(transcript.entries().back().index, 9);
    for (int i = 0; i < 50; ++i) {
        transcript.addOutput(QStringLiteral("0123456789"), at(100 + i));
    }
    EXPECT_LE(transcript.bytes(), 100U);
    EXPECT_FALSE(transcript.entries().empty());
}

TEST(Transcript, ParsesAReceivedSyncWithLimits) {
    QJsonArray commands;
    for (int i = 0; i < 20; ++i) {
        QJsonObject response;
        response.insert(QStringLiteral("outputs"),
                        QJsonArray{QStringLiteral("out\u0007%1").arg(i), 5});
        QJsonObject entry;
        entry.insert(QStringLiteral("command"), QStringLiteral("cmd %1").arg(i));
        entry.insert(QStringLiteral("index"), i);
        entry.insert(QStringLiteral("response"), response);
        commands.append(entry);
    }
    commands.append(QStringLiteral("garbage"));
    auto transcript =
        Transcript::fromJson(commands, TranscriptLimits{.maxEntries = 5, .maxBytes = 1000});
    ASSERT_TRUE(transcript.has_value());
    EXPECT_TRUE(transcript->truncated());
    ASSERT_EQ(transcript->entries().size(), 4U); // the last 5 array items, one of them invalid
    EXPECT_EQ(transcript->entries().back().command, QStringLiteral("cmd 19"));
    ASSERT_EQ(transcript->entries().back().outputs.size(), 1);
    EXPECT_FALSE(transcript->entries().back().outputs.front().contains(QChar(7)));
    EXPECT_FALSE(Transcript::fromJson(QJsonObject{}).has_value());
}

// Plan 0061 (ADR-0431): the mobile app puts the whole output of a command into one `outputs`
// element; fromJson splits it into lines, and the newest entry's unfinished line (the prompt) is
// handed on instead of being a line.
TEST(Transcript, SplitsAnOutputInStreamForm) {
    const auto entry = [](const QString& command, const QString& output) {
        QJsonObject response;
        response.insert(QStringLiteral("outputs"), QJsonArray{output});
        QJsonObject object;
        object.insert(QStringLiteral("command"), command);
        object.insert(QStringLiteral("response"), response);
        return object;
    };
    auto transcript = Transcript::fromJson(
        QJsonArray{entry(QStringLiteral("info show"),
                         QStringLiteral("\r\nVendor name: HARDWARIO\r\nSerial: 1\r\nbt_nus:~$ ")),
                   entry(QStringLiteral("config show"),
                         QStringLiteral("\r\nlrw config region eu868\r\nbt_nus:~$ "))});
    ASSERT_TRUE(transcript.has_value());
    EXPECT_TRUE(transcript->streamForm());
    ASSERT_EQ(transcript->entries().size(), 2U);
    EXPECT_EQ(transcript->entries().front().outputs,
              (QStringList{QString(), QStringLiteral("Vendor name: HARDWARIO"),
                           QStringLiteral("Serial: 1"), QStringLiteral("bt_nus:~$ ")}));
    EXPECT_EQ(transcript->entries().back().outputs,
              (QStringList{QString(), QStringLiteral("lrw config region eu868")}));
    EXPECT_EQ(transcript->unfinishedTail(), QStringLiteral("bt_nus:~$ "));

    // One line per element (Monitor, web Terminal): unchanged, no tail.
    auto lines = Transcript::fromJson(QJsonArray{entry(QStringLiteral("x"), QStringLiteral("y"))});
    ASSERT_TRUE(lines.has_value());
    EXPECT_FALSE(lines->streamForm());
    EXPECT_TRUE(lines->unfinishedTail().isEmpty());
    EXPECT_EQ(lines->entries().front().outputs, QStringList{QStringLiteral("y")});

    // The byte bound counts the split lines; the tail is bounded.
    QString many;
    for (int i = 0; i < 100; ++i) {
        many += QStringLiteral("line %1\r\n").arg(i);
    }
    many += QString(Transcript::maxTailChars + 10, QLatin1Char('x'));
    auto bounded = Transcript::fromJson(QJsonArray{entry(QStringLiteral("c"), many)},
                                        TranscriptLimits{.maxEntries = 5, .maxBytes = 200});
    ASSERT_TRUE(bounded.has_value());
    EXPECT_TRUE(bounded->truncated());
    EXPECT_LE(bounded->bytes(), 200U);
    EXPECT_EQ(bounded->entries().back().outputs.back(), QStringLiteral("line 99"));
    EXPECT_EQ(bounded->unfinishedTail().size(), Transcript::maxTailChars);
}

} // namespace
} // namespace hwio::remote
