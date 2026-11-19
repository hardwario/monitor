#include "viewmodels/ui_mode.h"

#include <QString>
#include <QStringLiteral>
#include <QStringView>

#include <optional>

namespace hwio::gui {

std::optional<UiMode> parseUiMode(QStringView id) {
    if (id == u"basic") {
        return UiMode::Basic;
    }
    if (id == u"advanced") {
        return UiMode::Advanced;
    }
    if (id == u"expert") {
        return UiMode::Expert;
    }
    return std::nullopt;
}

QString uiModeId(UiMode mode) {
    switch (mode) {
    case UiMode::Basic:
        return QStringLiteral("basic");
    case UiMode::Expert:
        return QStringLiteral("expert");
    case UiMode::Advanced:
        break;
    }
    return QStringLiteral("advanced");
}

std::optional<UiMode> uiModeFromInt(int value) {
    switch (value) {
    case static_cast<int>(UiMode::Basic):
        return UiMode::Basic;
    case static_cast<int>(UiMode::Advanced):
        return UiMode::Advanced;
    case static_cast<int>(UiMode::Expert):
        return UiMode::Expert;
    default:
        return std::nullopt;
    }
}

} // namespace hwio::gui
