// The neutral console profile (plan 0059, ADR-0415): the console rules of a remote support tab
// whose host names a profile this build does not know. Not a device: not registered in
// ProfileRegistry (no picker, no flashing, no J-Link), only genericConsoleProfile().

#include "hwio/device/profile.h"
#include "profiles.h"

#include <QtTranslation>

#include <array>
#include <optional>
#include <string_view>

namespace hwio::device::profiles {

namespace {

// One console target "app" (RemoteConsoleSession::consoleTargetId()): a shell on the Terminal
// channel, no prompt assumption (the silence rule ends a query, ADR-0104), no '#' log rule (Zephyr
// log levels are parsed as on every console), no sensitive rules, nothing to flash.
constexpr std::array genericTargets{
    TargetProfile{.id = "app",
                  .displayName = QT_TRANSLATE_NOOP("hwio::device", "device"),
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
                  .recoverImage = RecoverImage::None,
                  .defaultErase = EraseScope::Code,
                  .connectionGuide = {},
                  .ram = {},
                  .peripherals = {},
                  .writeDenied = {},
                  .shellPrompt = "",
                  .designatedFirmware = std::nullopt,
                  .sensitiveCommandPrefixes = {},
                  .sensitiveOutputKeys = {},
                  .resetReason = std::nullopt},
};

} // namespace

constinit const DeviceProfile generic{.id = "generic",
                                      .displayName = "",
                                      .targets = genericTargets,
                                      .firmwareCatalogUrl = "",
                                      .powerHintSourceText = "",
                                      .ble = std::nullopt,
                                      .firmwareDocsUrl = {}};

} // namespace hwio::device::profiles
