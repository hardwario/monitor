#pragma once

// SessionRegistry - the sessions of this process (plan/features/0002-m1-jlink-rtt.md, ADR-0030,
// ADR-0058). Owned by the application (GUI / CLI); must outlive its sessions.
//
// Bounds: at most maxSessions sessions (internal.session_limit), J-Link and BLE together
// (ADR-0083), at most one per SessionKey (probe.open_failed / ble.in_use_by_other_process
// "already used by this application"). Probes and Bluetooth devices used by other processes are
// detected through their ProbeLock (ADR-0058) / BleLock (ADR-0083).
//
// Thread safety: none; owner thread only (the thread of the sessions' facades).

#include "hwio/core/result.h"
#include "hwio/device/probe_lock.h"
#include "hwio/device/session_key.h"
#include "hwio/hw/ble/types.h"
#include "hwio/hw/jlink/library_locator.h"
#include "hwio/hw/jlink/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace hwio::device {

class BleSession;
class ConsoleSession;
class DeviceSession;

namespace detail {
struct BleSessionInternals;
struct SessionInternals;
} // namespace detail

struct RegistryOptions {
    std::filesystem::path instanceRoot;     // J-Link library copies (ADR-0030)
    std::filesystem::path lockDirectory;    // probe locks (ADR-0058)
    std::filesystem::path consoleDirectory; // default console files and command history

    // JLinkLibrary::defaultInstanceRoot(), ProbeLock::defaultDirectory(),
    // ConsoleFileSink::defaultDirectory().
    [[nodiscard]] static RegistryOptions defaults();
};

struct ProbeStatus {
    jlink::ProbeInfo probe;
    const DeviceSession* usedBySession = nullptr;     // a session of this process (GUI: a tab)
    std::optional<ProbeLockOwner> usedByOtherProcess; // another process's lock owner
};

// Who uses a Bluetooth device (scan rows; plan 0011).
struct BleUse {
    const ConsoleSession* session = nullptr;    // a session of this process
    std::optional<ProbeLockOwner> otherProcess; // another process's BleLock owner
};

class SessionRegistry;

// Plan 0036 (ADR-0257): a probe reserved by this process for a short-lived probe operation run
// off the owner thread (the GUI's J-Link firmware update). While it lives, checkAvailable() of
// that serial number fails (probe.open_failed "is being updated"), so no session of this process
// can open the probe. Move-only RAII; owner thread only; must be released before the registry
// is destroyed. A default-constructed (or moved-from) reservation holds nothing.
class ProbeReservation {
public:
    ProbeReservation() = default;
    ~ProbeReservation() { release(); }
    ProbeReservation(const ProbeReservation&) = delete;
    ProbeReservation& operator=(const ProbeReservation&) = delete;
    ProbeReservation(ProbeReservation&& other) noexcept;
    ProbeReservation& operator=(ProbeReservation&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept { return m_registry != nullptr; }
    [[nodiscard]] std::uint32_t serialNumber() const noexcept { return m_serialNumber; }
    // Idempotent.
    void release() noexcept;

private:
    friend class SessionRegistry;
    ProbeReservation(SessionRegistry& registry, std::uint32_t serialNumber) noexcept
        : m_registry(&registry), m_serialNumber(serialNumber) {}

    SessionRegistry* m_registry = nullptr; // non-owning; outlives the reservation
    std::uint32_t m_serialNumber = 0;
};

struct ProbeStatusList {
    std::uint32_t libraryVersion = 0; // read from the loaded library
    std::vector<ProbeStatus> probes;
};

class SessionRegistry {
public:
    static constexpr std::size_t maxSessions = 8;

    // Removes stale J-Link instance directories (ADR-0030: older than 24 h).
    explicit SessionRegistry(RegistryOptions options = RegistryOptions::defaults());
    // Closes the sessions that are still open.
    ~SessionRegistry();
    SessionRegistry(const SessionRegistry&) = delete;
    SessionRegistry& operator=(const SessionRegistry&) = delete;
    SessionRegistry(SessionRegistry&&) = delete;
    SessionRegistry& operator=(SessionRegistry&&) = delete;

    // Enumerates the probes (separate short-lived library copy) and adds, per probe, who uses it.
    // Errors: those of jlink::enumerateProbes.
    [[nodiscard]] Result<ProbeStatusList>
    enumerateProbes(const jlink::LibraryLocation& location) const;
    [[nodiscard]] Result<std::vector<ProbeStatus>>
    listProbes(const jlink::LibraryLocation& location) const;

    [[nodiscard]] std::size_t sessionCount() const noexcept { return m_sessions.size(); }
    // The session of this process that uses (or asked for) the probe, or nullptr.
    // (A thin wrapper of sessionFor(ProbeKey) for the M1-M4 call sites.)
    [[nodiscard]] const DeviceSession* sessionForProbe(std::uint32_t serialNumber) const;
    // The session of this process with this key (a J-Link session also by its requested serial
    // number), or nullptr.
    [[nodiscard]] const ConsoleSession* sessionFor(const SessionKey& key) const;
    // This process's session of the device, else the other process holding its BleLock.
    [[nodiscard]] BleUse bleInUse(const ble::BleDeviceId& id) const;
    [[nodiscard]] const RegistryOptions& options() const noexcept { return m_options; }

    // Errors: internal.session_limit, probe.open_failed (the probe is used by a session of this
    // process). Used by DeviceSession::open() and the short-lived probe tools.
    [[nodiscard]] Status checkAvailable(std::optional<std::uint32_t> serialNumber) const;
    // Errors: internal.session_limit, probe.open_failed (J-Link key used by this process),
    // ble.in_use_by_other_process "already used by this application" (BLE key, Q3).
    [[nodiscard]] Status checkAvailable(const std::optional<SessionKey>& key) const;

    // Plan 0036 (ADR-0257): reserves the probe (see ProbeReservation). Call after a successful
    // checkAvailable(serialNumber). An empty reservation (valid() false) when the serial number
    // is already reserved or maxReservations are held.
    static constexpr std::size_t maxReservations = maxSessions;
    [[nodiscard]] ProbeReservation reserve(std::uint32_t serialNumber);
    [[nodiscard]] bool reserved(std::uint32_t serialNumber) const;

private:
    friend class BleSession;
    friend class ProbeReservation;
    friend class DeviceSession;
    friend struct detail::BleSessionInternals;
    friend struct detail::SessionInternals;

    void add(ConsoleSession* session);
    void remove(ConsoleSession* session) noexcept;

    RegistryOptions m_options;
    std::vector<ConsoleSession*> m_sessions; // non-owning; each removes itself in close()
    std::vector<std::uint32_t> m_reserved;   // ≤ maxReservations serial numbers (plan 0036)
};

} // namespace hwio::device
