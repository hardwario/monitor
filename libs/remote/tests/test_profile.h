#pragma once

// A device profile for the plan 0059 tests (not registered): one console target "app" with the
// ADR-0416 rules of a STICKER-like device (a secret printed without a colon, setters refused from
// remote, identity-destroying commands refused).

#include "hwio/device/profile.h"

#include <array>
#include <optional>
#include <string_view>

namespace hwio::remote::testing {

inline constexpr std::array<std::string_view, 1> testSensitiveCommands{"config secret-key"};
inline constexpr std::array<std::string_view, 1> testSensitiveOutputPrefixes{"config secret-key"};
inline constexpr std::array<std::string_view, 1> testRefusedSetters{"config serial-number"};
inline constexpr std::array<std::string_view, 2> testRefusedCommands{"settings erase", "nfc write"};

inline constexpr std::array testTargets{
    device::TargetProfile{.id = "app",
                          .displayName = "application",
                          .jlinkDevice = "",
                          .rttTerminal = "Terminal",
                          .rttLogger = "",
                          .hashPrefixIsLog = false,
                          .defaultSpeedKhz = std::nullopt,
                          .defaultLatencyMs = 50,
                          .minTargetMillivolts = 0,
                          .parts = {},
                          .chipFamily = {},
                          .flash = {},
                          .uicr = {},
                          .pibOffset = 0,
                          .pibErasableWithUicr = false,
                          .recoverImage = device::RecoverImage::None,
                          .defaultErase = device::EraseScope::Code,
                          .connectionGuide = {},
                          .ram = {},
                          .peripherals = {},
                          .writeDenied = {},
                          .shellPrompt = "",
                          .designatedFirmware = std::nullopt,
                          .sensitiveCommandPrefixes = testSensitiveCommands,
                          .sensitiveOutputKeys = {},
                          .resetReason = std::nullopt,
                          .sensitiveOutputPrefixes = testSensitiveOutputPrefixes,
                          .refuseRemoteSensitive = true,
                          .remoteRefusedSetters = testRefusedSetters,
                          .remoteRefusedCommands = testRefusedCommands},
};

inline constexpr device::DeviceProfile testProfile{.id = "testdev",
                                                   .displayName = "Test device",
                                                   .targets = testTargets,
                                                   .firmwareCatalogUrl = "",
                                                   .powerHintSourceText = "",
                                                   .ble = std::nullopt,
                                                   .firmwareDocsUrl = {}};

} // namespace hwio::remote::testing
