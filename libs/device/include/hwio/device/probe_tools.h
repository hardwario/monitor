#pragma once

// Short-lived probe operations for `hwio jlink info` and `hwio jlink update-firmware`
// (plan/features/0002-m1-jlink-rtt.md). They run synchronously on the caller's thread with their
// own library copy (ADR-0030) and hold the probe's ProbeLock (ADR-0058) while they work. Without a
// serial number the first probe that is not locked is used.
//
// Plan 0036 (ADR-0257): the overloads taking RegistryOptions never touch a SessionRegistry (which
// is owner-thread only), so they may run on any thread; the caller checks the registry
// (checkAvailable) and holds a ProbeReservation for the serial number on the owner thread.

#include "hwio/core/result.h"
#include "hwio/device/session_registry.h"
#include "hwio/hw/jlink/library_locator.h"
#include "hwio/hw/jlink/types.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace hwio::device {

struct ProbeDetails {
    std::filesystem::path libraryPath;
    std::uint32_t libraryVersion = 0;
    jlink::ProbeInfo probe;      // firmware = the probe's firmware string
    std::string libraryFirmware; // the firmware bundled in the library ("" when unknown)
    jlink::FirmwareStatus firmwareStatus = jlink::FirmwareStatus::Current;
    std::optional<int> targetMillivolts; // nullopt when the probe does not report it
};

// Errors: those of loading the library and opening the probe (probe.not_found,
// probe.open_failed, probe.in_use_by_other_process, ...).
[[nodiscard]] Result<ProbeDetails> inspectProbe(const SessionRegistry& registry,
                                                const jlink::LibraryLocation& location,
                                                std::optional<std::uint32_t> serialNumber);

// Registry-free (any thread; plan 0036).
[[nodiscard]] Result<ProbeDetails> inspectProbe(const RegistryOptions& options,
                                                const jlink::LibraryLocation& location,
                                                std::uint32_t serialNumber);

struct FirmwareUpdate {
    std::uint32_t serialNumber = 0;
    jlink::FirmwareStatus before = jlink::FirmwareStatus::Current; // Current = nothing was done
};

// Updates (outdated) or synchronizes (newer) the probe firmware with the library's.
[[nodiscard]] Result<FirmwareUpdate> updateProbeFirmware(const SessionRegistry& registry,
                                                         const jlink::LibraryLocation& location,
                                                         std::optional<std::uint32_t> serialNumber);
// Registry-free (any thread; plan 0036). Errors as above; a failure after the vendor update call
// (also a probe that does not come back to be verified) is probe.firmware_update_failed.
[[nodiscard]] Result<FirmwareUpdate> updateProbeFirmware(const RegistryOptions& options,
                                                         const jlink::LibraryLocation& location,
                                                         std::uint32_t serialNumber);

} // namespace hwio::device
