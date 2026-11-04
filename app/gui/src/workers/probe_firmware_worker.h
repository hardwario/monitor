#pragma once

// ProbeFirmwareWorker - runs the short-lived J-Link probe tools of the GUI's firmware update off
// the GUI thread (plan/features/0036-jlink-firmware-update-gui.md, ADR-0257).
//
// One job at a time: start() starts one owned std::jthread that runs the registry-free
// device::inspectProbe() or device::updateProbeFirmware() (own library copy, ADR-0030; the probe's
// ProbeLock, ADR-0058) with copies of the RegistryOptions and the LibraryLocation. The result is
// stored in a mutex-protected slot and the GUI thread is woken with one queued call by name (the
// ProbeScanner / DeviceSession pattern, ADR-0031); there the thread is joined and the result handed
// to the `finished` callback (exactly one result per job). The vendor call is never cancelled (an
// interrupted update can leave the probe without firmware): the destructor joins a running job,
// which may take the duration of the update (10-30 s, bounded by the vendor call).
//
// Thread safety: the public functions are called on the GUI thread (the owner). The job touches no
// Qt object and no SessionRegistry (the GUI thread reserves the probe, ADR-0257).

#include "hwio/core/result.h"
#include "hwio/device/probe_tools.h"
#include "hwio/device/session_registry.h"
#include "hwio/hw/jlink/library_locator.h"

#include <QObject>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>

namespace hwio::gui {

class ProbeFirmwareWorker : public QObject {
    Q_OBJECT

public:
    enum class Job : std::uint8_t { Inspect, Update };

    struct Request {
        Job job = Job::Inspect;
        device::RegistryOptions options;
        jlink::LibraryLocation location;
        std::uint32_t serialNumber = 0;
    };
    using Outcome = std::variant<Result<device::ProbeDetails>, Result<device::FirmwareUpdate>>;

    // The probe tools the jobs call (tests replace them; default: the registry-free libs/device
    // overloads). Called on the job thread.
    struct Tools {
        std::function<Result<device::ProbeDetails>(const device::RegistryOptions&,
                                                   const jlink::LibraryLocation&, std::uint32_t)>
            inspect;
        std::function<Result<device::FirmwareUpdate>(const device::RegistryOptions&,
                                                     const jlink::LibraryLocation&, std::uint32_t)>
            update;
    };
    [[nodiscard]] static Tools defaultTools();

    // `finished` is called on the GUI thread once per job, after the thread has been joined.
    ProbeFirmwareWorker(Tools tools, std::function<void(Job, Outcome)> finished,
                        QObject* parent = nullptr);
    ~ProbeFirmwareWorker() override; // joins a running job (it is never interrupted)
    ProbeFirmwareWorker(const ProbeFirmwareWorker&) = delete;
    ProbeFirmwareWorker& operator=(const ProbeFirmwareWorker&) = delete;
    ProbeFirmwareWorker(ProbeFirmwareWorker&&) = delete;
    ProbeFirmwareWorker& operator=(ProbeFirmwareWorker&&) = delete;

    // false (nothing started) while a job runs or its result has not been delivered yet.
    [[nodiscard]] bool start(Request request);
    // A job runs or its result waits for delivery.
    [[nodiscard]] bool busy() const noexcept { return m_busy; }

private:
    // GUI thread: invoked (queued, by name) by the job.
    Q_INVOKABLE void deliver();
    void join() noexcept;

    Tools m_tools;
    std::function<void(Job, Outcome)> m_finished;
    bool m_busy = false; // GUI thread only
    std::optional<Job> m_job;

    std::mutex m_mutex; // guards m_result
    std::optional<Outcome> m_result;
    std::jthread m_thread; // last member: joined first
};

} // namespace hwio::gui
