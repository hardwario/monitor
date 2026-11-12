#include "hwio/remote/host_operations.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/program_progress.h"
#include "hwio/remote/tunnel.h"

#include <QLoggingCategory>
#include <QObject>
#include <QtLogging>

#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::remote {

DeviceSessionOperations::DeviceSessionOperations(device::DeviceSession& session,
                                                 firmware::CatalogOptions catalog,
                                                 firmware::FirmwareCache* cache, QObject* parent)
    : QObject(parent), m_session(&session),
      m_catalog(std::make_unique<firmware::FirmwareCatalog>(std::move(catalog), cache)) {
    connect(m_session, &device::DeviceSession::operationProgress, this,
            [this](device::OperationId id, const device::OperationProgress& progress) {
                if (!m_operation || *m_operation != id || !m_progress) {
                    return;
                }
                m_progress(OperationUpdate{.phase = std::string(firmware::phaseId(progress.phase)),
                                           .step = progress.step,
                                           .steps = progress.steps,
                                           .done = progress.done,
                                           .total = progress.total});
            });
    connect(m_session, &device::DeviceSession::operationFinished, this,
            [this](device::OperationId id, const device::OperationResult& result) {
                if (!m_operation || *m_operation != id) {
                    return;
                }
                m_operation.reset();
                finish(result.status);
            });
}

DeviceSessionOperations::~DeviceSessionOperations() { abandon(); }

std::vector<tunnel::OpKind> DeviceSessionOperations::kinds() const {
    std::vector<tunnel::OpKind> kinds{tunnel::OpKind::Flash, tunnel::OpKind::Erase,
                                      tunnel::OpKind::Recover, tunnel::OpKind::Reset};
    if (!lteUpdateFirmware().first.empty()) {
        kinds.push_back(tunnel::OpKind::LteUpdate);
    }
    return kinds;
}

std::pair<std::string, std::string> DeviceSessionOperations::lteUpdateFirmware() const {
    if (m_lteUpdate == nullptr) {
        return {};
    }
    const device::TargetProfile* lte = device::findTarget(m_session->profile(), "lte");
    if (lte == nullptr || !lte->designatedFirmware) {
        return {};
    }
    return {std::string(lte->designatedFirmware->name),
            std::string(lte->designatedFirmware->version)};
}

Status DeviceSessionOperations::validate(const tunnel::OpRequest& request) const {
    if (request.kind == tunnel::OpKind::LteUpdate) {
        if (lteUpdateFirmware().first.empty()) {
            return std::unexpected(makeError(ErrorCode::RemoteOperationUnsupportedPeer,
                                             "no guided LTE update on this host"));
        }
        return {};
    }
    const std::string_view target = tunnel::targetId(request.target);
    if (device::findTarget(m_session->profile(), target) == nullptr) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         std::format("the device has no {} target", target)));
    }
    if (request.kind == tunnel::OpKind::Flash && request.target != tunnel::OpTarget::App) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument,
                      "catalog firmware is flashed to the application core only"));
    }
    return {};
}

void DeviceSessionOperations::lookup(const std::string& firmwareId, LookupDone done) {
    auto id = firmware::FirmwareId::parse(firmwareId);
    if (!id) {
        done(std::unexpected(makeError(ErrorCode::RemoteFirmwareNotInCatalog, firmwareId)));
        return;
    }
    m_lookup = m_catalog->lookup(*id, m_stop.get_token());
    m_lookupDone = std::move(done);
    connect(m_lookup.get(), &firmware::LookupOperation::finished, this, [this] {
        Result<firmware::CatalogInfo> result = *m_lookup->result();
        m_lookup.release()->deleteLater(); // inside its own signal
        if (!result && result.error().code == ErrorCode::FirmwareNotFoundInCatalog) {
            result = std::unexpected(
                makeError(ErrorCode::RemoteFirmwareNotInCatalog, result.error().detail));
        }
        m_lookedUp = result ? std::optional(*result) : std::nullopt;
        if (LookupDone callback = std::exchange(m_lookupDone, {})) {
            callback(std::move(result));
        }
    });
}

