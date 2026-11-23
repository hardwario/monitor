#pragma once

// WizardLauncher - opens wizards (plan/features/0007): "Help me fix this" in an ErrorPanel (the
// error's kebab wizard id + the error as context), the Help menu and
// DeviceTabViewModel::openWizard. A wizard whose id is not registered cannot be opened (canOpen =
// false: the UI hides the button). Plan 0052 (ADR-0386): a per-tab wizard opens in the tab's own
// WizardViewModel (DeviceTabViewModel::wizard(), one per tab), a global wizard in the global one
// (AppViewModel.globalWizard). When that instance has a wizard open already, the new one is refused
// and the open one is shown (WizardViewModel::raiseRequested). Owned by AppViewModel (Qt parent).
// GUI thread only.

#include "viewmodels/error_info.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

#include <functional>

namespace hwio::gui {

class DeviceTabViewModel;
class WizardRegistry;
class WizardViewModel;

class WizardLauncher : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

public:
    // `tabAt(-1)` is the current tab; nullptr when there is none. `global` hosts global wizards.
    WizardLauncher(const WizardRegistry& registry, WizardViewModel& global,
                   std::function<DeviceTabViewModel*(int tabIndex)> tabAt,
                   QObject* parent = nullptr);

    // Registered? (`wizardId` is the kebab id, e.g. "connect-device"; "" = false.)
    Q_INVOKABLE [[nodiscard]] bool canOpen(const QString& wizardId) const;
    // Opens the wizard with the error as context on the tab (-1 = the current tab; ignored for
    // global wizards). False when it is not registered, needs a tab and there is none, or a
    // wizard is open in the same instance already (that one is shown).
    Q_INVOKABLE bool open(const QString& wizardId, const hwio::gui::ErrorInfo& context,
                          int tabIndex = -1);
    // The same without an error (Help menu).
    Q_INVOKABLE bool start(const QString& wizardId, int tabIndex = -1);
    // For DeviceTabViewModel::openWizard.
    bool openForTab(const QString& wizardId, DeviceTabViewModel* tab);

private:
    bool launch(const QString& wizardId, const ErrorInfo& context, DeviceTabViewModel* tab);

    const WizardRegistry& m_registry;
    WizardViewModel& m_global;
    std::function<DeviceTabViewModel*(int)> m_tabAt;
};

} // namespace hwio::gui
