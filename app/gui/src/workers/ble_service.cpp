#include "workers/ble_service.h"

#include "gui_log.h"
#include "hwio/core/bounded_queue.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/profile.h"
#include "hwio/hw/ble/ble_host.h"
#include "hwio/hw/ble/ble_scanner.h"
#include "hwio/hw/ble/types.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QtCore/qnamespace.h>
#include <QtCore/qtconfigmacros.h>

#if QT_CONFIG(permissions)
#include <QPermissions>
#endif

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace hwio::gui {

BleService::BleService(ble::BleHost::ApiFactory factory, QObject* parent)
    : QObject(parent), m_factory(std::move(factory)) {
    m_pollTimer.setInterval(adapterPollInterval);
    connect(&m_pollTimer, &QTimer::timeout, this, &BleService::pollAdapter);
    m_replyTimer.setInterval(replyCheckInterval);
    connect(&m_replyTimer, &QTimer::timeout, this, &BleService::checkReply);
    m_pairTimer.setInterval(std::chrono::seconds(1));
    connect(&m_pairTimer, &QTimer::timeout, this, &BleService::drainPairings);
}

BleService::~BleService() { stop(); }

ble::BleHost& BleService::host() {
    if (!m_host) {
        qCInfo(lcGui) << "Bluetooth: starting the BLE host";
        m_host = std::make_unique<ble::BleHost>(m_factory);
    }
    return *m_host;
}

bool BleService::scanning() const { return m_scanner && m_scanner->isScanning(); }

const std::vector<ble::BleDeviceInfo>& BleService::devices() const {
    static const std::vector<ble::BleDeviceInfo> none;
    return m_scanner ? m_scanner->devices() : none;
}

void BleService::acquireScan() {
    if (m_stopped) {
        return;
    }
    ++m_references;
    if (m_references == 1) {
        m_pollTimer.start();
        pollAdapter(); // at once; the scan starts when the adapter answers Ready
    }
}

void BleService::releaseScan() {
    if (m_references == 0) {
        return;
    }
    --m_references;
    if (m_references == 0) {
        m_pollTimer.stop();
        stopScan();
    }
}

void BleService::pollNow() {
    if (m_references > 0) {
        pollAdapter();
    }
}

void BleService::pollAdapter() {
    if (m_stopped || m_reply) {
        return; // a query is still pending
    }
    auto reply = std::make_shared<BoundedQueue<ble::AdapterInfo>>(1, OverflowPolicy::Reject);
    if (const Status posted = host().post(ble::QueryAdapter{.reply = reply}); !posted) {
        qCWarning(lcGui) << "Bluetooth: adapter query not posted:"
                         << toQString(posted.error().detail);
        return;
    }
    m_reply = std::move(reply);
    m_replyPosted = std::chrono::steady_clock::now();
    m_replyTimer.start();
}

void BleService::checkReply() {
    if (!m_reply) {
        m_replyTimer.stop();
        return;
    }
    std::optional<ble::AdapterInfo> adapter =
        m_reply->pop(std::stop_token(), std::chrono::milliseconds(0));
    if (!adapter) {
        if (std::chrono::steady_clock::now() - m_replyPosted > replyTimeout) {
            qCWarning(lcGui) << "Bluetooth: no answer to the adapter query";
            m_reply.reset(); // dropped; the next poll asks again
            m_replyTimer.stop();
        }
        return;
    }
    m_reply.reset();
    m_replyTimer.stop();
    onAdapter(*adapter);
}

void BleService::onAdapter(const ble::AdapterInfo& adapter) {
    const bool stateChangedNow = m_adapterState != adapter.state;
    m_adapterState = adapter.state;
    if (adapter.state == ble::AdapterState::PermissionUndetermined) {
        m_errorFromScan = false;
        setAdapterError(std::nullopt);
        requestPermission();
    } else if (std::optional<Error> problem = ble::adapterError(adapter)) {
        stopScan();
        m_errorFromScan = false;
        setAdapterError(std::move(problem));
    } else {
        if (!m_errorFromScan) {
            setAdapterError(std::nullopt); // a failed scan's error stays until a scan works
        }
        if (m_references > 0 && !scanning()) {
            startScanPeriod();
        }
    }
    if (stateChangedNow) {
        stateChanged(); // signal
    }
}

void BleService::requestPermission() {
    if (m_permissionAsked) {
        return;
    }
    m_permissionAsked = true;
#if QT_CONFIG(permissions)
    if (QCoreApplication::instance() == nullptr) {
        return;
    }
    qCInfo(lcGui) << "Bluetooth: requesting the permission";
    QBluetoothPermission permission;
    permission.setCommunicationModes(QBluetoothPermission::Access);
    QCoreApplication::instance()->requestPermission(permission, this, [this](const QPermission&) {
        pollAdapter(); // the answer is read from the adapter state
    });
#endif
}

void BleService::startScanPeriod() {
    if (m_stopped) {
        return;
    }
    if (!m_scanner) {
        m_scanner = std::make_unique<ble::BleScanner>(host());
        connect(m_scanner.get(), &ble::BleScanner::devicesChanged, this,
                [this] { devicesChanged(); });
        connect(m_scanner.get(), &ble::BleScanner::finished, this, &BleService::onScanFinished);
    }
    // All devices with their profile id: each picker filters ("Show all devices").
    ble::ScanOptions options{
        .duration = scanPeriod,
        .matcher =
            [](const ble::Advertisement& advertisement) {
                const device::DeviceProfile* profile = device::ProfileRegistry::findByAdvertisement(
                    advertisement.name, advertisement.serviceUuids);
                return profile != nullptr ? std::string(profile->id) : std::string();
            },
        .all = true};
    if (const Status started = m_scanner->start(std::move(options)); !started) {
        qCWarning(lcGui) << "Bluetooth: scan not started:" << toQString(started.error().detail);
        return;
    }
    stateChanged(); // signal
}

void BleService::onScanFinished(const std::optional<Error>& error) {
    const bool firstResult = !m_scanned;
    m_scanned = true;
    if (!error && m_errorFromScan) {
        m_errorFromScan = false;
        setAdapterError(std::nullopt);
    }
    if (error) {
        qCInfo(lcGui) << "Bluetooth: scan failed:" << toQString(idOf(error->code));
        m_errorFromScan = true;
        setAdapterError(error);
    } else if (m_references > 0 && !m_stopped) {
        startScanPeriod(); // ADR-0226: restarted every scanPeriod while visible
    }
    if (firstResult || error) {
        stateChanged(); // signal
    }
}

void BleService::stopScan() {
    if (m_scanner && m_scanner->isScanning()) {
        m_scanner->stop();
        stateChanged(); // signal
    }
}

void BleService::setAdapterError(std::optional<Error> error) {
    const bool changed = error.has_value() != m_adapterError.has_value() ||
                         (error && m_adapterError && error->code != m_adapterError->code);
    m_adapterError = std::move(error);
    if (changed) {
        stateChanged(); // signal
    }
}

Status BleService::pair(const ble::BleDeviceId& device, QObject* context, PairDone done) {
    if (m_stopped) {
        return std::unexpected(makeError(ErrorCode::Cancelled, "Bluetooth stopped"));
    }
    if (m_pairings.size() >= maxPendingPairings) {
        return std::unexpected(makeError(ErrorCode::InternalQueueFull, "too many pairings"));
    }
    // The wake function runs on the BLE thread: one queued drain (Qt drops it with this object).
    auto subscription = ble::BleHost::subscribe(
        [this] { QMetaObject::invokeMethod(this, "drainPairings", Qt::QueuedConnection); });
    if (Status posted = host().post(
            ble::PairDevice{.device = device, .subscriber = subscription, .client = std::nullopt});
        !posted) {
        subscription->detach();
        return posted;
    }
    qCInfo(lcGui) << "Bluetooth: pairing" << toQString(device);
    m_pairings.push_back(
        PendingPairing{.subscription = std::move(subscription),
                       .context = context,
                       .done = std::move(done),
                       .deadline = std::chrono::steady_clock::now() + pairTimeout});
    m_pairTimer.start();
    return {};
}

void BleService::drainPairings() {
    const auto now = std::chrono::steady_clock::now();
    // Finished entries are moved out first: a callback may start another pairing.
    std::vector<std::pair<PendingPairing, std::optional<ble::PairingResult>>> finished;
    for (auto it = m_pairings.begin(); it != m_pairings.end();) {
        std::optional<ble::PairingResult> result;
        while (auto event = it->subscription->pop()) {
            if (auto* pairing = std::get_if<ble::PairingResult>(&*event)) {
                result = std::move(*pairing);
            }
        }
        if (result || now >= it->deadline || it->context.isNull()) {
            it->subscription->detach();
            finished.emplace_back(std::move(*it), std::move(result));
            it = m_pairings.erase(it);
        } else {
            ++it;
        }
    }
    if (m_pairings.empty()) {
        m_pairTimer.stop();
    }
    for (auto& [pairing, result] : finished) {
        if (pairing.context.isNull() || !pairing.done) {
            continue;
        }
        if (result) {
            pairing.done(result->status, result->alreadyPaired);
        } else {
            pairing.done(std::unexpected(makeError(ErrorCode::BlePairingFailed, "no result")),
                         false);
        }
    }
}

void BleService::stop() {
    if (m_stopped) {
        return;
    }
    m_stopped = true;
    m_pollTimer.stop();
    m_replyTimer.stop();
    m_reply.reset();
    m_pairTimer.stop();
    for (PendingPairing& pairing : m_pairings) {
        pairing.subscription->detach();
    }
    m_pairings.clear();
    if (m_scanner) {
        m_scanner->stop();
        m_scanner.reset();
    }
    if (m_host) {
        m_host->stop();
    }
}

} // namespace hwio::gui
