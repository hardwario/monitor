#include "hwio/core/localized_text.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"

#include <QCoreApplication>
#include <QString>

#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hwio {

namespace {

// One multi-argument QString::arg() (review R13: a chained arg() would expand "%n" inside a
// value). QString::arg takes the arguments as a pack, so the count is dispatched here.
QString withArgs(const QString& text, std::span<const QString> args) {
    switch (args.size()) {
    case 0:
        return text;
    case 1:
        return text.arg(args[0]);
    case 2:
        return text.arg(args[0], args[1]);
    case 3:
        return text.arg(args[0], args[1], args[2]);
    case 4:
        return text.arg(args[0], args[1], args[2], args[3]);
    case 5:
        return text.arg(args[0], args[1], args[2], args[3], args[4]);
    case 6:
        return text.arg(args[0], args[1], args[2], args[3], args[4], args[5]);
    case 7:
        return text.arg(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
    case 8:
        return text.arg(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
    default:
        return text.arg(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7],
                        args[8]);
    }
}

std::string& translatedField(Error& error, ErrorText field) {
    switch (field) {
    case ErrorText::Cause:
        return error.cause;
    case ErrorText::Suggestion:
        return error.suggestion;
    case ErrorText::Message:
        break;
    }
    return error.message;
}

std::string& englishField(Error& error, ErrorText field) {
    switch (field) {
    case ErrorText::Cause:
        return error.causeEn;
    case ErrorText::Suggestion:
        return error.suggestionEn;
    case ErrorText::Message:
        break;
    }
    return error.messageEn;
}

} // namespace

LocalizedText localizedText(const char* context, const char* source,
                            std::initializer_list<TextArg> args, const char* disambiguation) {
    std::vector<QString> translatedArgs;
    std::vector<QString> englishArgs;
    translatedArgs.reserve(args.size());
    englishArgs.reserve(args.size());
    for (const TextArg& arg : args) {
        translatedArgs.push_back(arg.translated());
        englishArgs.push_back(arg.english());
    }
    return LocalizedText{
        .translated =
            withArgs(QCoreApplication::translate(context, source, disambiguation), translatedArgs),
        .english = withArgs(QString::fromUtf8(source), englishArgs)};
}

void setText(Error& error, ErrorText field, const char* context, const char* source,
             std::initializer_list<TextArg> args, const char* disambiguation) {
    setText(error, field, localizedText(context, source, args, disambiguation));
}

void setText(Error& error, ErrorText field, const LocalizedText& text) {
    translatedField(error, field) = text.translated.toStdString();
    englishField(error, field) = text.english.toStdString();
}

void setUntranslatedText(Error& error, ErrorText field, std::string text) {
    englishField(error, field) = text;
    translatedField(error, field) = std::move(text);
}

void clearText(Error& error, ErrorText field) {
    translatedField(error, field).clear();
    englishField(error, field).clear();
}

LocalizedText textOf(const Error& error, ErrorText field) {
    const auto pair = [](const std::string& translated, const std::string& english) {
        // An Error built without hwio::setText() (e.g. aggregate-initialized in a test) has no
        // English text: the translated one stands in, like in toJson(ErrorLanguage::English).
        return LocalizedText{.translated = toQString(translated),
                             .english = toQString(english.empty() ? translated : english)};
    };
    switch (field) {
    case ErrorText::Cause:
        return pair(error.cause, error.causeEn);
    case ErrorText::Suggestion:
        return pair(error.suggestion, error.suggestionEn);
    case ErrorText::Message:
        break;
    }
    return pair(error.message, error.messageEn);
}

} // namespace hwio
