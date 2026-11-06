// The generic "STM32 chip" device (plan/features/0039, ADR-0274, ADR-0279): one target per curated
// STM32 part, flashed with the user's own file. No RTT console, no BLE, no firmware catalog.

#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_image.h"
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

// STM32L0 memory-write ranges (ADR-0160, ADR-0164; ST cmsis_device_l0): the APB / AHB peripherals
// 0x40000000-0x40026400 and the IOPORT GPIOs 0x50000000-0x50002000, the Cortex-M0+ Private
// Peripheral Bus; never the FLASH interface (its PECR / option-byte keys reach flash, data EEPROM
// and the option bytes), the BPU (FPB) and the System Control Space.
constexpr std::array l0Peripherals{
    firmware::AddressRange{.start = 0x40000000, .end = 0x40026400},
    firmware::AddressRange{.start = 0x50000000, .end = 0x50002000},
    firmware::AddressRange{.start = 0xE0000000, .end = 0xE0100000},
};
constexpr std::array l0WriteDenied{
    firmware::AddressRange{.start = 0x40022000, .end = 0x40022400}, // FLASH interface
    firmware::AddressRange{.start = 0xE0002000, .end = 0xE0003000}, // BPU (FPB)
    firmware::AddressRange{.start = 0xE000E000, .end = 0xE000F000}, // System Control Space
};

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

} // namespace

// The STM32 target of curated part `index` (also used by CHESTER targets, plan 0040).
consteval TargetProfile stm32Target(std::size_t index, std::string_view id,
                                    std::string_view displayName) {
    const ChipPart& part = stm32PartList.at(index);
    const Stm32Memory& memory = stm32PartMemory.at(index);
    return TargetProfile{.id = id,
                         .displayName = displayName,
                         .jlinkDevice = part.jlinkDevice,
                         .rttTerminal = "",
                         .rttLogger = "",
                         .hashPrefixIsLog = false,
                         .defaultSpeedKhz = std::nullopt, // automatic (ADR-0090)
                         .defaultLatencyMs = 50,
                         .minTargetMillivolts = 1000,
                         .parts = std::span(stm32PartList).subspan(index, 1),
                         .chipFamily = {},
                         .flash = memory.flash,
                         .uicr = {},
                         .pibOffset = 0,
                         .pibErasableWithUicr = false,
                         .recoverImage = RecoverImage::None,
                         .defaultErase = EraseScope::All,
                         .connectionGuide = chipGuide,
                         .ram = memory.ram,
                         .peripherals = l0Peripherals,
                         .writeDenied = l0WriteDenied,
                         .shellPrompt = "",
                         .designatedFirmware = std::nullopt,
                         .sensitiveCommandPrefixes = {},
                         .sensitiveOutputKeys = {},
                         .resetReason = std::nullopt,
                         .vendor = ChipVendor::Stm32,
                         .stm32 = stm32l0Family,
                         .dataEeprom = memory.dataEeprom};
}

namespace {

consteval std::array<TargetProfile, stm32PartList.size()> chipTargets() {
    std::array<TargetProfile, stm32PartList.size()> targets{};
    for (std::size_t i = 0; i < stm32PartList.size(); ++i) {
        targets.at(i) = stm32Target(i, stm32PartList.at(i).id, stm32PartList.at(i).displayName);
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
    .displayName = "STM32 chip",
    .targets = stm32Targets,
    .firmwareCatalogUrl = "",
    .powerHintSourceText = QT_TRANSLATE_NOOP(
        "hwio::device", "Power the board of the chip; the J-Link measures its voltage on VTref."),
    .ble = std::nullopt,
    .firmwareDocsUrl = {}};

} // namespace profiles

} // namespace hwio::device
