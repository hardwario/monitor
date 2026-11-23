#pragma once

// UiMode - how much of the app the user sees: Basic, Advanced or Expert (plan/features/
// 0045-user-modes.md, ADR-0359). A GUI presentation setting (ui/mode); it hides Settings sections
// and rows and gates the expert maintenance actions (ADR-0360), it never changes a value.
//
// The values are ordered: a higher mode shows everything a lower one shows (atLeast). QML uses
// them as UiMode.Basic, UiMode.Advanced, UiMode.Expert (ints).

#include <QObject>
#include <QString>
#include <QStringView>
#include <QtQmlIntegration>
#include <QtTypes>

#include <optional>

namespace hwio::gui {

namespace ui_mode {
Q_NAMESPACE
QML_NAMED_ELEMENT(UiMode)

enum class UiMode : quint8 { Basic = 0, Advanced = 1, Expert = 2 };
Q_ENUM_NS(UiMode)

} // namespace ui_mode

using UiMode = ui_mode::UiMode;

// "basic" | "advanced" | "expert" → the mode; anything else → nullopt.
[[nodiscard]] std::optional<UiMode> parseUiMode(QStringView id);
// The stored id of a mode ("basic", "advanced", "expert").
[[nodiscard]] QString uiModeId(UiMode mode);
// An int from QML (UiMode.Basic, ...) → the mode; out of range → nullopt.
[[nodiscard]] std::optional<UiMode> uiModeFromInt(int value);

// Whether `current` shows what needs `needed` (Expert shows everything, Basic only Basic).
[[nodiscard]] constexpr bool atLeast(UiMode current, UiMode needed) noexcept {
    return static_cast<int>(current) >= static_cast<int>(needed);
}

} // namespace hwio::gui
