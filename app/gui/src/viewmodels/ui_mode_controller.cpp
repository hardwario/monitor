#include "viewmodels/ui_mode_controller.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "settings_store.h"
#include "viewmodels/ui_mode.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <optional>

namespace hwio::gui {

UiModeController::UiModeController(SettingsStore& store, QObject* parent)
    : QObject(parent), m_store(store) {
    const std::optional<UiMode> stored = parseUiMode(m_store.value(modeKey).toString());
    m_chosen = stored.has_value();
    m_mode = stored.value_or(preselection());
    connect(&m_store, &SettingsStore::valueChanged, this, [this](const QString& key) {
        if (key == modeKey) {
            reload();
        }
    });
}

UiMode UiModeController::preselection() const {
    return m_store.value(legacyExpertKey).toBool() ? UiMode::Expert : UiMode::Advanced;
}

QString UiModeController::name(UiMode mode) {
    switch (mode) {
    case UiMode::Basic:
        return tr("Basic");
    case UiMode::Expert:
        return tr("Expert");
    case UiMode::Advanced:
        break;
    }
    return tr("Advanced");
}

QString UiModeController::description(UiMode mode) {
    switch (mode) {
    case UiMode::Basic:
        return tr("The essentials: language, look and console files.");
    case UiMode::Expert:
        return tr("Everything, including timing options and expert maintenance actions.");
    case UiMode::Advanced:
        break;
    }
    return tr("Also J-Link, nRF Util, AI assistants and console options.");
}

QVariantList UiModeController::modes() const {
    QVariantList list;
    for (const UiMode mode : std::array{UiMode::Basic, UiMode::Advanced, UiMode::Expert}) {
        list.append(QVariantMap{{QStringLiteral("mode"), static_cast<int>(mode)},
                                {QStringLiteral("name"), name(mode)},
                                {QStringLiteral("description"), description(mode)}});
    }
    return list;
}

Status UiModeController::choose(UiMode mode) {
    const bool wasChosen = m_chosen;
    m_chosen = true; // for this run, even when the write fails (not asked again)
    Status status = m_store.setValue(modeKey, uiModeId(mode)); // valueChanged → reload()
    if (status) {
        // ADR-0360: the rc.3 switch is replaced by the mode.
        if (const Status removed = m_store.removeGroup(legacyExpertKey); !removed) {
            qCWarning(lcGui) << "UiMode: could not remove" << legacyExpertKey
                             << toQString(removed.error().detail);
        }
    } else {
        qCWarning(lcGui) << "UiMode: could not store" << modeKey
                         << toQString(status.error().detail);
        writeFailed(toQString(status.error().message)); // signal
    }
    apply(mode);
    if (!wasChosen) {
        chosenChanged(); // signal
    }
    return status;
}

void UiModeController::chooseMode(int mode) {
    if (const std::optional<UiMode> value = uiModeFromInt(mode); value) {
        (void)choose(*value); // a failure is reported by writeFailed
    }
}

void UiModeController::reload() {
    const std::optional<UiMode> stored = parseUiMode(m_store.value(modeKey).toString());
    const bool wasChosen = m_chosen;
    m_chosen = stored.has_value();
    apply(stored.value_or(preselection()));
    if (m_chosen != wasChosen) {
        chosenChanged(); // signal
    }
}

void UiModeController::retranslate() {
    textsChanged(); // signal
}

void UiModeController::apply(UiMode mode) {
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    qCInfo(lcGui).noquote() << "UiMode:" << uiModeId(mode);
    modeChanged(); // signal
}

} // namespace hwio::gui
