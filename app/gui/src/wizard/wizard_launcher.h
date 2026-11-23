#pragma once

// WizardLauncher - opens wizards (plan/features/0007): "Help me fix this" in an ErrorPanel (the
// error's kebab wizard id + the error as context), the Help menu and
// DeviceTabViewModel::openWizard. A wizard whose id is not registered cannot be opened (canOpen =
// false: the UI hides the button). Plan 0052 (ADR-0386): a per-tab wizard opens in the tab's own
// WizardViewModel (DeviceTabViewModel::wizard(), one per tab), a global wizard in the global one
// (AppViewModel.globalWizard). When that instance has a wizard open already, the new one is refused
// and the open one is shown (WizardViewModel::raiseRequested). Plan 0056: a wizard that needs a
// local device (WizardDescriptor::localDeviceOnly) is refused on a remote-support tab (ADR-0393),
// and a per-tab wizard is refused while the global setup wizard is open (ADR-0391 D-6; not a
// remote LTE update request, ADR-0363); both with a toast. Owned by AppViewModel (Qt parent).
// GUI thread only.

#include "viewmodels/error_info.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

#include <cstdint>
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
    // `tabAt(-1)` is the current tab (the last device tab while Settings is current, plan 0056
    // D-12); nullptr when there is none. `global` hosts global wizards. `toast` shows a refusal
    // (AppViewModel::showToast); may be empty (tests).
    WizardLauncher(const WizardRegistry& registry, WizardViewModel& global,
                   std::function<DeviceTabViewModel*(int tabIndex)> tabAt,
                   std::function<void(const QString&)> toast, QObject* parent = nullptr);

    // Registered? (`wizardId` is the kebab id, e.g. "connect-device"; "" = false.)
    Q_INVOKABLE [[nodiscard]] bool canOpen(const QString& wizardId) const;
    // Plan 0056: registered, has its tab, not refused (remote tab, setup open) and its instance is
    // free: launch() would open it now.
    Q_INVOKABLE [[nodiscard]] bool canStart(const QString& wizardId, int tabIndex = -1) const;
    // Opens the wizard with the error as context on the tab (-1 = the current tab; ignored for
    // global wizards). False when it is not registered, needs a tab and there is none, or a
    // wizard is open in the same instance already (that one is shown).
    Q_INVOKABLE bool open(const QString& wizardId, const hwio::gui::ErrorInfo& context,
                          int tabIndex = -1);
    // The same without an error (Help menu).
    Q_INVOKABLE bool start(const QString& wizardId, int tabIndex = -1);
    // For DeviceTabViewModel::openWizard and the hand-offs between wizards (ADR-0297: with the
    // error as context). `fromRemote`: support's request on a shared tab (ADR-0363), not refused
    // while the setup wizard is open.
    bool openForTab(const QString& wizardId, DeviceTabViewModel* tab,
                    const ErrorInfo& context = ErrorInfo(), bool fromRemote = false);

    // The refusal texts (toasts).
    [[nodiscard]] static QString localDeviceOnlyText();
    [[nodiscard]] static QString setupOpenText();

private:
    enum class Refusal : std::uint8_t { None, NotRegistered, NoTab, Remote, SetupOpen, Busy };
    [[nodiscard]] Refusal refusal(const QString& wizardId, const DeviceTabViewModel* tab,
                                  bool fromRemote) const;
    bool launch(const QString& wizardId, const ErrorInfo& context, DeviceTabViewModel* tab,
                bool fromRemote = false);

    const WizardRegistry& m_registry;
    WizardViewModel& m_global;
    std::function<DeviceTabViewModel*(int)> m_tabAt;
    std::function<void(const QString&)> m_toast;
};

} // namespace hwio::gui
