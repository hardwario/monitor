#pragma once

// A user-visible text in the UI language together with its English source (plan 0050, ADR-0374):
// hwio::Error keeps both, the GUI shows the translation, MCP sends the English text (ADR-0103).
//
// The translatable literal stays visible to lupdate at the call site through QT_TRANSLATE_NOOP:
//
//   setText(error, ErrorText::Message, "hwio::jlink",
//           QT_TRANSLATE_NOOP("hwio::jlink", "RTT buffer \"%1\" not found."), {toQString(name)});
//
// The context passed to the function must be the one in the macro.

#include "hwio/core/error.h"

#include <QString>

#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

namespace hwio {

// A translated text and its English source, both with the arguments filled in.
struct LocalizedText {
    QString translated;
    QString english;
};

// An argument of localizedText() / setText(): a value that is the same in both languages (a name,
// a path, a number) or a translated text with its English counterpart (a LocalizedText).
class TextArg {
public:
    // NOLINTBEGIN(google-explicit-constructor, hicpp-explicit-conversions): an argument list
    // ({name, 42, localized}) is the point of this type.
    TextArg(QString value) : m_translated(value), m_english(std::move(value)) {}
    TextArg(LocalizedText text)
        : m_translated(std::move(text.translated)), m_english(std::move(text.english)) {}
    template <std::integral T>
        requires(!std::same_as<T, bool> && !std::same_as<T, char>)
    TextArg(T value) : TextArg(QString::number(value)) {}
    // NOLINTEND(google-explicit-constructor, hicpp-explicit-conversions)

    [[nodiscard]] const QString& translated() const noexcept { return m_translated; }
    [[nodiscard]] const QString& english() const noexcept { return m_english; }

private:
    QString m_translated;
    QString m_english;
};

// QCoreApplication::translate(context, source, disambiguation) and the English `source`, each with
// the arguments filled in by one multi-argument QString::arg() (a value containing "%2" is not
// expanded again; at most 9 arguments).
[[nodiscard]] LocalizedText localizedText(const char* context, const char* source,
                                          std::initializer_list<TextArg> args = {},
                                          const char* disambiguation = nullptr);

// Which text of an hwio::Error setText() sets.
enum class ErrorText : std::uint8_t { Message, Cause, Suggestion };

// Sets error.<field> to the translation and error.<field>En to the English source, both with the
// arguments filled in (localizedText()).
void setText(Error& error, ErrorText field, const char* context, const char* source,
             std::initializer_list<TextArg> args = {}, const char* disambiguation = nullptr);

// Sets both texts of `field` (e.g. from another error's texts, or a LocalizedText built earlier).
void setText(Error& error, ErrorText field, const LocalizedText& text);

// Sets both texts of `field` to an English-only text (the CLI and MCP call sites, which have no
// translations, ADR-0027 / ADR-0103).
void setUntranslatedText(Error& error, ErrorText field, std::string text);

// Clears both texts of `field`.
void clearText(Error& error, ErrorText field);

// The texts of `field` as a LocalizedText (e.g. to use an error's message as an argument); an
// empty English text falls back to the translated one.
[[nodiscard]] LocalizedText textOf(const Error& error, ErrorText field);

} // namespace hwio
