#pragma once

// BleService - the application's Bluetooth access for the GUI
// (plan/features/0012-m5-ble-gui-pairing.md, ADR-0080, ADR-0081, ADR-0225).
//
// Owns the BleHost (0011) and the one BleScanner of the application. The host - and with it the BLE
// thread - is created on the first use (a Bluetooth picker became visible, a BLE tab connects), so
// users who never use Bluetooth never start the thread or see the macOS permission prompt.
//
// Scanning: pickers hold a scan reference while visible (acquireScan / releaseScan). While held,
// the adapter state is read every adapterPollInterval without blocking the GUI thread (a posted
// QueryAdapter whose reply is checked every replyCheckInterval, dropped after replyTimeout). A
// ready adapter is scanned in periods of scanPeriod, restarted until the last reference is released
// (the list is kept between periods); an adapter that cannot be used gives adapterError(), cleared
// when it becomes ready. An undecided permission (macOS) is asked for once on this thread (review
// B8 of 0011).
//
// Ownership: AppViewModel (a std::unique_ptr member destroyed after the tabs, before the session
// registry). GUI thread only.

#include "hwio/core/bounded_queue.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/hw/ble/ble_api.h"
#include "hwio/hw/ble/ble_host.h"
#include "hwio/hw/ble/ble_scanner.h"
#include "hwio/hw/ble/types.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace hwio::gui {

class BleService : public QObject {
    Q_OBJECT

public:
    static constexpr std::chrono::milliseconds scanPeriod{10000};         // ADR-0226
    static constexpr std::chrono::milliseconds adapterPollInterval{2000}; // plan 0012
    static constexpr std::chrono::milliseconds replyCheckInterval{50};
    static constexpr std::chrono::milliseconds replyTimeout{3000};

    explicit BleService(ble::BleHost::ApiFactory factory = ble::createBleApi,
                        QObject* parent = nullptr);
    ~BleService() override; // stops the scan and the host (joins the BLE thread)
    BleService(const BleService&) = delete;
    BleService& operator=(const BleService&) = delete;
    BleService(BleService&&) = delete;
    BleService& operator=(BleService&&) = delete;

    // A visible picker holds one reference; the scan runs while at least one is held.
    void acquireScan();
    void releaseScan();
    // Reads the adapter state at once (while a scan reference is held).
    void pollNow();
    [[nodiscard]] int scanReferences() const noexcept { return m_references; }
    // A scan period is running now.
    [[nodiscard]] bool scanning() const;
    // At least one scan period ended since the first reference was taken (the empty state may
    // show).
    [[nodiscard]] bool scanned() const noexcept { return m_scanned; }
    // Sorted by RSSI, strongest first (BleScanner, all devices: pickers filter by profile).
    [[nodiscard]] const std::vector<ble::BleDeviceInfo>& devices() const;
    // Why the adapter cannot be used now (nullopt: ready, or not known yet).
    [[nodiscard]] const std::optional<Error>& adapterError() const noexcept {
        return m_adapterError;
    }
    // The last adapter state read (nullopt before the first answer).
    [[nodiscard]] std::optional<ble::AdapterState> adapterState() const noexcept {
        return m_adapterState;
    }

    // The host, created (and its thread started on its first command) on the first call.
    [[nodiscard]] ble::BleHost& host();
    [[nodiscard]] bool hostCreated() const noexcept { return static_cast<bool>(m_host); }

    // App-initiated pairing (ADR-0084, ADR-0224): `done` is called on this thread with the result
    // (and whether the system had a bond already), or ble.pairing_failed "no result" after
    // pairTimeout; never when `context` was destroyed first. At most maxPendingPairings at a time.
    // Errors (returned, `done` not called): internal.queue_full, cancelled (stopped).
    using PairDone = std::function<void(const Status& status, bool alreadyPaired)>;
    static constexpr std::chrono::milliseconds pairTimeout{90000};
    static constexpr std::size_t maxPendingPairings = 8;
    [[nodiscard]] Status pair(const ble::BleDeviceId& device, QObject* context, PairDone done);

    // Stops scanning and the host (AppViewModel, after the tabs closed their sessions). Idempotent.
    void stop();

signals:
    void devicesChanged();
    // scanning(), scanned(), adapterError() or adapterState() changed.
    void stateChanged();

private:
    struct PendingPairing {
        std::shared_ptr<ble::Subscription> subscription;
        QPointer<QObject> context;
        PairDone done;
        std::chrono::steady_clock::time_point deadline;
    };

    Q_INVOKABLE void drainPairings();
    void pollAdapter();
    void checkReply();
    void onAdapter(const ble::AdapterInfo& adapter);
    void requestPermission();
    void startScanPeriod();
    void stopScan();
    void setAdapterError(std::optional<Error> error);
    void onScanFinished(const std::optional<Error>& error);

    ble::BleHost::ApiFactory m_factory;
    std::unique_ptr<ble::BleHost> m_host;
    std::unique_ptr<ble::BleScanner> m_scanner;              // destroyed before m_host
    std::shared_ptr<BoundedQueue<ble::AdapterInfo>> m_reply; // the pending QueryAdapter, or null
    std::chrono::steady_clock::time_point m_replyPosted;
    QTimer m_pollTimer;
    QTimer m_replyTimer;
    int m_references = 0;
    bool m_scanned = false;
    bool m_permissionAsked = false;
    bool m_errorFromScan = false; // adapterError() came from a failed scan, not the adapter state
    bool m_stopped = false;
    std::optional<ble::AdapterState> m_adapterState;
    std::optional<Error> m_adapterError;
    std::vector<PendingPairing> m_pairings; // <= maxPendingPairings
    QTimer m_pairTimer;                     // pairing deadlines
};

} // namespace hwio::gui
