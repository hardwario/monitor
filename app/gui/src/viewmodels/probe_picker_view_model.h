#pragma once

// ProbePickerViewModel - the "Choose device" page of a tab (plan/features/0004-m3-gui-shell.md, B3;
// ADR-0050, ADR-0058).
//
// A list model of the probes of the latest ProbeScanner result. Per row: serial number, product,
// firmware date and who uses the probe: another tab of this window ("In use (tab 2)": choosing it
// switches to that tab) or another program ("In use by another program (hwio, PID 1234)": the row
// is disabled). The "in use by a tab" information is added here on the GUI thread. Exactly one
// free probe is preselected. Below the list: the device profile picker (shown only with more than
// one J-Link profile, plan 0033 D1) and Connect. ADR-0233: no target choice - a tab connects to
// the profile's console target (CHESTER: app).
//
// While `active` (the page is visible) the picker holds the scanner (2 s scans).
//
// Wizard mode (plan 0035, ADR-0252): the picker of a wizard's Embedded `probe-picker` step. Rows
// used by another tab are not selectable (a wizard never takes another tab's probe, ADR-0391),
// choose() never emits tabActivationRequested and connectSelected() does nothing; the wizard reads
// selectedSerial().
//
// Ownership: child of its DeviceTabViewModel, or of the wizard that created it with
// AppViewModel::makeProbePicker (Qt parent). GUI thread only.

#include "viewmodels/error_info.h"
#include "workers/probe_scanner.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace hwio::gui {

class ProbePickerViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DeviceTabViewModel or a wizard")

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool scanned READ scanned NOTIFY scanChanged)
    Q_PROPERTY(QString profileId READ profileId WRITE setProfileId NOTIFY profileIdChanged)
    Q_PROPERTY(QVariantList profiles READ profiles CONSTANT)
    // Plan 0033 (D1): more than one J-Link profile (the Device picker is shown), and the name of
    // the chosen one (shown as text otherwise).
    Q_PROPERTY(bool profileChoice READ profileChoice CONSTANT)
    Q_PROPERTY(QString profileName READ profileName NOTIFY profileIdChanged)
    // ADR-0233: the console target of the chosen profile.
    Q_PROPERTY(QString targetId READ targetId NOTIFY profileIdChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectedRowChanged)
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY selectedRowChanged)
    Q_PROPERTY(hwio::gui::ErrorInfo libraryError READ libraryError NOTIFY scanChanged)
    Q_PROPERTY(QString libraryVersion READ libraryVersion NOTIFY scanChanged)

public:
    // Unscoped: Qt model roles are ints.
    enum Roles : quint16 {             // NOLINT(cppcoreguidelines-use-enum-class)
        SerialRole = Qt::UserRole + 1, // QString (decimal)
        ProductRole,                   // "J-Link" when the library does not say
        FirmwareRole,                  // "2024-03-07" or ""
        InUseTabRole,                  // 1-based tab number, -1 = none
        InUseOtherRole,                // "hwio, PID 1234" or ""
        SelectableRole,                // false when used by another program (wizard mode: or tab)
        UpdatingFirmwareRole,          // plan 0036: reserved for a J-Link firmware update
    };
    Q_ENUM(Roles)

    // `tabUsingProbe(sn)`: the 0-based index of another tab that uses the probe, or -1.
    ProbePickerViewModel(ProbeScanner* scanner, std::function<int(std::uint32_t)> tabUsingProbe,
                         QObject* parent);
    ~ProbePickerViewModel() override; // releases the scanner when active
    ProbePickerViewModel(const ProbePickerViewModel&) = delete;
    ProbePickerViewModel& operator=(const ProbePickerViewModel&) = delete;
    ProbePickerViewModel(ProbePickerViewModel&&) = delete;
    ProbePickerViewModel& operator=(ProbePickerViewModel&&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool active() const noexcept { return m_active; }
    void setActive(bool active);
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] bool scanned() const noexcept { return m_scanned; }
    [[nodiscard]] QString profileId() const { return m_profileId; }
    void setProfileId(const QString& id);
    [[nodiscard]] static QVariantList profiles();
    [[nodiscard]] QString targetId() const;
    [[nodiscard]] static bool profileChoice();
    [[nodiscard]] QString profileName() const;
    [[nodiscard]] int selectedRow() const noexcept { return m_selectedRow; }
    void setSelectedRow(int row);
    [[nodiscard]] bool canConnect() const;
    [[nodiscard]] ErrorInfo libraryError() const { return m_libraryError; }
    [[nodiscard]] QString libraryVersion() const { return m_libraryVersion; }

    // Wizard mode (ADR-0252): set once by the creator, before the picker is shown.
    void setWizardMode(bool wizardMode);
    [[nodiscard]] bool wizardMode() const noexcept { return m_wizardMode; }
    // The serial number of the selected row; nullopt when none is selected.
    [[nodiscard]] std::optional<std::uint32_t> selectedSerial() const;

    // A click on a row: selects it, or switches to the tab that uses the probe
    // (tabActivationRequested).
    Q_INVOKABLE void choose(int row);
    Q_INVOKABLE void refresh();
    // Emits connectRequested for the selected row (when it can connect).
    Q_INVOKABLE void connectSelected();

    // Plan 0036: `reserved(sn)` is true while the probe is reserved for a J-Link firmware update
    // (SessionRegistry::reserved): the row is not selectable ("Updating firmware…").
    void setReservedCheck(std::function<bool(std::uint32_t)> reserved);

    // Re-reads the scanner's latest result (also after another tab changed its probe).
    void updateFromScanner();
    void retranslate();

    // "J-Link V11 compiled Sep 21 2023 10:38:43" -> "2023-09-21"; "" when it cannot be read.
    [[nodiscard]] static QString firmwareDate(const QString& firmware);
    // Translated long target title with the profile's chips (0021, ADR-0168): "Application MCU
    // (nRF52840)", "LTE MCU (nRF9151 / nRF9160)"; the id for unknown targets.
    [[nodiscard]] static QString targetTitle(const QString& profileId, const QString& targetId);

signals:
    void activeChanged();
    void countChanged();
    void scanChanged();
    void profileIdChanged();
    void selectedRowChanged();
    void tabActivationRequested(int tabIndex);
    void connectRequested(std::uint32_t serialNumber, const QString& product,
                          const QString& profileId, const QString& targetId);

private:
    struct Row {
        std::uint32_t serial = 0;
        QString product;
        QString firmware;
        int inUseTab = -1; // 0-based
        QString inUseOther;
        bool updating = false; // plan 0036

        [[nodiscard]] bool operator==(const Row& other) const = default;
    };

    [[nodiscard]] std::vector<Row> rowsOf(const ProbeScan& scan) const;
    // Not used by another program, not being updated and (wizard mode) not used by another tab.
    [[nodiscard]] bool selectable(const Row& row) const;
    void setRows(std::vector<Row> rows);
    void restoreSelection();
    void apply(const ProbeScan& scan);
    void preselect();

    ProbeScanner* m_scanner; // non-owning, owned by AppViewModel (outlives the tabs)
    std::function<int(std::uint32_t)> m_tabUsingProbe;
    std::function<bool(std::uint32_t)> m_reserved; // plan 0036; may be empty
    std::vector<Row> m_rows;
    std::optional<std::uint32_t> m_selectedSerial;
    int m_selectedRow = -1;
    bool m_active = false;
    bool m_wizardMode = false;
    bool m_scanned = false;
    std::uint64_t m_scanSequence = 0;
    ErrorInfo m_libraryError;
    QString m_libraryVersion;
    QString m_profileId;
};

} // namespace hwio::gui
