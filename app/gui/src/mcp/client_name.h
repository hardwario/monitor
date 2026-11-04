#pragma once

// The display form of an MCP client's self-declared clientInfo name / version (plan 0014 review
// M4, ADR-0183, ADR-0241): sanitised like device text (escape sequences and C0/C1 controls
// removed), line breaks and tabs as spaces, Unicode bidirectional controls (U+200E, U+200F,
// U+202A-U+202E, U+2066-U+2069) removed, white space simplified, and cut to maxClientNameChars
// characters (with "…"). Applied where the name enters the GUI: the consent broker (prompt,
// grants), the activity list and a tab's openedBy. Pure functions.

#include <QString>

#include <cstddef>
#include <string>
#include <string_view>

namespace hwio::gui {

inline constexpr qsizetype maxClientNameChars = 64;

[[nodiscard]] QString clientDisplayText(std::string_view raw);
// The same as UTF-8 (for the backend structs that keep std::string).
[[nodiscard]] std::string clientDisplayUtf8(std::string_view raw);

} // namespace hwio::gui
