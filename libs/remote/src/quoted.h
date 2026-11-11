#pragma once

#include <format>
#include <string>
#include <string_view>

namespace hwio::remote::detail {

// `text` in double quotes for error messages: `"` and `\` escaped, control bytes and DEL as \xNN
// (a portable stand-in for std::format's "{:?}", which libstdc++ 14 lacks).
[[nodiscard]] inline std::string quoted(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
            out.push_back(ch);
        } else if (byte < 0x20U || byte == 0x7FU) {
            out += std::format("\\x{:02x}", byte);
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

} // namespace hwio::remote::detail
