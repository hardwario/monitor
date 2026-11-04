#include "mcp/activity_model.h"

#include "hwio/core/json.h"
#include "hwio/mcp/activity.h"
#include "mcp/client_name.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QChar>
#include <QHash>
#include <QLocale>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QVariant>
#include <QtContainerFwd>

#include <cstddef>
#include <utility>

namespace hwio::gui {

ActivityModel::ActivityModel(QObject* parent) : QAbstractListModel(parent) {}

int ActivityModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QString ActivityModel::durationText(const hwio::mcp::ActivityEntry& entry) {
    const double seconds = static_cast<double>(entry.duration.count()) / 1000.0;
    return tr("%1 s").arg(QLocale().toString(seconds, 'f', 1));
}

QVariant ActivityModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const hwio::mcp::ActivityEntry& entry = m_entries.at(static_cast<std::size_t>(index.row()));
    switch (role) {
    case TimeRole:
        return entry.time.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
    case ClientRole:
        return entry.client.empty() ? QStringLiteral("?") : toQString(entry.client);
    case ToolRole:
        return toQString(entry.tool);
    case DeviceRole:
        return toQString(entry.deviceId);
    case ArgumentsRole:
        return toQString(entry.arguments);
    case ResultRole:
        return toQString(entry.result);
    case IsErrorRole:
        return entry.result != "ok";
    case DurationRole:
        return durationText(entry);
    default:
        return {};
    }
}

QHash<int, QByteArray> ActivityModel::roleNames() const {
    return {{TimeRole, "time"},       {ClientRole, "client"},       {ToolRole, "tool"},
            {DeviceRole, "device"},   {ArgumentsRole, "arguments"}, {ResultRole, "result"},
            {IsErrorRole, "isError"}, {DurationRole, "duration"}};
}

void ActivityModel::record(hwio::mcp::ActivityEntry entry) {
    entry.client = clientDisplayUtf8(entry.client); // review M4
    const QString clientKey = toQString(entry.clientKey);
    if (m_entries.size() >= maxEntries) {
        const int last = count() - 1;
        beginRemoveRows({}, last, last);
        m_entries.pop_back();
        endRemoveRows();
    }
    beginInsertRows({}, 0, 0);
    m_entries.push_front(std::move(entry));
    endInsertRows();
    countChanged();           // signal
    entryRecorded(clientKey); // signal
}

QString ActivityModel::lineOf(const hwio::mcp::ActivityEntry& entry) {
    QStringList fields{entry.time.toLocalTime().toString(QStringLiteral("HH:mm:ss")),
                       entry.client.empty() ? QStringLiteral("?") : toQString(entry.client),
                       toQString(entry.tool),
                       entry.deviceId.empty() ? QStringLiteral("-") : toQString(entry.deviceId),
                       toQString(entry.result),
                       durationText(entry)};
    if (!entry.arguments.empty()) {
        fields.append(toQString(entry.arguments));
    }
    return fields.join(QStringLiteral("  "));
}

QString ActivityModel::text() const {
    QStringList lines;
    lines.reserve(count());
    for (const hwio::mcp::ActivityEntry& entry : m_entries) {
        lines.append(lineOf(entry));
    }
    return lines.join(QLatin1Char('\n'));
}

void ActivityModel::clear() {
    if (m_entries.empty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
    countChanged(); // signal
}

} // namespace hwio::gui
