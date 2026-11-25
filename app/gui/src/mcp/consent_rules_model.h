#pragma once

// ConsentRulesModel - the rows of Settings › AI assistants (MCP) › Permissions (plan/features/0055,
// ADR-0390, ADR-0101, ADR-0164): one row per consent category in the settings order
// (GuiConsentBroker::categories, always 5 rows), roles ruleId ("flash", "memory-write", ...),
// title, hint (a warning, "" when none) and allow (true = Always allow).
//
// The rows never change; GuiConsentBroker::rulesChanged compares each row's rule with the cached
// value and emits dataChanged (role allow) for the changed rows only, and retranslate() (a
// language switch) emits dataChanged for the texts. There is never a model reset, so the QML
// delegates of the other rows stay as they are (no flicker, the focus stays).
//
// The texts come from the controller (`Describer`), so they keep their translation context.
//
// Ownership: child of McpController (Qt parent); the broker outlives the model's use of it (the
// controller destroys the broker in its destructor, before its children; the model does not touch
// the broker in its own destructor). GUI thread only.

#include "hwio/mcp/consent.h"
#include "mcp/gui_consent_broker.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtCore/qnamespace.h>
#include <QtQmlIntegration>

#include <array>
#include <functional>

namespace hwio::gui {

class ConsentRulesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by McpController")

public:
    // Unscoped: Qt roles are ints.
    enum Roles : int { // NOLINT(performance-enum-size,cppcoreguidelines-use-enum-class)
        RuleIdRole = Qt::UserRole + 1, // the category id ("flash", "memory-write", ...)
        TitleRole,
        HintRole,  // "" when none (ADR-0164)
        AllowRole, // true = Always allow
    };

    struct Texts {
        QString title;
        QString hint;
    };
    // The translated texts of a category (called on every data() of a text role).
    using Describer = std::function<Texts(hwio::mcp::ConsentCategory)>;

    // `broker` must outlive every call into the model except its destruction.
    ConsentRulesModel(GuiConsentBroker& broker, Describer describer, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // After a language switch: dataChanged for the title and hint of every row.
    void retranslate();

private:
    // Compares the rules with the cached ones; dataChanged for each changed row.
    void refresh();

    GuiConsentBroker* m_broker; // non-owning, see the ownership note above
    Describer m_describer;
    // The cached rules, in the order of GuiConsentBroker::categories.
    std::array<bool, GuiConsentBroker::categories.size()> m_allow{};
};

} // namespace hwio::gui
