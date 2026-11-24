#pragma once

// RemoteHiddenView - the hidden view of a joined remote tab's console for AI clients (plan 0062,
// ADR-0437), for implementations of IRemoteDevice::hiddenText() (the GUI's remote tab adapter).
//
// text(line): device::ConsoleRedaction of the host's profile first (secret commands, their echo,
// "<key>: <value>" and prefix outputs), then every occurrence of the session code - plain
// ("482913", as a whole number: not next to another digit) and grouped for reading aloud
// ("482 913") - becomes "<session>". Markers and log lines get the code replacement only (the
// redaction rules do not apply to them). The English text of a marker (ADR-0374) is the input.
//
// The view keeps the code to itself: nothing returns it. Short codes (a web Terminal host accepts 1
// to 6 digits) replace every whole number equal to them; hiding beats readability here.
//
// Immutable after construction; any thread.

#include "hwio/device/console_line.h"
#include "hwio/device/console_redaction.h"

#include <string>
#include <string_view>

namespace hwio::mcp::tools {

class RemoteHiddenView {
public:
    // `code`: the session code's digits ("482913"); characters other than digits are ignored, an
    // empty code replaces nothing.
    RemoteHiddenView(hwio::device::ConsoleRedaction redaction, std::string_view code);

    // The text of `line` (englishText) as an AI client may see it.
    [[nodiscard]] std::string text(const hwio::device::ConsoleLine& line) const;
    // `text` with the session code replaced (no redaction rules).
    [[nodiscard]] std::string scrub(std::string_view text) const;

    static constexpr std::string_view placeholder = "<session>";

private:
    hwio::device::ConsoleRedaction m_redaction;
    std::string m_plain;   // "482913"
    std::string m_grouped; // "482 913" (six digits only), else empty
};

} // namespace hwio::mcp::tools
