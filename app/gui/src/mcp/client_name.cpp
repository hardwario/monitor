#include "mcp/client_name.h"

#include "hwio/core/json.h"
#include "hwio/core/text_sanitizer.h"

#include <QChar>
#include <QLatin1Char>
#include <QString>
#include <QtTypes>

#include <string>
#include <string_view>

namespace hwio::gui {

namespace {

bool bidiControl(char16_t c) {
    return c == 0x200E || c == 0x200F || (c >= 0x202A && c <= 0x202E) ||
           (c >= 0x2066 && c <= 0x2069);
}

bool lineBreak(char16_t c) { return c == u'\t' || c == 0x2028 || c == 0x2029; }

} // namespace

QString clientDisplayText(std::string_view raw) {
    // Line breaks become spaces before the sanitiser (which drops C0 controls).
    std::string bytes(raw);
    for (char& c : bytes) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    const QString sanitized = toQString(sanitizeDeviceText(bytes));
    QString text;
    text.reserve(sanitized.size());
    for (const QChar c : sanitized) {
        if (lineBreak(c.unicode())) {
            text.append(QLatin1Char(' '));
        } else if (!bidiControl(c.unicode())) {
            text.append(c);
        }
    }
    text = text.simplified();
    if (text.size() > maxClientNameChars) {
        text.truncate(maxClientNameChars - 1);
        if (!text.isEmpty() && text.back().isHighSurrogate()) {
            text.chop(1);
        }
        text.append(QChar(0x2026));
    }
    return text;
}

std::string clientDisplayUtf8(std::string_view raw) { return toStdString(clientDisplayText(raw)); }

} // namespace hwio::gui
