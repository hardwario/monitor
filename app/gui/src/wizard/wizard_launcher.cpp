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
                               std::function<DeviceTabViewModel*(int)> tabAt, QObject* parent)
    : QObject(parent), m_registry(registry), m_global(global), m_tabAt(std::move(tabAt)) {}

bool WizardLauncher::canOpen(const QString& wizardId) const {
    return !wizardId.isEmpty() && m_registry.has(wizardId);
}

bool WizardLauncher::open(const QString& wizardId, const ErrorInfo& context, int tabIndex) {
    return launch(wizardId, context, m_tabAt ? m_tabAt(tabIndex) : nullptr);
}

bool WizardLauncher::start(const QString& wizardId, int tabIndex) {
    return open(wizardId, ErrorInfo(), tabIndex);
}

bool WizardLauncher::openForTab(const QString& wizardId, DeviceTabViewModel* tab,
                                const ErrorInfo& context) {
    return launch(wizardId, context, tab);
}

bool WizardLauncher::launch(const QString& wizardId, const ErrorInfo& context,
                            DeviceTabViewModel* tab) {
    const WizardDescriptor* descriptor = m_registry.find(wizardId);
    if (descriptor == nullptr) {
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "is not registered";
        return false;
    }
    if (descriptor->perTab && tab == nullptr) {
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "needs a tab";
        return false;
    }
    // Plan 0052 (ADR-0386): one wizard per tab, one global wizard.
    WizardViewModel& instance = descriptor->perTab ? *tab->wizard() : m_global;
    if (instance.isOpen()) {
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "refused:" << instance.wizardId()
                                << "is open";
        emit instance.raiseRequested(); // the open one is shown
        return false;
    }
    // Ownership: the factory creates the wizard with the WizardViewModel as Qt parent.
    Wizard* wizard = descriptor->create(&instance);
    return instance.start(wizard, context, descriptor->perTab ? tab : nullptr);
}

} // namespace hwio::gui
