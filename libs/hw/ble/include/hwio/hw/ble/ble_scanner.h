#pragma once

// BleScanner - one scan of nearby BLE devices, as a list for the CLI and the GUI picker
// (ADR-0083, ADR-0129; plan/features/0011).
//
// Rows are de-duplicated by device id (never by name: legacy bug), hold at most maxDevices (the
// one seen least recently is evicted), names are sanitised (hwio::sanitizeDeviceText, ADR-0183)
// and cut at maxNameBytes (UTF-8 boundary), and rows are sorted by the smoothed RSSI (strongest
// first; plan 0033, ADR-0235): per device a time-based exponential moving average
//   s += (r - s) * (1 - e^(-dt / rssiTimeConstant)), the first sample = its value,
// where dt is the time between the advertisements that carried an RSSI (Advertisement::seen, so
// tests drive it with their own time points). RSSI 0 (unknown) is not a sample. Which
// profile a device belongs to is decided by the injected matcher (libs/device:
// ProfileRegistry::findByAdvertisement); without `all`, devices without a profile
// are left out. devicesChanged() is emitted at most every signalInterval, finished() once per
// start() when the scan ended (its duration passed, stop() was not called) or failed.
//
// Thread safety: owner thread only (a facade on BleHost, ADR-0080).

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/hw/ble/ble_host.h"
#include "hwio/hw/ble/types.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hwio::ble {

// The id of the profile an advertisement belongs to, "" for none.
using ProfileMatcher = std::function<std::string(const Advertisement&)>;

struct ScanOptions {
    std::chrono::milliseconds duration{5000}; // 0 = until stop()
    ProfileMatcher matcher;
    bool all = false; // keep devices without a profile too (`--all`, "Show all devices")
};

struct BleDeviceInfo {
    BleDeviceId id;
    std::string name; // <= BleScanner::maxNameBytes
    int rssi = 0;     // the last measured value (dBm), 0 = unknown
    // Plan 0033 (ADR-0235): the smoothed RSSI (dBm), 0 = no sample yet; the time of its last
    // sample.
    double rssiSmoothed = 0;
    std::chrono::steady_clock::time_point rssiSampled;
    bool nusAdvertised = false;
    std::string profileId; // "" = no profile
    std::chrono::steady_clock::time_point lastSeen;
};

class BleScanner : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t maxDevices = 256;
    static constexpr std::size_t maxNameBytes = 64;
    static constexpr std::chrono::milliseconds signalInterval{250}; // <= 4 signals per second
    // Plan 0033 (D5, ADR-0235): the time constant of the RSSI smoothing.
    static constexpr std::chrono::milliseconds rssiTimeConstant{3000};

    explicit BleScanner(BleHost& host, QObject* parent = nullptr);
    ~BleScanner() override;
    BleScanner(const BleScanner&) = delete;
    BleScanner& operator=(const BleScanner&) = delete;
    BleScanner(BleScanner&&) = delete;
    BleScanner& operator=(BleScanner&&) = delete;

    // Starts (or restarts) a scan; the list is kept. Errors: those of BleHost::post().
    [[nodiscard]] Status start(ScanOptions options);
    void stop();
    void clear();
    [[nodiscard]] bool isScanning() const noexcept { return m_scanning; }
    // Sorted by the smoothed RSSI, strongest first (unknown last).
    [[nodiscard]] const std::vector<BleDeviceInfo>& devices() const noexcept { return m_devices; }

signals:
    void devicesChanged(const QList<hwio::ble::BleDeviceInfo>& devices);
    void finished(const std::optional<hwio::Error>& error);

private:
    Q_INVOKABLE void drainEvents();
    void add(const Advertisement& advertisement);
    void publish();

    BleHost* m_host; // non-owning; outlives the scanner
    std::shared_ptr<Subscription> m_subscription;
    std::atomic<bool> m_wakePending{false};
    ScanOptions m_options;
    bool m_scanning = false;
    bool m_dirty = false;
    std::vector<BleDeviceInfo> m_devices; // <= maxDevices
    QTimer m_signalTimer;
};

// One step of the RSSI smoothing (ADR-0235): `smoothed` moved towards `sample` by
// 1 - e^(-elapsed / rssiTimeConstant). A negative elapsed time counts as 0.
[[nodiscard]] double smoothRssi(double smoothed, int sample, std::chrono::nanoseconds elapsed);

// `text` cut to at most `maxBytes` bytes without splitting a UTF-8 character.
[[nodiscard]] std::string truncateUtf8(std::string text, std::size_t maxBytes);

} // namespace hwio::ble

Q_DECLARE_METATYPE(hwio::ble::BleDeviceInfo)
