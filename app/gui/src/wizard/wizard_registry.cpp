#include "wizard/wizard_registry.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "wizard.h"

#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtContainerFwd>
#include <QtTypes>

#include <algorithm>
#include <utility>

namespace hwio::gui {

bool WizardRegistry::add(WizardDescriptor descriptor) {
    if (has(descriptor.id) || !descriptor.create) {
        return false;
    }
    m_wizards.push_back(std::move(descriptor));
    return true;
}

bool WizardRegistry::has(WizardId id) const {
    return std::ranges::any_of(m_wizards,
                               [id](const WizardDescriptor& wizard) { return wizard.id == id; });
}

bool WizardRegistry::has(const QString& id) const { return find(id) != nullptr; }

const WizardDescriptor* WizardRegistry::find(const QString& id) const {
    const auto wizard = wizardIdFromString(id.toStdString());
    if (!wizard) {
        return nullptr;
    }
    const auto it = std::ranges::find(m_wizards, *wizard, &WizardDescriptor::id);
    return it == m_wizards.end() ? nullptr : &*it;
}

bool WizardRegistry::addEmbedded(const QString& name, QUrl qml) {
    if (m_embedded.size() >= maxEmbedded || !embedded(name).isEmpty()) {
        return false;
    }
    m_embedded.emplace_back(name, std::move(qml));
    return true;
}

QUrl WizardRegistry::embedded(const QString& name) const {
    const auto it = std::ranges::find(m_embedded, name, &std::pair<QString, QUrl>::first);
    return it == m_embedded.end() ? QUrl() : it->second;
}

QStringList WizardRegistry::embeddedNames() const {
    QStringList names;
    names.reserve(static_cast<qsizetype>(m_embedded.size()));
    for (const auto& [name, url] : m_embedded) {
        names.append(name);
    }
    return names;
}

} // namespace hwio::gui
