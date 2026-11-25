#include "host_output_reader.h"

#include "hwio/core/json.h"
#include "hwio/core/text_sanitizer.h"
#include "hwio/device/console_line.h"
#include "hwio/device/line_assembler.h"

#include <QByteArray>
#include <QChar>
#include <QString>

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::remote {

void classifyHostOutput(device::ConsoleLine& line) {
    line.level = device::parseZephyrLevel(line.text);
    const std::string_view shown = line.text;
    if (line.level == device::LogLevel::None &&
        device::isHexdumpLine(shown.starts_with('#') ? shown.substr(1) : shown)) {
        // ADR-0171 (as BleConsoleHandler and the '#' rule of LineAssembler): the continuation
        // lines of a log hexdump are log lines too, hidden by default like in a local tab.
        line.level = device::LogLevel::Hexdump;
    }
    line.channel =
        line.level != device::LogLevel::None ? device::Channel::Log : device::Channel::Shell;
}

HostOutputReader::HostOutputReader() = default;

std::vector<device::ConsoleLine> HostOutputReader::read(const QString& data, TimePoint now) {
    if (!m_stream && !m_lineMode && data.contains(QLatin1Char('\n'))) {
        m_stream = true; // ADR-0431: a host that sends pieces of the stream (the mobile app)
    }
    std::vector<device::ConsoleLine> out;
    if (!m_stream) {
        device::ConsoleLine line;
        line.dir = device::Direction::Out;
        line.time = now;
        line.text = sanitizeDeviceText(toStdString(data));
        classifyHostOutput(line);
        out.push_back(std::move(line));
        return out;
    }
    const QByteArray bytes = data.toUtf8();
    for (device::ConsoleLine& line : m_assembler.feed(
             std::as_bytes(std::span(bytes.constData(), static_cast<std::size_t>(bytes.size()))),
             now)) {
        classifyHostOutput(line);
        if (keep(line)) {
            out.push_back(std::move(line));
        }
    }
    // ADR-0082 as in the Bluetooth console: the prompt is not a line.
    if (device::isZephyrPrompt(m_assembler.partial())) {
        m_assembler.reset();
        m_afterPrompt = true;
    }
    return out;
}

void HostOutputReader::commandShown() noexcept { m_afterCommand = true; }

void HostOutputReader::hostHello() noexcept {
    m_lineMode = true;
    m_stream = false;
    restart();
}

void HostOutputReader::restart() noexcept {
    m_assembler.reset();
    m_afterCommand = false;
    m_afterPrompt = false;
}

void HostOutputReader::streamSeen() noexcept {
    if (!m_lineMode) {
        m_stream = true;
    }
}

bool HostOutputReader::keepHistoryLine(const device::ConsoleLine& line) noexcept {
    if (!m_stream) {
        return true;
    }
    if (line.channel == device::Channel::Shell && device::isZephyrPrompt(line.text)) {
        m_afterCommand = false;
        m_afterPrompt = true;
        return false;
    }
    return keep(line);
}

bool HostOutputReader::keep(const device::ConsoleLine& line) noexcept {
    if (line.channel != device::Channel::Shell) {
        return true;
    }
    // HIL H5 (as BleConsoleHandler): the shell answers a command with a new line first; that one
    // empty line - the first shell line after a command or a prompt - is not output.
    const bool afterCommand = std::exchange(m_afterCommand, false);
    const bool afterPrompt = std::exchange(m_afterPrompt, false);
    return !((afterCommand || afterPrompt) && line.text.empty());
}

} // namespace hwio::remote
