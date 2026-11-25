#include "viewmodels/error_info.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/localized_text.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtTranslation>

namespace hwio::gui {

namespace error_action {

Value forCategory(ErrorCategory category) noexcept {
    switch (category) {
    case ErrorCategory::Probe:
    case ErrorCategory::Target:
        return Reconnect;
    case ErrorCategory::Firmware:
        return ChooseFile;
    case ErrorCategory::Dependency:
        return OpenSettings;
    case ErrorCategory::Io:
    case ErrorCategory::Network:
    case ErrorCategory::Timeout:
    case ErrorCategory::Internal:
        return Retry;
    case ErrorCategory::Usage:
    case ErrorCategory::Cancelled:
        return Dismiss;
    }
    return Dismiss;
}

} // namespace error_action

ErrorInfo::ErrorInfo(const Error& error)
    : m_valid(true), m_code(toQString(idOf(error.code))),
      m_category(toQString(categoryId(categoryOf(error.code)))),
      m_message(toQString(error.message)), m_cause(toQString(error.cause)),
      m_suggestion(toQString(error.suggestion)), m_messageEn(toQString(error.messageEn)),
      m_causeEn(toQString(error.causeEn)), m_suggestionEn(toQString(error.suggestionEn)),
      m_detail(toQString(error.detail)),
      // The kebab-case id comes from the JSON form, the single place that names wizards.
      m_wizard(toJson(error).value(QStringLiteral("wizard")).toString()),
      m_primaryAction(error_action::forCategory(categoryOf(error.code))) {}

QString ErrorInfo::settingsSection() const {
    if (m_code.startsWith(QStringLiteral("dependency.jlink"))) {
        return QStringLiteral("jlink");
    }
    if (m_code.startsWith(QStringLiteral("dependency.nrfutil"))) {
        return QStringLiteral("nrfutil");
    }
    return {};
}

QString ErrorInfo::detailsText() const {
    if (m_detail.isEmpty()) {
        return m_code;
    }
    return m_code + QLatin1Char('\n') + m_detail;
}

Error nonLocalFileError(const QUrl& url) {
    Error error =
        makeError(ErrorCode::IoWriteFailed,
                  "not a local file: " + url.toString(QUrl::RemoveUserInfo).toStdString());
    setText(error, ErrorText::Message, "hwio::gui::FileErrors",
            QT_TRANSLATE_NOOP("hwio::gui::FileErrors", "The file could not be saved."));
    setText(
        error, ErrorText::Cause, "hwio::gui::FileErrors",
        QT_TRANSLATE_NOOP("hwio::gui::FileErrors", "Only folders on this computer can be used."));
    setText(error, ErrorText::Suggestion, "hwio::gui::FileErrors",
            QT_TRANSLATE_NOOP("hwio::gui::FileErrors", "Choose a folder on this computer."));
    return error;
}

} // namespace hwio::gui
