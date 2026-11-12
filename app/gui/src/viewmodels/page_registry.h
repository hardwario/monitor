#pragma once

// PageRegistry - the page-extension contract of the GUI shell (plan/features/0004-m3-gui-shell.md,
// § Page-extension contract; ADR-0059).
//
// Pages (Console 0005, Flash 0006, the global Settings page 0006) and the sections of the global
// Settings page are registered once at startup, before the first tab exists. Every tab then
// creates the view models of the per-tab pages in registry order (DeviceTabViewModel), and
// AppViewModel creates one view model per global page. QML shows the pages in the rail (per-tab
// pages at the top, global pages at the bottom) and loads `qml` with the properties `tab` (the
// DeviceTabViewModel, null for global pages) and `vm` (the page view model).
//
// Ownership: a value member of AppViewModel. Thread safety: GUI thread only.

#include "viewmodels/ui_mode.h"

#include <QString>
#include <QUrl>

#include <functional>
#include <span>
#include <vector>

class QObject;

namespace hwio::gui {

class DeviceTabViewModel;

struct PageDescriptor {
    QString id;                       // "console", "flash", "settings"
    const char* titleSourceText = ""; // QT_TRANSLATE_NOOP("Pages", "Console"), translated live
    QString icon;                     // icon file name without ".svg" (ADR-0060)
    QUrl qml;                         // page root component; gets `tab` and `vm` properties
    int order = 0;                    // rail order (ascending)
    bool global = false;              // one instance for the app, shown at the rail bottom
    // Plan 0012: the page runs operations (flash, erase, ...): disabled in a tab whose transport
    // has none (Bluetooth), with DeviceTabViewModel::pageDisabledReason as the tooltip.
    bool needsOperations = false;
    // Creates the page view model; parent = the tab (or AppViewModel for global pages): Qt parent
    // ownership. May be empty (a page without a view model).
    std::function<QObject*(DeviceTabViewModel* tab /* nullptr for global pages */)> createViewModel;
};

struct SettingsSectionDescriptor {    // a section of the global Settings page (rendered by 0006)
    QString id;                       // "general", "console", "jlink", ...
    const char* titleSourceText = ""; // QT_TRANSLATE_NOOP("Pages", "Console")
    QUrl qml;                         // section component; binds to SettingsStore keys of its plan
    int order = 0;
    // Plan 0045 (ADR-0359): the lowest user mode that shows the section (its rows declare their
    // own minimum in QML, SettingsRow.minimumMode).
    UiMode minimumMode = UiMode::Basic;
};

class PageRegistry {
public:
    // Duplicate id: asserted in debug, ignored and logged in release. Kept sorted by `order`.
    void add(PageDescriptor page);
    void addSettingsSection(SettingsSectionDescriptor section);

    [[nodiscard]] std::span<const PageDescriptor> pages() const { return m_pages; }
    [[nodiscard]] std::span<const SettingsSectionDescriptor> settingsSections() const {
        return m_sections;
    }
    // nullptr when no page has this id.
    [[nodiscard]] const PageDescriptor* find(const QString& id) const;
    // nullptr when no settings section has this id.
    [[nodiscard]] const SettingsSectionDescriptor* findSettingsSection(const QString& id) const;

    // The page title in the current language.
    [[nodiscard]] static QString title(const PageDescriptor& page);
    [[nodiscard]] static QString title(const SettingsSectionDescriptor& section);

private:
    std::vector<PageDescriptor> m_pages;
    std::vector<SettingsSectionDescriptor> m_sections;
};

} // namespace hwio::gui
