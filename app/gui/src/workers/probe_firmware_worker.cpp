#include "workers/probe_firmware_worker.h"

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/probe_tools.h"
#include "hwio/device/session_registry.h"
#include "hwio/hw/jlink/library_locator.h"

#include <QMetaObject>
#include <QObject>
#include <QtCore/qnamespace.h>

#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace hwio::gui {

ProbeFirmwareWorker::Tools ProbeFirmwareWorker::defaultTools() {
    return Tools{.inspect =
                     [](const device::RegistryOptions& options,
                        const jlink::LibraryLocation& location, std::uint32_t serialNumber) {
                         return device::inspectProbe(options, location, serialNumber);
                     },
                 .update =
                     [](const device::RegistryOptions& options,
                        const jlink::LibraryLocation& location, std::uint32_t serialNumber) {
                         return device::updateProbeFirmware(options, location, serialNumber);
                     }};
}

ProbeFirmwareWorker::ProbeFirmwareWorker(Tools tools, std::function<void(Job, Outcome)> finished,
                                         QObject* parent)
    : QObject(parent), m_tools(std::move(tools)), m_finished(std::move(finished)) {}

ProbeFirmwareWorker::~ProbeFirmwareWorker() { join(); }

void ProbeFirmwareWorker::join() noexcept {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool ProbeFirmwareWorker::start(Request request) {
    if (m_busy) {
        return false;
    }
    join(); // the previous job's thread has delivered; joined there already
    m_busy = true;
    m_job = request.job;
    m_thread = std::jthread([this, request = std::move(request)] {
        const auto failure = [&request](Error error) -> Outcome {
            if (request.job == Job::Inspect) {
                return Result<device::ProbeDetails>(std::unexpected(std::move(error)));
            }
            return Result<device::FirmwareUpdate>(std::unexpected(std::move(error)));
        };
        std::optional<Outcome> outcome;
        // No exception may leave the thread (CLAUDE.md, ADR-0007).
        try {
            if (request.job == Job::Inspect && m_tools.inspect) {
                outcome = m_tools.inspect(request.options, request.location, request.serialNumber);
            } else if (request.job == Job::Update && m_tools.update) {
                outcome = m_tools.update(request.options, request.location, request.serialNumber);
            } else {
                outcome = failure(makeError(ErrorCode::InternalUnexpected, "no probe tool"));
            }
        } catch (const std::exception& e) {
            outcome = failure(makeError(ErrorCode::InternalUnexpected, e.what()));
        } catch (...) {
            outcome = failure(makeError(ErrorCode::InternalUnexpected, "unknown exception"));
        }
        {
            const std::scoped_lock lock(m_mutex);
            m_result = std::move(*outcome);
        }
        // By name: Qt creates and consumes the queued call (see DeviceSession::wakeUp).
        QMetaObject::invokeMethod(this, "deliver", Qt::QueuedConnection);
    });
    return true;
}

void ProbeFirmwareWorker::deliver() {
    std::optional<Outcome> result;
    {
        const std::scoped_lock lock(m_mutex);
        result.swap(m_result);
    }
    const std::optional<Job> job = std::exchange(m_job, std::nullopt);
    if (!result || !job) {
        return;
    }
    join(); // the job has stored its result: the thread ends at once
    m_busy = false;
    if (m_finished) {
        m_finished(*job, std::move(*result));
    }
}

} // namespace hwio::gui
