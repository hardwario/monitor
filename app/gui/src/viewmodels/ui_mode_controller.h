#pragma once

// UiModeController - the user mode Basic / Advanced / Expert (plan/features/0045-user-modes.md;
// ADR-0359, ADR-0360, ADR-0362).
//
// Reads and writes ui/mode ("basic" | "advanced" | "expert"; missing or invalid = not chosen yet).
// Until the mode is chosen (the first-start dialog, ModeChoiceDialog.qml) `mode` is the
// preselection: Expert when the rc.3 switch advanced/expertActions was on (upgrading users,
// ADR-0362), otherwise Advanced. Choosing stores ui/mode and removes advanced/expertActions
// (ADR-0360). A failed write (read-only settings file) keeps the mode for this run and reports
// writeFailed (the Settings page shows it as its write error).
//
// Ownership: child of AppViewModel (Qt parent, created in its constructor). GUI thread only.

#include "hwio/core/result.h"
#include "viewmodels/ui_mode.h"

#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QVariantList>
#include <QtQmlIntegration>

namespace hwio::gui {

class SettingsStore;

class UiModeController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    // UiMode.Basic / Advanced / Expert (the preselection while !chosen).
    Q_PROPERTY(int mode READ modeValue NOTIFY modeChanged)
    // False until the mode is chosen (first start, upgrade from rc.3): Main.qml shows the dialog.
    Q_PROPERTY(bool chosen READ chosen NOTIFY chosenChanged)
    // [{mode, name, description}] in mode order, translated.
    Q_PROPERTY(QVariantList modes READ modes NOTIFY textsChanged)

public:
    static inline const QString modeKey = QStringLiteral("ui/mode");
    // The rc.3 switch "Show expert maintenance actions" (ADR-0236); read only for the preselection.
    static inline const QString legacyExpertKey = QStringLiteral("advanced/expertActions");

    // Reads ui/mode from `store` (whose keys registerShellKeys registered).
    UiModeController(SettingsStore& store, QObject* parent);

    [[nodiscard]] UiMode mode() const noexcept { return m_mode; }
    [[nodiscard]] int modeValue() const noexcept { return static_cast<int>(m_mode); }
    [[nodiscard]] bool chosen() const noexcept { return m_chosen; }
    [[nodiscard]] bool shows(UiMode needed) const noexcept { return atLeast(m_mode, needed); }
    // Expert when advanced/expertActions is on, otherwise Advanced (ADR-0362).
    [[nodiscard]] UiMode preselection() const;
    [[nodiscard]] QVariantList modes() const;
    // The description line of a mode (Settings › General, the first-start dialog).
    [[nodiscard]] static QString description(UiMode mode);
    [[nodiscard]] static QString name(UiMode mode);

    // Applies and stores the mode, marks it chosen and removes advanced/expertActions. The mode
    // applies for this run even when the write fails (the error is returned and reported).
    [[nodiscard]] Status choose(UiMode mode);
    // QML (the dialog, the segmented control): UiMode values; others are ignored.
    Q_INVOKABLE void chooseMode(int mode);

    // Reads ui/mode again (it changes through the store; tests reset it).
    void reload();
    // The language changed: modes() again.
    void retranslate();

signals:
    void modeChanged();
    void chosenChanged();
    void textsChanged();
    // A write of ui/mode failed; `message` is the error message (translated).
    void writeFailed(const QString& message);

private:
    void apply(UiMode mode);

    SettingsStore& m_store;
    UiMode m_mode = UiMode::Advanced;
    bool m_chosen = false;
};

} // namespace hwio::gui
