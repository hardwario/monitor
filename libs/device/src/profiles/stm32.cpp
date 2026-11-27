// The generic "STM32 chip" device (plan/features/0039, ADR-0274, ADR-0279): one target per curated
// STM32 part, flashed with the user's own file. No RTT console, no BLE, no firmware catalog.

#include "hwio/device/profile.h"
#include "profiles.h"
#include "stm32_parts.h"

#include <QtTranslation>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace hwio::device {

std::span<const ChipPart> stm32Parts() noexcept { return profiles::stm32PartList; }

namespace profiles {

namespace {

constexpr std::array chipGuide{
    ConnectionGuideStep{
        .id = "cable",
        .illustrationSlot = "stm32-swd-cable",
        .titleSourceText = QT_TRANSLATE_NOOP("hwio::device", "Cable"),
        .bodySourceText = QT_TRANSLATE_NOOP(
            "hwio::device",
            "Connect the J-Link to the SWD pins of the chip: SWDIO, SWCLK, GND and VTref."),
    },
};

consteval std::array<TargetProfile, stm32PartList.size()> chipTargets() {
    std::array<TargetProfile, stm32PartList.size()> targets{};
    for (std::size_t i = 0; i < stm32PartList.size(); ++i) {
        targets.at(i) =
            stm32Target(i, stm32PartList.at(i).id, stm32PartList.at(i).displayName, chipGuide);
    }
    return targets;
}

constexpr std::array stm32Targets = chipTargets();

// Plan 0028 (ADR-0200): profile ids contain no '-'.
constexpr std::string_view stm32Id = "stm32";
static_assert(stm32Id.find('-') == std::string_view::npos, "profile ids contain no '-'");

} // namespace

constinit const DeviceProfile stm32{
    .id = stm32Id,
    .displayName = QT_TRANSLATE_NOOP("hwio::device", "STM32 chip"),
    .targets = stm32Targets,
    .firmwareCatalogUrl = "",
    .powerHintSourceText = QT_TRANSLATE_NOOP(
        "hwio::device", "Power the board of the chip; the J-Link measures its voltage on VTref."),
    .ble = std::nullopt,
    .firmwareDocsUrl = {}};

} // namespace profiles

} // namespace hwio::device
