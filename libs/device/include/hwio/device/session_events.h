#pragma once

// Events of a console session (plan/features/0002-m1-jlink-rtt.md, ADR-0031; M5: the transport
// and the BLE MTU, plan/features/0011, ADR-0079).

#include "hwio/core/error.h"
#include "hwio/device/console_line.h"
#include "hwio/hw/jlink/types.h"

#include <QMetaType>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace hwio::device {

// The transport of a console session (ADR-0079). Remote (plan 0044, ADR-0325): the joiner side of
// a remote support session (hwio::remote::RemoteConsoleSession), no hardware of its own.
enum class Transport : std::uint8_t { JLink, Ble, Remote };

enum class ConnectionState : std::uint8_t {
    Connecting, // the worker loads the library, opens the probe, connects and attaches RTT
    // M4 (ADR-0069): probe open and locked, the target not connected (connectTarget = false);
    // the terminal state of a probe-only session until startConnection().
    ProbeOpen,
    Connected,    // RTT attached and the link works
    Disconnected, // the link failed (error says why); auto-reconnect or reconnect() may recover
    Stopped,      // fatal error, or suspended on purpose (no error); only reconnect() recovers
};

struct ConnectionEvent {
    ConnectionState state = ConnectionState::Connecting;
    std::optional<Error> error;
    // ADR-0091: the target voltage (VTref, mV) the session's last power reading measured when the
    // event was emitted (set at least on Connected); nullopt when not measured or unavailable
    // (no reading yet, the probe does not answer, or a probe that cannot measure VTref reads 0).
    std::optional<int> targetMillivolts;
    // M5 (plan/features/0011): the session's transport; ProbeOpen is never used by BLE.
    Transport transport = Transport::JLink;
    // M5: the negotiated BLE ATT MTU, set on Connected of a BLE session; nullopt for J-Link.
    std::optional<std::uint16_t> mtu = std::nullopt;
    // Plan 0028: the attach this event belongs to, set by the J-Link worker / BLE handler on
    // every Connected (monotonic per session, from 1) and kept on the later events of that
    // attach; 0 before the first Connected. ConsoleSession::setFirmwareIdentity() names it, so a
    // stale answer after a reconnect is ignored (events may be dropped, ADR-0031).
    std::uint64_t attach = 0;
};

// Events were lost because the event queue was full (it drops the oldest, ADR-0031).
struct LinesDropped {
    std::uint64_t count = 0;
};

// Plan 0036: what the J-Link session found about the probe firmware after the first open; carried
// by the probe.firmware_outdated warning (Outdated or Newer), so the GUI never parses its text.
struct ProbeFirmwareNotice {
    jlink::FirmwareStatus status = jlink::FirmwareStatus::Current;
    std::string probeFirmware;   // the probe's firmware string
    std::string libraryFirmware; // the firmware bundled in the library ("" when unknown)
};

struct Warning {
    Error error;
    std::optional<ProbeFirmwareNotice> probeFirmware = std::nullopt; // plan 0036
};

// The shell prompt of the profile was seen after output (ADR-0082 for BLE; M6, ADR-0104: also
// RTT when TargetProfile::shellPrompt is set). Not a line; ConsoleSession::promptSeen.
struct PromptSeen {};

using SessionEvent = std::variant<ConsoleLine, ConnectionEvent, LinesDropped, Warning, PromptSeen>;

// "connecting", "probeOpen", "connected", "disconnected", "stopped" (JSON).
[[nodiscard]] std::string_view connectionStateId(ConnectionState state) noexcept;
// "jlink", "ble", "remote" (JSON).
[[nodiscard]] std::string_view transportId(Transport transport) noexcept;

} // namespace hwio::device

Q_DECLARE_METATYPE(hwio::device::ConnectionEvent)
Q_DECLARE_METATYPE(hwio::Error)
