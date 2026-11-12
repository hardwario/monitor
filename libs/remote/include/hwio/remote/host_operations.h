#pragma once

// HostOperations - what a RemoteHost runs for an allowed operation request of a Monitor joiner
// (plan/features/0044, ADR-0322 scope, ADR-0323 catalog only, ADR-0336): flash (a catalog firmware
// ID, downloaded and verified by the host through the catalog and its cache, ADR-0037), erase,
// recover, reset of the `app` / `lte` target. One operation at a time. Owner thread only; the
// callbacks are called there.
//
// DeviceSessionOperations runs them on the J-Link DeviceSession of the shared console (the same
// session: flashing detaches and re-attaches RTT, ADR-0062):
//   flash   app only: catalog lookup (name, version, SHA-256) + download, then DeviceSession::
//           flash(app, image, {}) (UICR records not programmed, ADR-0036)
//   erase   app: EraseScope::Code (UICR and PIB kept); lte: EraseScope::All (as `chester lte
//   erase`) recover DeviceSession::recover(target) reset   DeviceSession::reset(target, Go)
//   lteUpdate (ADR-0363) through the LteUpdateRunner set by the GUI / CLI: the guided LTE update
//           with the profile's recommended (designated) LTE firmware; only when a runner is set
//           and the profile's LTE target has a designated firmware.
// A BLE console has no operations (Capability::Operations): no HostOperations, hello ops: false.

#include "hwio/core/result.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/remote/tunnel.h"

#include <QObject>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace hwio::device {
class DeviceSession;
} // namespace hwio::device

namespace hwio::firmware {
class FirmwareCache;
} // namespace hwio::firmware

namespace hwio::remote {

struct OperationUpdate {
    std::string phase; // firmware::phaseId() or "download"
    int step = 1;
    int steps = 1;
    std::uint64_t done = 0;
    std::uint64_t total = 0;
};

// Runs the guided LTE update for an allowed lteUpdate request (ADR-0363): the GUI opens Flash ›
// Update LTE firmware… with the recommended firmware and starts it, the CLI runs the terminal flow
// of `hwio chester lte flash --guided`. The customer does the cable steps. Owner thread only.
class LteUpdateRunner {
public:
    using Progress = std::function<void(const OperationUpdate&)>;
    using Finished = std::function<void(Status)>;

    LteUpdateRunner() = default;
    LteUpdateRunner(const LteUpdateRunner&) = delete;
    LteUpdateRunner& operator=(const LteUpdateRunner&) = delete;
    LteUpdateRunner(LteUpdateRunner&&) = delete;
    LteUpdateRunner& operator=(LteUpdateRunner&&) = delete;
    virtual ~LteUpdateRunner() = default;

    // Starts it; `progress` any number of times (the guided update's step as the phase), then
    // `finished` once: ok (the LTE MCU was updated and the application written back),
    // remote.application_not_restored (updated, the application not written back), cancelled,
    // or the update's error (target.reset_during_update, network.*, firmware.*, ...). Errors
    // (immediate): internal.busy (a wizard or operation is open), those of the start.
    [[nodiscard]] virtual Status start(Progress progress, Finished finished) = 0;
    // The session ends: no more callbacks. The guided update itself stays with the customer (it
    // is not interrupted while they move the cable).
    virtual void abandon() = 0;
};

class HostOperations {
public:
    using LookupDone = std::function<void(Result<firmware::CatalogInfo>)>;
    using Progress = std::function<void(const OperationUpdate&)>;
    using Finished = std::function<void(Status)>;

    HostOperations() = default;
    HostOperations(const HostOperations&) = delete;
    HostOperations& operator=(const HostOperations&) = delete;
    HostOperations(HostOperations&&) = delete;
    HostOperations& operator=(HostOperations&&) = delete;
    virtual ~HostOperations() = default;

    // The kinds this host runs (the hello's opKinds, ADR-0363).
    [[nodiscard]] virtual std::vector<tunnel::OpKind> kinds() const = 0;
    // The recommended LTE firmware of lteUpdate: {name, version} ("HARDWARIO LTE firmware",
    // "1.7.0"); empty when the host has no LTE update.
    [[nodiscard]] virtual std::pair<std::string, std::string> lteUpdateFirmware() const = 0;
    // The request fits the device (profile target exists, flash only to app). Errors:
    // usage.invalid_argument.
    [[nodiscard]] virtual Status validate(const tunnel::OpRequest& request) const = 0;
    // The catalog page of a flash request's firmware (for the confirmation text); `done` once.
    // Errors: remote.firmware_not_in_catalog (unknown ID), network.*, cancelled.
    virtual void lookup(const std::string& firmwareId, LookupDone done) = 0;
    // Starts an allowed request; `progress` any number of times, then `finished` once. Errors
    // (immediate): internal.busy (one runs), those of the session.
    [[nodiscard]] virtual Status start(const tunnel::OpRequest& request, Progress progress,
                                       Finished finished) = 0;
    // The session ends: a lookup or download is cancelled and no callback follows; an operation
    // already running on the device finishes (it is not interrupted half-way), unreported.
    virtual void abandon() = 0;
};

class DeviceSessionOperations final : public QObject, public HostOperations {
    Q_OBJECT

public:
    // `session` and `cache` (may be nullptr) must outlive this object.
    DeviceSessionOperations(device::DeviceSession& session, firmware::CatalogOptions catalog,
                            firmware::FirmwareCache* cache, QObject* parent = nullptr);
    ~DeviceSessionOperations() override;
    DeviceSessionOperations(const DeviceSessionOperations&) = delete;
    DeviceSessionOperations& operator=(const DeviceSessionOperations&) = delete;
    DeviceSessionOperations(DeviceSessionOperations&&) = delete;
    DeviceSessionOperations& operator=(DeviceSessionOperations&&) = delete;

    // ADR-0363: lteUpdate runs through `runner` (nullptr = no LTE update); it must outlive this
    // object or be replaced first.
    void setLteUpdateRunner(LteUpdateRunner* runner) noexcept { m_lteUpdate = runner; }

    [[nodiscard]] std::vector<tunnel::OpKind> kinds() const override;
    [[nodiscard]] std::pair<std::string, std::string> lteUpdateFirmware() const override;
    [[nodiscard]] Status validate(const tunnel::OpRequest& request) const override;
    void lookup(const std::string& firmwareId, LookupDone done) override;
    [[nodiscard]] Status start(const tunnel::OpRequest& request, Progress progress,
                               Finished finished) override;
    void abandon() override;

private:
    void startDeviceOperation(const tunnel::OpRequest& request,
                              std::shared_ptr<const firmware::FirmwareImage> image);
    void finish(Status status);

    device::DeviceSession* m_session;       // non-owning
    LteUpdateRunner* m_lteUpdate = nullptr; // non-owning, may be nullptr
    bool m_lteUpdateRunning = false;
    std::unique_ptr<firmware::FirmwareCatalog> m_catalog;
    std::unique_ptr<firmware::LookupOperation> m_lookup;
    LookupDone m_lookupDone;
    std::unique_ptr<firmware::CatalogOperation> m_download;
    std::optional<firmware::CatalogInfo> m_lookedUp; // the last lookup (its SHA-256 verifies)
    std::optional<std::uint64_t> m_operation;        // the device operation id
    Progress m_progress;
    Finished m_finished;
    bool m_running = false;
    std::stop_source m_stop;
};

} // namespace hwio::remote
