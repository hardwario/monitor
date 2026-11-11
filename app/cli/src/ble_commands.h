#pragma once

// `hwio ble scan|console|command|pair` (ADR-0085, plan/features/0011-m5-ble-backend-cli.md).
// English only (ADR-0027). Every command owns its BleHost (ADR-0080) for its run.

#include "cli_app.h"
#include "hwio/core/result.h"
#include "hwio/device/ble_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_registry.h"
#include "hwio/hw/ble/ble_host.h"

#include <CLI/App.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::cli {

void registerBleCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

// Plan 0044: what `ble console` opens, also for `remote host --ble`.
struct BleDeviceSelection {
    std::string address; // -a/--address
    std::string name;    // --name
    std::string profile = "chester";
};
// Destroyed in reverse order: the session, the registry, then the host.
struct OpenedBleSession {
    std::unique_ptr<ble::BleHost> host;
    std::unique_ptr<device::SessionRegistry> registry;
    std::unique_ptr<device::BleSession> session;
    const device::DeviceProfile* profile = nullptr;
    std::string description; // "CHESTER-ABC123 (F1:22:...)"
};
// The adapter check, the device (-a or a --name scan) and BleSession::open (not connected yet);
// the pairing prompt is shown through `print`. Errors: as `ble console`.
[[nodiscard]] Result<OpenedBleSession>
openBleSession(const Context& context, const BleDeviceSelection& selection, bool autoReconnect,
               std::optional<std::filesystem::path> consoleFile,
               std::function<void(std::string_view)> print);

} // namespace hwio::cli