Status DeviceSessionOperations::start(const tunnel::OpRequest& request, Progress progress,
                                      Finished finished) {
    if (m_running) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "a remote operation runs"));
    }
    if (Status valid = validate(request); !valid) {
        return valid;
    }
    m_running = true;
    m_progress = std::move(progress);
    m_finished = std::move(finished);
    if (request.kind == tunnel::OpKind::LteUpdate) {
        // ADR-0363: the guided LTE update (GUI wizard / CLI terminal flow).
        m_lteUpdateRunning = true;
        Status started = m_lteUpdate->start(
            [this](const OperationUpdate& update) {
                if (m_progress) {
                    m_progress(update);
                }
            },
            [this](Status status) {
                m_lteUpdateRunning = false;
                finish(std::move(status));
            });
        if (!started) {
            m_lteUpdateRunning = false;
            m_running = false;
            m_progress = {};
            m_finished = {};
        }
        return started;
    }
    if (request.kind != tunnel::OpKind::Flash) {
        startDeviceOperation(request, nullptr);
        return {};
    }
    auto id = firmware::FirmwareId::parse(request.firmwareId.value_or(""));
    if (!id) {
        m_running = false;
        return std::unexpected(
            makeError(ErrorCode::RemoteFirmwareNotInCatalog, request.firmwareId.value_or("")));
    }
    // ADR-0037 / ADR-0166: downloaded (or from the cache) and verified by the host.
    m_download = m_catalog->download(
        *id,
        firmware::DownloadOptions{.noCache = false,
                                  .expectedSha256 = m_lookedUp ? m_lookedUp->sha256 : std::nullopt},
        [this](std::uint64_t done, std::uint64_t total) {
            if (m_progress) {
                m_progress(OperationUpdate{
                    .phase = "download", .step = 1, .steps = 1, .done = done, .total = total});
            }
        },
        m_stop.get_token());
    connect(m_download.get(), &firmware::CatalogOperation::finished, this, [this, request] {
        Result<firmware::CachedFirmware> downloaded = *m_download->result();
        m_download.release()->deleteLater(); // inside its own signal
        if (!downloaded) {
            Error error = std::move(downloaded).error();
            if (error.code == ErrorCode::FirmwareNotFoundInCatalog) {
                error = makeError(ErrorCode::RemoteFirmwareNotInCatalog, error.detail);
            }
            finish(std::unexpected(std::move(error)));
            return;
        }
        for (const Error& warning : downloaded->warnings) {
            qCWarning(log::lcRemote).noquote() << "Remote flash:" << toQString(warning.detail);
        }
        startDeviceOperation(request, downloaded->image);
    });
    return {};
}

void DeviceSessionOperations::startDeviceOperation(
    const tunnel::OpRequest& request, std::shared_ptr<const firmware::FirmwareImage> image) {
    const std::string target(tunnel::targetId(request.target));
    Result<device::OperationId> started = std::unexpected(makeError(ErrorCode::InternalUnexpected));
    switch (request.kind) {
    case tunnel::OpKind::Flash:
        started = m_session->flash(target, std::move(image), device::FlashOptions{});
        break;
    case tunnel::OpKind::Erase:
        started = m_session->erase(target, request.target == tunnel::OpTarget::App
                                               ? device::EraseScope::Code
                                               : device::EraseScope::All);
        break;
    case tunnel::OpKind::Recover:
        started = m_session->recover(target);
        break;
    case tunnel::OpKind::Reset:
        started = m_session->reset(target, device::ResetMode::Go);
        break;
    case tunnel::OpKind::LteUpdate: // started by start() through the runner
        break;
    }
    if (!started) {
        finish(std::unexpected(std::move(started).error()));
        return;
    }
    m_operation = *started;
}

void DeviceSessionOperations::finish(Status status) {
    m_running = false;
    m_progress = {};
    if (Finished callback = std::exchange(m_finished, {})) {
        callback(std::move(status));
    }
}

void DeviceSessionOperations::abandon() {
    m_stop.request_stop();
    m_stop = std::stop_source();
    m_lookupDone = {};
    m_progress = {};
    m_finished = {};
    if (m_lookup) {
        m_lookup->disconnect(this);
        m_lookup->cancel();
        m_lookup.reset();
    }
    if (m_download) {
        m_download->disconnect(this);
        m_download->cancel();
        m_download.reset();
    }
    if (m_lteUpdateRunning && m_lteUpdate != nullptr) {
        m_lteUpdate->abandon(); // the guided update stays with the customer, unreported
    }
    m_lteUpdateRunning = false;
    // A device operation that runs keeps running (m_operation); its result is not reported.
    m_running = m_operation.has_value();
}

} // namespace hwio::remote
