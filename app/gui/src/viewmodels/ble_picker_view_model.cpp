#include "viewmodels/ble_picker_view_model.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/device/profile.h"
#include "hwio/hw/ble/ble_scanner.h"
#include "viewmodels/error_info.h"
#include "workers/ble_service.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>
#include <QtCore/qsystemdetection.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

const device::DeviceProfile* bleProfile(const QString& id) {
    const device::DeviceProfile* profile = device::ProfileRegistry::find(toStdString(id));
    return profile != nullptr && profile->ble ? profile : nullptr;
}

} // namespace

BlePickerViewModel::BlePickerViewModel(BleService* service, Hooks hooks, QObject* parent)
    : QAbstractListModel(parent), m_service(service), m_hooks(std::move(hooks)) {
    for (const device::DeviceProfile* profile : device::ProfileRegistry::all()) {
        if (profile->ble) {
            m_profileId = toQString(profile->id);
            break;
        }
    }
    m_ageTimer.setInterval(ageCheckInterval);
    connect(&m_ageTimer, &QTimer::timeout, this, &BlePickerViewModel::update);
    m_ownerTimer.setInterval(ownerRefreshInterval);
    connect(&m_ownerTimer, &QTimer::timeout, this, [this] {
        refreshOwners();
        update();
    });
    m_announceTimer.setSingleShot(true);
    m_announceTimer.setInterval(announceInterval);
    connect(&m_announceTimer, &QTimer::timeout, this, [this] {
        if (m_pendingAnnouncement > 0) {
            maybeAnnounce(0);
        }
    });
    if (m_service != nullptr) {
        connect(m_service, &BleService::devicesChanged, this, [this] {
            if (m_active) {
                update();
            }
        });
        connect(m_service, &BleService::stateChanged, this, [this] {
            readAdapter();
            scanChanged(); // signal
        });
    }
}

BlePickerViewModel::~BlePickerViewModel() {
    if (m_active && m_service != nullptr) {
        m_service->releaseScan();
    }
}

int BlePickerViewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant BlePickerViewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Row& row = m_rows.at(static_cast<std::size_t>(index.row()));
    switch (role) {
    case IdRole:
        return row.id;
    case NameRole:
        return row.name;
    case RssiRole:
        return row.rssi;
    case SignalLevelRole:
        return signalLevel(row.rssi);
    case ProfileRole:
        return row.profileId;
    case ProfileNameRole: {
        const device::DeviceProfile* profile =
            device::ProfileRegistry::find(toStdString(row.profileId));
        return profile != nullptr ? toQString(profile->displayName) : tr("Unknown device");
    }
    case InUseTabRole:
        return row.inUseTab < 0 ? -1 : row.inUseTab + 1;
    case InUseOtherRole:
        return row.inUseOther;
    case StaleRole:
        return row.stale;
    case ConnectableRole:
        return row.inUseOther.isEmpty();
    default:
        return {};
    }
}

QHash<int, QByteArray> BlePickerViewModel::roleNames() const {
    return {{IdRole, "deviceId"},       {NameRole, "name"},
            {RssiRole, "rssi"},         {SignalLevelRole, "signalLevel"},
            {ProfileRole, "profile"},   {ProfileNameRole, "profileName"},
            {InUseTabRole, "inUseTab"}, {InUseOtherRole, "inUseOther"},
            {StaleRole, "stale"},       {ConnectableRole, "connectable"}};
}

void BlePickerViewModel::setActive(bool active) {
    if (active == m_active) {
        return;
    }
    m_active = active;
    if (m_service != nullptr) {
        if (active) {
            m_service->acquireScan();
        } else {
            m_service->releaseScan();
        }
    }
    if (active) {
        m_ageTimer.start();
        m_ownerTimer.start();
        refreshOwners();
        readAdapter();
        update(); // the last list at once, the scan adds to it
    } else {
        m_ageTimer.stop();
        m_ownerTimer.stop();
    }
    activeChanged(); // signal
    scanChanged();   // signal
}

bool BlePickerViewModel::scanning() const { return m_service != nullptr && m_service->scanning(); }

bool BlePickerViewModel::scanned() const { return m_service != nullptr && m_service->scanned(); }

void BlePickerViewModel::readAdapter() {
    const std::optional<Error>& error =
        m_service != nullptr ? m_service->adapterError() : std::optional<Error>();
    m_adapterError = error ? ErrorInfo(*error) : ErrorInfo();
}

