#include "mcp/consent_rules_model.h"

#include "hwio/mcp/consent.h"
#include "mcp/gui_consent_broker.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtTypes>

#include <cstddef>
#include <string_view>
#include <utility>

namespace hwio::gui {

ConsentRulesModel::ConsentRulesModel(GuiConsentBroker& broker, Describer describer, QObject* parent)
    : QAbstractListModel(parent), m_broker(&broker), m_describer(std::move(describer)) {
    for (std::size_t row = 0; row < m_allow.size(); ++row) {
        m_allow.at(row) = m_broker->alwaysAllowed(GuiConsentBroker::categories.at(row));
    }
    connect(m_broker, &GuiConsentBroker::rulesChanged, this, &ConsentRulesModel::refresh);
}

int ConsentRulesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_allow.size());
}

QVariant ConsentRulesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto row = static_cast<std::size_t>(index.row());
    const hwio::mcp::ConsentCategory category = GuiConsentBroker::categories.at(row);
    switch (role) {
    case RuleIdRole: {
        const std::string_view id = hwio::mcp::consentCategoryId(category);
        return QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
    }
    case TitleRole:
        return m_describer ? m_describer(category).title : QString();
    case HintRole:
        return m_describer ? m_describer(category).hint : QString();
    case AllowRole:
        return m_allow.at(row);
    default:
        return {};
    }
}

QHash<int, QByteArray> ConsentRulesModel::roleNames() const {
    return {{RuleIdRole, "ruleId"}, {TitleRole, "title"}, {HintRole, "hint"}, {AllowRole, "allow"}};
}

void ConsentRulesModel::retranslate() {
    dataChanged(index(0), index(rowCount() - 1), {TitleRole, HintRole}); // signal
}

void ConsentRulesModel::refresh() {
    for (std::size_t row = 0; row < m_allow.size(); ++row) {
        const bool allow = m_broker->alwaysAllowed(GuiConsentBroker::categories.at(row));
        if (allow != m_allow.at(row)) {
            m_allow.at(row) = allow;
            const QModelIndex changed = index(static_cast<int>(row));
            dataChanged(changed, changed, {AllowRole}); // signal
        }
    }
}

} // namespace hwio::gui
