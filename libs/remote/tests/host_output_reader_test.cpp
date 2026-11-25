// HostOutputReader (plan 0061 amendment 1, ADR-0431): line mode for a Monitor or web Terminal
// host, stream mode for a host that sends pieces of the byte stream (the HARDWARIO mobile app).

#include "host_output_reader.h"
#include "hwio/device/console_line.h"
#include "hwio/device/line_assembler.h"

#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace hwio::remote {
namespace {

std::vector<std::string> read(HostOutputReader& reader, const QString& data) {
    std::vector<std::string> out;
    for (const device::ConsoleLine& line : reader.read(data, std::chrono::system_clock::now())) {
        EXPECT_EQ(line.dir, device::Direction::Out);
        out.push_back(line.text);
    }
    return out;
}

device::ConsoleLine historyLine(const std::string& text) {
    device::ConsoleLine line;
    line.dir = device::Direction::Out;
    line.text = text;
    classifyHostOutput(line);
    return line;
}

TEST(HostOutputReader, LineModeIsOneLinePerRead) {
    HostOutputReader reader;
    EXPECT_FALSE(reader.streamMode());
    EXPECT_EQ(read(reader, QStringLiteral("+CSQ: 20,99")), std::vector<std::string>{"+CSQ: 20,99"});
    EXPECT_EQ(read(reader, QString()), std::vector<std::string>{""}); // an empty line stays
    EXPECT_EQ(read(reader, QStringLiteral("bt_nus:~$ ")), std::vector<std::string>{"bt_nus:~$ "});
    EXPECT_FALSE(reader.streamMode());
    EXPECT_TRUE(reader.keepHistoryLine(historyLine("")));
}

TEST(HostOutputReader, ALineFeedSwitchesToStreamModeForGood) {
    HostOutputReader reader;
    reader.commandShown();
    EXPECT_EQ(read(reader, QStringLiteral("\r\nVendor name: HARDWARIO\r\nSerial: 1\r\nbt_nus:~$ ")),
              (std::vector<std::string>{"Vendor name: HARDWARIO", "Serial: 1"}));
    EXPECT_TRUE(reader.streamMode());
    // A piece without a line feed is no longer a line of its own.
    EXPECT_TRUE(read(reader, QStringLiteral("app config interval-sa")).empty());
    EXPECT_EQ(read(reader, QStringLiteral("mple 60\r\n")),
              std::vector<std::string>{"app config interval-sample 60"});
    reader.restart(); // a sync: the mode stays
    EXPECT_TRUE(reader.streamMode());
}

TEST(HostOutputReader, TheHelloKeepsLineMode) {
    HostOutputReader reader;
    reader.hostHello();
    EXPECT_EQ(read(reader, QStringLiteral("a\r\nb")), std::vector<std::string>{"b"});
    reader.streamSeen();
    EXPECT_FALSE(reader.streamMode());

    HostOutputReader late;
    late.streamSeen();
    EXPECT_TRUE(late.streamMode());
    late.hostHello();
    EXPECT_FALSE(late.streamMode());
}

// As Monitor's Bluetooth console: the prompt is not a line, and the one empty line after a command
// or after a prompt is not output; later empty lines are. No `write` is needed (a command typed on
// the phone comes without one).
TEST(HostOutputReader, PromptAndTheFirstEmptyLine) {
    HostOutputReader reader;
    // Stream mode; nothing came before, so this empty line is output.
    EXPECT_EQ(read(reader, QStringLiteral("\r\n")), std::vector<std::string>{""});
    reader.restart();
    EXPECT_TRUE(read(reader, QStringLiteral("bt_nus:~$ ")).empty()); // the prompt alone
    EXPECT_EQ(read(reader, QStringLiteral("\r\nfirst\r\n\r\nsecond\r\nbt_nus:~$ ")),
              (std::vector<std::string>{"first", "", "second"}));
    reader.commandShown();
    EXPECT_EQ(read(reader, QStringLiteral("\r\n\r\nx\r\n")), (std::vector<std::string>{"", "x"}));
    // History lines of a sync in stream form: the prompt and the empty line after it are dropped.
    EXPECT_FALSE(reader.keepHistoryLine(historyLine("bt_nus:~$ ")));
    EXPECT_FALSE(reader.keepHistoryLine(historyLine("")));
    EXPECT_TRUE(reader.keepHistoryLine(historyLine("")));
    reader.commandShown();
    EXPECT_FALSE(reader.keepHistoryLine(historyLine("")));
    EXPECT_TRUE(reader.keepHistoryLine(historyLine("text")));
}

TEST(HostOutputReader, LogLinesAndTheUnfinishedLineBound) {
    HostOutputReader reader;
    std::vector<device::ConsoleLine> lines =
        reader.read(QStringLiteral("\r\n[00:00:01.000,000] <wrn> app: low battery\r\n"),
                    std::chrono::system_clock::now());
    ASSERT_EQ(lines.size(), 2U); // no rule yet for the first empty line: nothing came before
    EXPECT_EQ(lines.back().channel, device::Channel::Log);
    EXPECT_EQ(lines.back().level, device::LogLevel::Warning);
    // An unfinished line longer than the assembler's bound is emitted as a line.
    const QString longLine(static_cast<qsizetype>(device::LineAssembler::maxPartialBytes) + 100,
                           QLatin1Char('x'));
    const auto out = read(reader, longLine);
    ASSERT_FALSE(out.empty());
    EXPECT_LE(out.front().size(), device::LineAssembler::maxPartialBytes);
}

} // namespace
} // namespace hwio::remote
