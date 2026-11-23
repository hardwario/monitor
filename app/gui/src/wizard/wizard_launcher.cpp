#include "wizard/wizard_launcher.h"

#include "error_info.h"
#include "gui_log.h"
#include "viewmodels/device_tab_view_model.h"
#include "wizard.h"
#include "wizard/wizard_registry.h"
#include "wizard/wizard_view_model.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QtCore/qtmetamacros.h>

#include <functional>
#include <utility>

namespace hwio::gui {

WizardLauncher::WizardLauncher(const WizardRegistry& registry, WizardViewModel& global,
                               std::function<DeviceTabViewModel*(int)> tabAt,
                               std::function<void(const QString&)> toast, QObject* parent)
    : QObject(parent), m_registry(registry), m_global(global), m_tabAt(std::move(tabAt)),
      m_toast(std::move(toast)) {}

QString WizardLauncher::localDeviceOnlyText() {
    return tr("This wizard works only with a device connected to this computer, not in a support "
              "session.");
}

QString WizardLauncher::setupOpenText() { return tr("Finish or close the setup wizard first."); }

bool WizardLauncher::canOpen(const QString& wizardId) const {
    return !wizardId.isEmpty() && m_registry.has(wizardId);
}

bool WizardLauncher::canStart(const QString& wizardId, int tabIndex) const {
    return refusal(wizardId, m_tabAt ? m_tabAt(tabIndex) : nullptr, false) == Refusal::None;
}

bool WizardLauncher::open(const QString& wizardId, const ErrorInfo& context, int tabIndex) {
    return launch(wizardId, context, m_tabAt ? m_tabAt(tabIndex) : nullptr);
}

bool WizardLauncher::start(const QString& wizardId, int tabIndex) {
    return open(wizardId, ErrorInfo(), tabIndex);
}

bool WizardLauncher::openForTab(const QString& wizardId, DeviceTabViewModel* tab,
                                const ErrorInfo& context, bool fromRemote) {
    return launch(wizardId, context, tab, fromRemote);
}

WizardLauncher::Refusal WizardLauncher::refusal(const QString& wizardId,
                                                const DeviceTabViewModel* tab,
                                                bool fromRemote) const {
    const WizardDescriptor* descriptor = m_registry.find(wizardId);
    if (descriptor == nullptr) {
        return Refusal::NotRegistered;
    }
    if (!descriptor->perTab) {
        return m_global.isOpen() ? Refusal::Busy : Refusal::None;
    }
    if (tab == nullptr) {
        return Refusal::NoTab;
    }
    if (descriptor->localDeviceOnly && tab->isRemote()) {
        return Refusal::Remote; // ADR-0393
    }
    if (m_global.isOpen() && !fromRemote) {
        return Refusal::SetupOpen; // ADR-0391 (plan 0056 D-6)
    }
    return tab->wizard()->isOpen() ? Refusal::Busy : Refusal::None;
}

bool WizardLauncher::launch(const QString& wizardId, const ErrorInfo& context,
                            DeviceTabViewModel* tab, bool fromRemote) {
    const WizardDescriptor* descriptor = m_registry.find(wizardId);
    switch (refusal(wizardId, tab, fromRemote)) {
    case Refusal::None:
        break;
    case Refusal::NotRegistered:
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "is not registered";
        return false;
    case Refusal::NoTab:
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "needs a tab";
        return false;
    case Refusal::Remote:
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "refused: a remote-support tab";
        if (m_toast) {
            m_toast(localDeviceOnlyText());
        }
        return false;
    case Refusal::SetupOpen:
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "refused: the setup wizard is open";
        if (m_toast) {
            m_toast(setupOpenText());
        }
        emit m_global.raiseRequested(); // the setup wizard stays shown
        return false;
    case Refusal::Busy: {
        // Plan 0052 (ADR-0386): one wizard per tab, one global wizard; the open one is shown.
        WizardViewModel& busy = descriptor->perTab ? *tab->wizard() : m_global;
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "refused:" << busy.wizardId()
                                << "is open";
        emit busy.raiseRequested();
        return false;
    }
    }
    WizardViewModel& instance = descriptor->perTab ? *tab->wizard() : m_global;
    // Ownership: the factory creates the wizard with the WizardViewModel as Qt parent.
    Wizard* wizard = descriptor->create(&instance);
    return instance.start(wizard, context, descriptor->perTab ? tab : nullptr);
}

} // namespace hwio::gui
