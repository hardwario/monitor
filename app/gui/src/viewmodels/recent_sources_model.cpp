#include "viewmodels/recent_sources_model.h"

#include "gui_log.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "pages/firmware_services.h"
#include "settings_store.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QLoggingCategory>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QVariant>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace hwio::gui {

RecentSourcesModel::RecentSourcesModel(SettingsStore* settings, QObject* parent)
    : QAbstractListModel(parent), m_settings(settings) {
    if (m_settings != nullptr) {
        connect(m_settings, &SettingsStore::valueChanged, this, [this](const QString& key) {
            if (key == FirmwareServices::recentKey) {
                reload();
            }
        });
    }
}

int RecentSourcesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant RecentSourcesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Entry& entry = m_rows.at(static_cast<std::size_t>(index.row()));
    const bool catalog = entry.source.startsWith(catalogPrefix);
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        if (catalog && !entry.name.isEmpty()) {
            return entry.version.isEmpty() ? entry.name
                                           : QStringLiteral("%1 %2").arg(entry.name, entry.version);
        }
        return catalog ? tr("Catalog ID %1").arg(entry.source.mid(catalogPrefix.size()))
                       : QFileInfo(entry.source).fileName();
    case DetailRole:
        return catalog ? tr("Catalog ID %1").arg(entry.source.mid(catalogPrefix.size()))
                       : entry.source;
    case TypeRole:
        if (catalog) {
            return tr("Catalog ID");
        }
        return entry.source.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
                   ? tr("Zip archive")
                   : tr("HEX file");
    case DateRole:
        return QLocale().toString(entry.time.toLocalTime(), QLocale::ShortFormat);
    case SourceRole:
        return entry.source;
    default:
        return {};
    }
}

QHash<int, QByteArray> RecentSourcesModel::roleNames() const {
    return {{NameRole, "name"},
            {DetailRole, "detail"},
            {TypeRole, "typeText"},
            {DateRole, "dateText"},
            {SourceRole, "source"}};
}

void RecentSourcesModel::setKind(const QString& kindId) {
    if (kindId == m_kind) {
        return;
    }
    m_kind = kindId;
    reload();
}

std::vector<RecentSourcesModel::Entry> RecentSourcesModel::entries() const {
    std::vector<Entry> result;
    if (m_settings == nullptr) {
        return result;
    }
    const QJsonDocument document =
        QJsonDocument::fromJson(m_settings->value(FirmwareServices::recentKey).toString().toUtf8());
    if (!document.isArray()) {
        return result;
    }
    const QJsonArray array = document.array();
    for (const auto& value : array) {
        if (result.size() >= maxEntries) {
            break;
        }
        const QJsonObject object = value.toObject();
        QString kind = object.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("modem")) {
            kind = QStringLiteral("lte"); // 0021 (ADR-0174): the modem is part of LTE firmware
        }
        Entry entry{.kind = std::move(kind),
                    .source = object.value(QStringLiteral("source")).toString(),
                    .time = QDateTime::fromString(object.value(QStringLiteral("time")).toString(),
                                                  Qt::ISODate),
                    .name = object.value(QStringLiteral("name")).toString(),
                    .version = object.value(QStringLiteral("version")).toString()};
        if (!entry.kind.isEmpty() && !entry.source.isEmpty()) {
            result.push_back(std::move(entry));
        }
    }
    return result;
}

void RecentSourcesModel::add(const QString& kindId, const QString& source, const QString& name,
                             const QString& version) {
    std::vector<Entry> list = entries();
    Entry added{.kind = kindId,
                .source = source,
                .time = QDateTime::currentDateTimeUtc(),
                .name = name,
                .version = version};
    for (const Entry& entry : list) {
        if (name.isEmpty() && entry.source == source && !entry.name.isEmpty()) {
            added.name = entry.name; // known from before (ADR-0385)
            added.version = entry.version;
            break;
        }
    }
    std::erase_if(
        list, [&](const Entry& entry) { return entry.kind == kindId && entry.source == source; });
    list.insert(list.begin(), std::move(added));
    if (list.size() > maxEntries) {
        list.resize(maxEntries);
    }
    store(list);
}

void RecentSourcesModel::remove(int row) {
    if (row < 0 || row >= count()) {
        return;
    }
    const Entry removed = m_rows.at(static_cast<std::size_t>(row));
    std::vector<Entry> list = entries();
    std::erase_if(list, [&removed](const Entry& entry) {
        return entry.kind == removed.kind && entry.source == removed.source;
    });
    store(list);
}

void RecentSourcesModel::setName(const QString& source, const QString& name,
                                 const QString& version) {
    if (name.isEmpty()) {
        return;
    }
    std::vector<Entry> list = entries();
    bool changed = false;
    for (Entry& entry : list) {
        if (entry.source == source && (entry.name != name || entry.version != version)) {
            entry.name = name;
            entry.version = version;
            changed = true;
        }
    }
    if (changed) {
        store(list);
    }
}

QStringList RecentSourcesModel::unnamedCatalogIds() const {
    QStringList ids;
    for (const Entry& entry : m_rows) {
        if (entry.name.isEmpty() && entry.source.startsWith(catalogPrefix)) {
            ids.append(entry.source.mid(catalogPrefix.size()));
        }
    }
    return ids;
}

QString RecentSourcesModel::sourceAt(int row) const {
    return row >= 0 && row < count() ? m_rows.at(static_cast<std::size_t>(row)).source : QString();
}

void RecentSourcesModel::retranslate() {
    if (!m_rows.empty()) {
        dataChanged(index(0), index(count() - 1), {NameRole, TypeRole, DateRole}); // signal
    }
}

void RecentSourcesModel::store(const std::vector<Entry>& entries) {
    if (m_settings == nullptr) {
        return;
    }
    QJsonArray array;
    for (const Entry& entry : entries) {
        QJsonObject object{{QStringLiteral("kind"), entry.kind},
                           {QStringLiteral("source"), entry.source},
                           {QStringLiteral("time"), entry.time.toString(Qt::ISODate)}};
        if (!entry.name.isEmpty()) { // ADR-0385
            object.insert(QStringLiteral("name"), entry.name);
            object.insert(QStringLiteral("version"), entry.version);
        }
        array.append(object);
    }
    const QString json = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    if (const Status status = m_settings->setValue(FirmwareServices::recentKey, json); !status) {
        qCWarning(lcGui) << "Could not store the recent firmware sources:"
                         << toQString(status.error().message);
    }
    // valueChanged reloads every model (also this one).
}

void RecentSourcesModel::reload() {
    std::vector<Entry> rows;
    for (Entry& entry : entries()) {
        if (entry.kind == m_kind) {
            rows.push_back(std::move(entry));
        }
    }
    const bool countChange = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChange) {
        countChanged(); // signal
    }
}

} // namespace hwio::gui
