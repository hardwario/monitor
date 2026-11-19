#pragma once

// `hwio chester app console` and `hwio chester app command` (plan/features/0002-m1-jlink-rtt.md,
// py-hardwario compatible options).

#include "cli_app.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "probe_options.h"

#include <CLI/App.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace hwio::cli {

// --jlink-speed: "auto" (nullopt: the automatic speed, as pylink; ADR-0090) or a fixed speed in
// kHz, device::minSpeedKhz..device::maxSpeedKhz. Errors: usage.invalid_argument (English text).
[[nodiscard]] Result<std::optional<int>> parseJLinkSpeed(std::string_view text);

// Plan 0044: the session options of `chester app console` (also used by `remote host`).
[[nodiscard]] Result<device::SessionOptions>
appConsoleSessionOptions(const ProbeOptions& group, const ProbeOptions& own, int latencyMs,
                         bool autoReconnect, bool reset, const std::string& consoleFile,
                         bool consoleFileGiven);

void registerChesterCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

} // namespace hwio::cli