void BlePickerViewModel::setFilterText(const QString& text) {
    if (text == m_filterText) {
        return;
    }
    m_filterText = text;
    filterChanged(); // signal
    update();
}

void BlePickerViewModel::setShowAll(bool showAll) {
    if (showAll == m_showAll) {
        return;
    }
    m_showAll = showAll;
    filterChanged(); // signal
    update();
}

void BlePickerViewModel::setProfileId(const QString& id) {
    if (id == m_profileId || bleProfile(id) == nullptr) {
        return;
    }
    m_profileId = id;
    profileIdChanged();   // signal
    selectedRowChanged(); // signal: canConnect
}

QVariantList BlePickerViewModel::profiles() {
    QVariantList list;
    for (const device::DeviceProfile* profile : device::ProfileRegistry::all()) {
        if (profile->ble) {
            list.append(QVariantMap{{QStringLiteral("id"), toQString(profile->id)},
                                    {QStringLiteral("name"), toQString(profile->displayName)}});
        }
    }
    return list;
}

bool BlePickerViewModel::hidesIds() {
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

QString BlePickerViewModel::powerHint() const {
    const device::DeviceProfile* profile = bleProfile(m_profileId);
    return profile != nullptr ? device::powerHint(*profile) : QString();
}

QString BlePickerViewModel::profileName() const {
    const device::DeviceProfile* profile = bleProfile(m_profileId);
    return profile != nullptr ? toQString(profile->displayName) : QString();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): reads the error on macOS
bool BlePickerViewModel::canOpenPrivacySettings() const {
#ifdef Q_OS_MACOS
    return m_adapterError.valid() &&
           m_adapterError.code() == QStringLiteral("ble.permission_denied");
#else
    return false;
#endif
}

void BlePickerViewModel::refresh() {
    if (m_service != nullptr && m_active) {
        m_service->pollNow();
    }
}

void BlePickerViewModel::openPrivacySettings() const {
    if (m_hooks.openUrl) {
        m_hooks.openUrl(QUrl(QStringLiteral(
            "x-apple.systempreferences:com.apple.preference.security?Privacy_Bluetooth")));
    }
}

int BlePickerViewModel::signalLevel(int rssi) {
    // ADR-0229: the thresholds of the old app; 0 = unknown.
    constexpr int excellent = -55;
    constexpr int good = -67;
    constexpr int fair = -80;
    if (rssi == 0) {
        return 0;
    }
    if (rssi >= excellent) {
        return 4;
    }
    if (rssi >= good) {
        return 3;
    }
    if (rssi >= fair) {
        return 2;
    }
    return 1;
}

void BlePickerViewModel::setSelectedRow(int row) {
    if (row < -1 || row >= count()) {
        return;
    }
    if (row >= 0 && !m_rows.at(static_cast<std::size_t>(row)).inUseOther.isEmpty()) {
        return; // used by another program: disabled
    }
    m_selectedId =
        row >= 0 ? std::optional(m_rows.at(static_cast<std::size_t>(row)).id) : std::nullopt;
    if (row != m_selectedRow) {
        m_selectedRow = row;
    }
    selectedRowChanged(); // signal (also when the same row index now names another device)
}

bool BlePickerViewModel::canConnect() const {
    if (m_selectedRow < 0 || m_selectedRow >= count()) {
        return false;
    }
    const Row& row = m_rows.at(static_cast<std::size_t>(m_selectedRow));
    const QString profile = row.profileId.isEmpty() ? m_profileId : row.profileId;
    return row.inUseOther.isEmpty() && row.inUseTab < 0 && bleProfile(profile) != nullptr;
}

void BlePickerViewModel::choose(int row) {
    if (row < 0 || row >= count()) {
        return;
    }
    const Row& chosen = m_rows.at(static_cast<std::size_t>(row));
    if (chosen.inUseTab >= 0) {
        tabActivationRequested(chosen.inUseTab); // signal
        return;
    }
    setSelectedRow(row);
}

void BlePickerViewModel::connectSelected() {
    if (!canConnect()) {
        return;
    }
    const Row& row = m_rows.at(static_cast<std::size_t>(m_selectedRow));
    connectRequested(row.id, row.name,
                     row.profileId.isEmpty() ? m_profileId : row.profileId); // signal
}

std::chrono::steady_clock::time_point BlePickerViewModel::now() const {
    return m_hooks.now ? m_hooks.now() : std::chrono::steady_clock::now();
}

std::vector<BlePickerViewModel::Row> BlePickerViewModel::buildRows() {
    std::vector<Row> rows;
    if (m_service == nullptr) {
        return rows;
    }
    const auto current = now();
    for (const ble::BleDeviceInfo& device : m_service->devices()) {
        const auto age = current - device.lastSeen;
        if (age > removeAfter) {
            continue;
        }
        const QString profileId = toQString(device.profileId);
        if (!m_showAll && bleProfile(profileId) == nullptr) {
            continue;
        }
        Row row{.id = toQString(device.id),
                .name = toQString(device.name),
                .rssi = device.rssi,
                .profileId = profileId,
                .inUseTab = -1,
                .inUseOther = {},
                .stale = age > staleAfter};
        if (!m_filterText.isEmpty() && !row.name.contains(m_filterText, Qt::CaseInsensitive) &&
            !row.id.contains(m_filterText, Qt::CaseInsensitive)) {
            continue;
        }
        if (m_hooks.tabUsingDevice) {
            row.inUseTab = m_hooks.tabUsingDevice(row.id);
        }
        if (const auto owner = m_owners.find(row.id); owner != m_owners.end()) {
            row.inUseOther = owner->second;
        }
        rows.push_back(std::move(row));
    }
    return rows; // the scanner's order: RSSI, strongest first
}

void BlePickerViewModel::refreshOwners() {
    m_owners.clear();
    if (m_service == nullptr || !m_hooks.otherProgram) {
        return;
    }
    // Only the devices a picker could show (a lock-file check each).
    for (const ble::BleDeviceInfo& device : m_service->devices()) {
        if (m_showAll || !device.profileId.empty()) {
            const QString id = toQString(device.id);
            m_owners.emplace(id, m_hooks.otherProgram(id));
        }
    }
}

void BlePickerViewModel::update() {
    std::vector<Row> rows = buildRows();
    // Ids not in the owner cache yet (new devices): read now.
    if (m_hooks.otherProgram) {
        for (Row& row : rows) {
            if (!m_owners.contains(row.id)) {
                row.inUseOther = m_hooks.otherProgram(row.id);
                m_owners.emplace(row.id, row.inUseOther);
            }
        }
    }
    QSet<QString> known;
    for (const Row& row : m_rows) {
        known.insert(row.id);
    }
    int added = 0;
    for (const Row& row : rows) {
        if (!known.contains(row.id)) {
            ++added;
        }
    }
    setRows(std::move(rows));
    if (added > 0) {
        maybeAnnounce(added);
    }
}

void BlePickerViewModel::setRows(std::vector<Row> rows) {
    if (rows == m_rows) {
        return;
    }
    const int before = count();
    if (rows.size() == m_rows.size()) {
        // Same length: in place (keeps the delegates and with them the keyboard focus).
        m_rows = std::move(rows);
        if (!m_rows.empty()) {
            dataChanged(index(0), index(count() - 1)); // signal
        }
    } else {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    if (count() != before) {
        countChanged(); // signal
    }
    restoreSelection();
    preselect();
}

void BlePickerViewModel::restoreSelection() {
    int row = -1;
    if (m_selectedId) {
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).id == *m_selectedId && m_rows.at(i).inUseOther.isEmpty()) {
                row = static_cast<int>(i);
                break;
            }
        }
    }
    if (row < 0) {
        m_selectedId.reset(); // the device went away or is used by another program
    }
    if (row != m_selectedRow) {
        m_selectedRow = row;
    }
    selectedRowChanged(); // signal: canConnect may change with the row's badges
}

void BlePickerViewModel::preselect() {
    if (m_selectedId) {
        return;
    }
    int connectable = -1;
    int found = 0;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows.at(i);
        if (row.inUseOther.isEmpty() && row.inUseTab < 0 && !row.profileId.isEmpty()) {
            connectable = static_cast<int>(i);
            ++found;
        }
    }
    if (found == 1) {
        setSelectedRow(connectable);
    }
}

void BlePickerViewModel::maybeAnnounce(int added) {
    m_pendingAnnouncement += added;
    if (m_announceTimer.isActive() || m_pendingAnnouncement == 0 || !m_active) {
        return;
    }
    m_pendingAnnouncement = 0;
    announce(tr("%n device(s) found", "", count())); // signal
    m_announceTimer.start();
}

void BlePickerViewModel::retranslate() {
    if (!m_rows.empty()) {
        dataChanged(index(0), index(count() - 1), {ProfileNameRole}); // signal
    }
    profileIdChanged(); // signal: powerHint
    readAdapter();
    scanChanged(); // signal
}

} // namespace hwio::gui
