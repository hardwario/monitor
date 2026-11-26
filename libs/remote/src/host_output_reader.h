#pragma once

// HostOutputReader - how the joiner reads the device output of a remote host (plan 0061 amendment
// 1, A1.7; ADR-0431). Private to libs/remote; owned by RemoteConsoleSession (owner thread only).
//
// Hosts send the device output in two forms:
//   - lines (HARDWARIO Monitor, the web Terminal): every `at` `read` and every `sync` `outputs`
//     element is one line without CR or LF;
//   - pieces of the byte stream (the HARDWARIO mobile app): a `read` is one Bluetooth
//     notification (several lines, cut anywhere, escape sequences, the prompt without a line end).
//
// Line mode (the start, and for good after the host's hello): a `read` is one line, as before.
// Stream mode (for the rest of the session, entered when a `read` or a `sync` `outputs` element
// holds '\n'): the text goes through a device::LineAssembler (split at '\n', one trailing '\r'
// removed, each line sanitised; an unfinished line is kept for the next event, at most
// LineAssembler::maxPartialBytes). As in Monitor's own Bluetooth console (ADR-0082): an unfinished
// line that is a Zephyr prompt ("<name>:~$ ") is not a line, and the one empty line after a prompt
// or after a written command is not output. Output of a command typed on the phone comes without a
// `write`, so the prompt alone also starts that rule.

#include "hwio/device/console_line.h"
#include "hwio/device/line_assembler.h"

#include <QString>

#include <chrono>
#include <string_view>
#include <vector>

namespace hwio::remote {

// Level and channel of a host's device line (a Zephyr level tag or a hexdump continuation goes to
// the Log channel, ADR-0171), as Monitor's own consoles classify it.
void classifyHostOutput(device::ConsoleLine& line);

class HostOutputReader {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    HostOutputReader();

    // The data of an `at` `read`: the lines to append (Direction::Out, sanitised, classified).
    [[nodiscard]] std::vector<device::ConsoleLine> read(const QString& data, TimePoint now);
    // An `at` `write` (a command line) was shown: the next empty line is not output.
    void commandShown() noexcept;
    // The host's hello: a HARDWARIO Monitor host sends lines; line mode for the rest of the
    // session.
    void hostHello() noexcept;
    // A `sync` replaces the log: the unfinished line and the empty-line rule start again (the mode
    // stays).
    void restart() noexcept;
    // A `sync` whose `outputs` were in stream form (Transcript::streamForm()): stream mode unless
    // the host sent its hello.
    void streamSeen() noexcept;
    // A history line of a `sync` in stream mode: false when it is not output (a prompt, the one
    // empty line after a command or a prompt). In line mode always true.
    [[nodiscard]] bool keepHistoryLine(const device::ConsoleLine& line) noexcept;

    [[nodiscard]] bool streamMode() const noexcept { return m_stream; }

private:
    [[nodiscard]] bool keep(const device::ConsoleLine& line) noexcept;

    device::LineAssembler m_assembler{device::Channel::Shell, false};
    bool m_stream = false;
    bool m_lineMode = false; // the host's hello: never stream mode
    bool m_afterCommand = false;
    bool m_afterPrompt = false;
};

} // namespace hwio::remote
