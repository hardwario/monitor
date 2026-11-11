#pragma once

// One line of device console output or input (plan/features/0002-m1-jlink-rtt.md, ADR-0063).

#include <QMetaType>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::device {

enum class Channel : std::uint8_t {
    Shell, // RTT terminal buffer: shell output and the commands sent
    Log,   // RTT logger buffer (and '#'-prefixed shell lines when the profile says so)
};

enum class Direction : std::uint8_t {
    Out,    // from the device
    In,     // a command sent to the device (echoed after it was written)
    Marker, // a marker line added by the GUI (ADR-0063); never produced by the session or CLI
};

enum class LogLevel : std::uint8_t {
    None, // no Zephyr level tag (shell output, commands)
    Debug,
    Info,
    Warning,
    Error,
    // Plan 0021 (ADR-0171): a Zephyr LOG_HEXDUMP continuation line (no tag of its own; the header
    // line keeps its level). Hidden by default in the GUI log view (chip H).
    Hexdump,
};

// Plan 0044 (ADR-0333, ADR-0335): who caused a line. Remote = a command a remote support session
// participant sent (its echo) or an audit marker of a remote session (ConsoleSession::
// recordRemoteEvent()); Local for everything else. Shown as the remote marker in the GUI, visible
// to MCP with the line; console files get the audit markers as "@" lines.
enum class LineSource : std::uint8_t {
    Local,
    Remote,
};

struct ConsoleLine {
    std::uint64_t seq = 0; // position in the ConsoleLog (assigned by ConsoleLog::append)
    Channel channel = Channel::Shell;
    Direction dir = Direction::Out;
    std::chrono::system_clock::time_point time;
    LogLevel level = LogLevel::None;
    std::string text; // UTF-8, without line terminator and ANSI CSI sequences
    // Plan 0021 (ADR-0175): on the echo (dir In) of a line sent with
    // ConsoleSession::sendCaptureLine() the token it returned, so that a silent shell query
    // recognises its own echo by origin, not by text; 0 for every other line. Not stored in
    // console files or JSON.
    std::uint64_t origin = 0;
    LineSource source = LineSource::Local; // plan 0044
};

// Stable lowercase ids used in JSON and console files.
[[nodiscard]] std::string_view channelId(Channel channel) noexcept;       // "shell", "log"
[[nodiscard]] std::string_view directionId(Direction direction) noexcept; // "out", "in", "marker"
// "dbg", "inf", "wrn", "err", "hex" (ADR-0171), or none.
[[nodiscard]] std::optional<std::string_view> levelId(LogLevel level) noexcept;
// "local", "remote" (plan 0044, JSON).
[[nodiscard]] std::string_view lineSourceId(LineSource source) noexcept;

} // namespace hwio::device

Q_DECLARE_METATYPE(hwio::device::ConsoleLine)
