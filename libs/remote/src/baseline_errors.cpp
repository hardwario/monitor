#include "hwio/remote/baseline_errors.h"

#include "hwio/core/error.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace hwio::remote {

namespace {

// errorTable of tag v2.0.0-rc.4, in its order. Frozen (ADR-0414): never add, remove or rename.
constexpr std::array<std::string_view, baselineErrorCount> rc4Ids{
    "internal.unexpected",
    "internal.queue_full",
    "usage.invalid_argument",
    "io.file_not_found",
    "io.read_failed",
    "io.write_failed",
    "settings.newer_schema",
    "timeout",
    "cancelled",
    "dependency.jlink_not_found",
    "dependency.jlink_load_failed",
    "dependency.jlink_too_old",
    "probe.not_found",
    "probe.in_use_by_other_process",
    "probe.open_failed",
    "probe.firmware_outdated",
    "target.no_power",
    "target.connect_failed",
    "target.rtt_not_found",
    "target.rtt_buffer_not_found",
    "target.rtt_write_timeout",
    "target.rtt_read_failed",
    "internal.session_limit",
    "usage.confirmation_required",
    "target.rtt_write_failed",
    "probe.firmware_update_failed",
    "target.not_connected",
    "firmware.hex_parse_error",
    "firmware.invalid_image",
    "firmware.out_of_range",
    "firmware.uicr_in_image",
    "firmware.verify_failed",
    "firmware.not_found_in_catalog",
    "firmware.modem_update_failed",
    "network.download_failed",
    "target.wrong_family",
    "target.protected",
    "target.program_failed",
    "target.erase_failed",
    "target.recover_failed",
    "pib.invalid",
    "dependency.nrfutil_not_found",
    "dependency.nrfutil_device_missing",
    "dependency.nrfutil_too_old",
    "internal.busy",
    "probe.io_failed",
    "usb.access_denied",
    "probe.held_by_program",
    "diag.report_write_failed",
    "diag.check_timeout",
    "ble.adapter_not_found",
    "ble.adapter_off",
    "ble.permission_denied",
    "ble.stack_unavailable",
    "ble.device_not_found",
    "ble.connect_failed",
    "ble.service_not_found",
    "ble.not_paired",
    "ble.pairing_failed",
    "ble.pairing_not_supported",
    "ble.write_failed",
    "ble.write_timeout",
    "ble.disconnected",
    "ble.in_use_by_other_process",
    "ble.no_pairing_agent",
    "mcp.port_in_use",
    "mcp.listen_failed",
    "mcp.token_file",
    "mcp.tool_not_allowed",
    "mcp.consent_denied",
    "mcp.consent_timeout",
    "mcp.device_ambiguous",
    "mcp.device_not_open",
    "mcp.capability_missing",
    "mcp.too_many_requests",
    "target.not_halted",
    "target.address_out_of_range",
    "target.memory_access_failed",
    "io.file_exists",
    "dependency.jlink_function_missing",
    "firmware.checksum_mismatch",
    "update.network",
    "update.rate_limited",
    "update.bad_response",
    "update.too_large",
    "mcp.to_file_not_allowed",
    "cli.file_is_symlink",
    "usb.world_accessible",
    "target.other_connector",
    "target.reset_during_update",
    "remote.relay_unreachable",
    "remote.code_invalid",
    "remote.code_collision",
    "remote.join_not_allowed",
    "remote.host_ended",
    "remote.operation_denied",
    "remote.operation_unsupported_peer",
    "remote.firmware_not_in_catalog",
    "remote.protocol_error",
    "remote.application_not_restored",
};

struct Mapping {
    ErrorCode code;
    std::string_view baseline; // one of rc4Ids
};

// Error ids added after 2.0.0-rc.4 → the rc.4 id of the same category with the nearest meaning
// (plan 0059 § Implementation notes, A1). Add a row with every new ErrorCode.
constexpr std::array mappings{
    // Plan 0051: rc.4 reported nRF Util LOW_VOLTAGE as firmware.modem_update_failed; the category
    // rule (Target) gives target.no_power, whose text names the power supply as this one does.
    Mapping{.code = ErrorCode::TargetLowVoltage, .baseline = "target.no_power"},
    // Plan 0059: an older joiner shows a refusal of the customer's Monitor.
    Mapping{.code = ErrorCode::RemoteTargetUnsupported, .baseline = "remote.operation_denied"},
    Mapping{.code = ErrorCode::RemoteCommandRefused, .baseline = "remote.operation_denied"},
    // Plan 0062 (ADR-0438): support's side only, never sent over the wire; rows for completeness.
    Mapping{.code = ErrorCode::RemoteCommandNotDelivered, .baseline = "timeout"},
    Mapping{.code = ErrorCode::RemoteWaitingForCustomer, .baseline = "remote.join_not_allowed"},
};

} // namespace

std::span<const std::string_view> baselineErrorIds() noexcept { return rc4Ids; }

bool isBaselineErrorId(std::string_view id) noexcept {
    return std::ranges::find(rc4Ids, id) != rc4Ids.end();
}

bool hasBaselineMapping(ErrorCode code) noexcept {
    return isBaselineErrorId(idOf(code)) ||
           std::ranges::find(mappings, code, &Mapping::code) != mappings.end();
}

std::string_view baselineErrorId(ErrorCode code) noexcept {
    if (const std::string_view id = idOf(code); isBaselineErrorId(id)) {
        return id;
    }
    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto it = std::ranges::find(mappings, code, &Mapping::code);
    return it != mappings.end() ? it->baseline : std::string_view("internal.unexpected");
}

} // namespace hwio::remote
