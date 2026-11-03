#pragma once

// BlePickerViewModel - the Bluetooth view of a tab's "Choose device" page
// (plan/features/0012-m5-ble-gui-pairing.md; ADR-0081, ADR-0083, ADR-0225, ADR-0226).
//
// A list model over the devices of the application's BleService scan. Rows are the devices of a
// profile with Bluetooth ("Show all devices": every device; rows without a profile are "Unknown
// device" and connect with the device type picked below), filtered by a case-insensitive substring
// of the name or id, sorted by RSSI (strongest first). The selection follows the device id, so it
// stays on its row when the order changes (legacy bug). A row is dimmed ("not seen recently")
// staleAfter after its last advertisement and removed after removeAfter. Per row: who uses the
// device - another tab of this window ("In use (tab 2)": choosing it switches to that tab) or
// another program ("In use by another program (hwio, PID 1234)": the row is disabled; read every
// ownerRefreshInterval). Exactly one connectable row is preselected. New rows are announced to
// screen readers at most once per announceInterval.
//
// While `active` (the Bluetooth view is visible) the picker holds a scan reference of the service.
//
// Ownership: child of its DeviceTabViewModel (Qt parent). GUI thread only.

#include "viewmodels/error_info.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration>
#include <QtTypes>

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace hwio::gui {

class BleService;

class BlePickerViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DeviceTabViewModel")

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    // The first scan period ended (the empty state may show).
    Q_PROPERTY(bool scanned READ scanned NOTIFY scanChanged)
    Q_PROPERTY(hwio::gui::ErrorInfo adapterError READ adapterError NOTIFY scanChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(bool showAll READ showAll WRITE setShowAll NOTIFY filterChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectedRowChanged)
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY selectedRowChanged)
    // The device type for rows without a profile (profiles with Bluetooth only).
    Q_PROPERTY(QString profileId READ profileId WRITE setProfileId NOTIFY profileIdChanged)
    Q_PROPERTY(QVariantList profiles READ profiles CONSTANT) // [{id, name}]
    // macOS: ids are CoreBluetooth UUIDs, shown only in the tooltip (ADR-0226).
    Q_PROPERTY(bool hidesIds READ hidesIds CONSTANT)
    // The profile's power hint for the empty state ("" = none).
    Q_PROPERTY(QString powerHint READ powerHint NOTIFY profileIdChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY profileIdChanged) // "CHESTER"
    // [Open Privacy settings] for ble.permission_denied (macOS).
    Q_PROPERTY(bool canOpenPrivacySettings READ canOpenPrivacySettings NOTIFY scanChanged)

public:
    static constexpr std::chrono::milliseconds staleAfter{15000};  // ADR-0226
    static constexpr std::chrono::milliseconds removeAfter{60000}; // ADR-0226
    static constexpr std::chrono::milliseconds announceInterval{5000};
    static constexpr std::chrono::milliseconds ageCheckInterval{1000};
    static constexpr std::chrono::milliseconds ownerRefreshInterval{2000};

    // Unscoped: Qt model roles are ints.
    enum Roles : quint16 {         // NOLINT(cppcoreguidelines-use-enum-class)
        IdRole = Qt::UserRole + 1, // the BleDeviceId
        NameRole,                  // the advertised name; "" when none
        RssiRole,                  // dBm, 0 = unknown
        SignalLevelRole,           // 0 (unknown) .. 4
        ProfileRole,               // profile id or "" (Unknown device)
        ProfileNameRole,           // "CHESTER" or "Unknown device"
        InUseTabRole,              // 1-based tab number, -1 = none
        InUseOtherRole,            // "hwio, PID 1234" or ""
        StaleRole,                 // not seen for staleAfter
        ConnectableRole,           // false when used by another program
    };
    Q_ENUM(Roles)

    struct Hooks {
        // The 0-based index of another tab that uses the device, or -1.
        std::function<int(const QString& deviceId)> tabUsingDevice;
        // "hwio, PID 1234" when another program holds the device's lock, "" otherwise.
        std::function<QString(const QString& deviceId)> otherProgram;
        // Test seam: the clock for the stale / removal ages (steady_clock::now by default).
        std::function<std::chrono::steady_clock::time_point()> now;
        // Opens a system URL (DeviceTabViewModel::openExternalUrl).
        std::function<void(const QUrl& url)> openUrl;
    };

    // `service` may be null (no Bluetooth list; tests of other parts).
    BlePickerViewModel(BleService* service, Hooks hooks, QObject* parent);
    ~BlePickerViewModel() override; // releases the scan when active
    BlePickerViewModel(const BlePickerViewModel&) = delete;
    BlePickerViewModel& operator=(const BlePickerViewModel&) = delete;
    BlePickerViewModel(BlePickerViewModel&&) = delete;
    BlePickerViewModel& operator=(BlePickerViewModel&&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool active() const noexcept { return m_active; }
    void setActive(bool active);
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] bool scanning() const;
    [[nodiscard]] bool scanned() const;
    [[nodiscard]] ErrorInfo adapterError() const { return m_adapterError; }
    [[nodiscard]] QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);
    [[nodiscard]] bool showAll() const noexcept { return m_showAll; }
    void setShowAll(bool showAll);
    [[nodiscard]] int selectedRow() const noexcept { return m_selectedRow; }
    void setSelectedRow(int row);
    [[nodiscard]] bool canConnect() const;
    [[nodiscard]] QString profileId() const { return m_profileId; }
    void setProfileId(const QString& id);
    [[nodiscard]] static QVariantList profiles();
    [[nodiscard]] static bool hidesIds();
    [[nodiscard]] QString powerHint() const;
    [[nodiscard]] QString profileName() const;
    [[nodiscard]] bool canOpenPrivacySettings() const;
    [[nodiscard]] QString selectedId() const { return m_selectedId.value_or(QString()); }

    // A click on a row: selects it, or switches to the tab that uses the device
    // (tabActivationRequested).
    Q_INVOKABLE void choose(int row);
    // Emits connectRequested for the selected row (when it can connect).
    Q_INVOKABLE void connectSelected();
    // Reads the adapter again at once (the ErrorPanel's Retry).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openPrivacySettings() const;
    // ADR-0226: the signal level of an RSSI (0 = unknown, 1..4).
    [[nodiscard]] static int signalLevel(int rssi);

    // Re-reads the service's devices (also after another tab changed its device).
    void update();
    void retranslate();

