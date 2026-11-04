#pragma once

// ActivityModel - the MCP activity list of Settings › AI assistants (plan/features/0014, ADR-0105,
// ADR-0244): the last `maxEntries` tool calls of the GUI's MCP server, newest first, in memory
// only.
//
// It is the server's IActivitySink: McpServer records one ActivityEntry when a call ends (also
// reads, errors and cancellations). The entry's arguments are already shortened by the server;
// the token never reaches the sink (it is not a tool argument). `entryRecorded` tells the
// controller which client was active (active clients, plan 0014 "Limits").
//
// Ownership: child of McpController (Qt parent). GUI thread only.

#include "hwio/mcp/activity.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQmlIntegration>

#include <cstddef>
#include <deque>

namespace hwio::gui {

class ActivityModel : public QAbstractListModel, public hwio::mcp::IActivitySink {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by McpController")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    static constexpr std::size_t maxEntries = 1000; // ADR-0105

    // Unscoped: Qt roles are ints.
    enum Roles : int { // NOLINT(performance-enum-size,cppcoreguidelines-use-enum-class)
        TimeRole = Qt::UserRole + 1, // local "HH:mm:ss"
        ClientRole,                  // clientInfo.name ("?" when none)
        ToolRole,
        DeviceRole,    // "jlink:<SN>" or ""
        ArgumentsRole, // compact JSON, shortened by the server
        ResultRole,    // "ok" or an error id
        IsErrorRole,   // result != "ok"
        DurationRole,  // "0.3 s"
    };

    explicit ActivityModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(m_entries.size()); }

    // hwio::mcp::IActivitySink
    void record(hwio::mcp::ActivityEntry entry) override;

    // All rows as text, one line each, newest first ("14:02:11  Claude Code  flash  jlink:… ok
    // 12.4 s  {…}").
    Q_INVOKABLE [[nodiscard]] QString text() const;
    Q_INVOKABLE void clear();

    // The newest entry first (tests).
    [[nodiscard]] const hwio::mcp::ActivityEntry& entry(int row) const {
        return m_entries.at(static_cast<std::size_t>(row));
    }

    [[nodiscard]] static QString durationText(const hwio::mcp::ActivityEntry& entry);

signals:
    void countChanged();
    // After an entry was added (its client key; for the active-client count).
    void entryRecorded(const QString& clientKey);

private:
    [[nodiscard]] static QString lineOf(const hwio::mcp::ActivityEntry& entry);

    std::deque<hwio::mcp::ActivityEntry> m_entries; // newest first, at most maxEntries
};

} // namespace hwio::gui
