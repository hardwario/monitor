#include "hwio/mcp/tools/remote_hidden_view.h"

#include "hwio/device/console_line.h"
#include "hwio/device/console_redaction.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::mcp::tools {

namespace {

constexpr bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

// Replaces every occurrence of `needle` in `text` that is not next to another digit.
std::string replaceWhole(std::string text, std::string_view needle, std::string_view replacement) {
    if (needle.empty()) {
        return text;
    }
    std::size_t from = 0;
    while (true) {
        const std::size_t at = text.find(needle, from);
        if (at == std::string::npos) {
            return text;
        }
        const std::size_t end = at + needle.size();
        const bool before = at > 0 && isDigit(text[at - 1]);
        const bool after = end < text.size() && isDigit(text[end]);
        if (before || after) {
            from = at + 1;
            continue;
        }
        text.replace(at, needle.size(), replacement);
        from = at + replacement.size();
    }
}

} // namespace

RemoteHiddenView::RemoteHiddenView(hwio::device::ConsoleRedaction redaction, std::string_view code)
    : m_redaction(redaction) {
    for (const char c : code) {
        if (isDigit(c)) {
            m_plain += c;
        }
    }
    constexpr std::size_t groupedDigits = 6;
    if (m_plain.size() == groupedDigits) {
        m_grouped = m_plain.substr(0, 3) + " " + m_plain.substr(3);
    }
}

std::string RemoteHiddenView::scrub(std::string_view text) const {
    std::string out(text);
    out = replaceWhole(std::move(out), m_grouped, placeholder);
    return replaceWhole(std::move(out), m_plain, placeholder);
}

std::string RemoteHiddenView::text(const hwio::device::ConsoleLine& line) const {
    if (auto redacted = m_redaction.redacted(line)) {
        return scrub(*redacted);
    }
    return scrub(hwio::device::englishText(line));
}

} // namespace hwio::mcp::tools