signals:
    void activeChanged();
    void countChanged();
    void scanChanged();
    void filterChanged();
    void selectedRowChanged();
    void profileIdChanged();
    void tabActivationRequested(int tabIndex);
    void connectRequested(const QString& deviceId, const QString& name, const QString& profileId);
    // A text for screen readers ("3 devices found"), at most once per announceInterval.
    void announce(const QString& text);

private:
    struct Row {
        QString id;
        QString name;
        int rssi = 0;
        QString profileId; // "" = no profile
        int inUseTab = -1; // 0-based
        QString inUseOther;
        bool stale = false;

        [[nodiscard]] bool operator==(const Row& other) const = default;
    };

    [[nodiscard]] std::chrono::steady_clock::time_point now() const;
    [[nodiscard]] std::vector<Row> buildRows();
    void setRows(std::vector<Row> rows);
    void restoreSelection();
    void preselect();
    void refreshOwners();
    void readAdapter();
    void maybeAnnounce(int added);

    BleService* m_service; // non-owning, owned by AppViewModel (outlives the tabs)
    Hooks m_hooks;
    std::vector<Row> m_rows;
    std::map<QString, QString> m_owners; // device id -> other program ("" none), last read
    std::optional<QString> m_selectedId;
    int m_selectedRow = -1;
    bool m_active = false;
    bool m_showAll = false;
    QString m_filterText;
    QString m_profileId;
    ErrorInfo m_adapterError;
    QTimer m_ageTimer;
    QTimer m_ownerTimer;
    QTimer m_announceTimer;
    int m_pendingAnnouncement = 0; // rows added since the last announcement
};

} // namespace hwio::gui
