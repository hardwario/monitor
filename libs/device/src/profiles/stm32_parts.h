#pragma once

// The curated STM32 parts (plan/features/0039, ADR-0275, ADR-0279) and the STM32L0 family data,
// shared by the generic `stm32` profile and CHESTER targets (plan 0040) so that a part is described
// once. Constant-initialized data (ADR-0032); inline constexpr, so every user refers to the same
// objects.
//
// Phase 0 (2026-11-06; sources: ST cmsis_device_l0 + stm32l0xx_hal_driver, OpenOCD stm32lx.c, the
// J-Link 9.84 device list, a CHESTER-S1 controller): one entry per category and flash size. Parts
// of one entry share DEV_ID, flash size and J-Link flash banks and cannot be told apart. The
// J-Link name is the plain one (never "(allow opt. bytes)", which adds the option bytes as a flash
// bank). The STM32L010 value line shares DEV_IDs but has a smaller data EEPROM: only named in the
// hint (ADR-0282).

#include "hwio/device/profile.h"
#include "hwio/firmware/designated_firmware.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/hw/jlink/programmer.h"

#include <QtTranslation>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hwio::device::profiles {

// STM32L0: DBGMCU_IDCODE, F_SIZE, the FLASH interface and the RDP option word.
inline constexpr jlink::Stm32Protection stm32l0Family{
    .family = "STM32L0",
    .idcodeAddress = 0x40015800,
    .flashSizeAddress = 0x1FF8007C,
    .flashInterface = 0x40022000,
    .pecrOffset = 0x04,
    .peKeyOffset = 0x0C,
    .optKeyOffset = 0x14,
    .srOffset = 0x18,
    .optrOffset = 0x1C,
    .peKeys = {0x89ABCDEF, 0x02030405},
    .optKeys = {0xFBEAD9C8, 0x24252627},
    .peLockMask = 1U << 0U,
    .optLockMask = 1U << 2U,
    .oblLaunchMask = 1U << 18U,
    .busyMask = 1U << 0U,
    // WRPERR, PGAERR, SIZERR, OPTVERR (bits 8-11), RDERR (13), NOTZEROERR (16), FWWERR (17)
    .errorMask = 0x00032F00,
    .optionErrorMask = 1U << 11U,
    .optionBytes = 0x1FF80000,
    .dataEepromArea = {.start = 0x08080000, .end = 0x08082000},
};

inline constexpr std::uint32_t stm32FlashStart = 0x08000000;
inline constexpr std::uint32_t stm32RamStart = 0x20000000;
inline constexpr std::uint32_t stm32EepromStart = 0x08080000;

// The memory map of one curated part (same index as stm32PartList).
struct Stm32Memory {
    std::string_view id; // equals stm32PartList[i].id (static_assert below)
    firmware::AddressRange flash;
    std::optional<firmware::AddressRange> dataEeprom;
    firmware::AddressRange ram;
};

namespace detail {
// Not constexpr and never defined: reached only from stm32PartIndex() with an unknown id, which
// then fails to compile ("not a constant expression").
void unknownStm32PartId();

constexpr ChipPart l0Part(std::string_view id, std::string_view name, std::string_view jlink,
                          std::uint16_t devId, std::uint16_t kib, const char* hint) {
    return ChipPart{.part = 0,
                    .displayName = name,
                    .jlinkDevice = jlink,
                    .stm32 = Stm32Id{.devId = devId, .flashSizeKib = kib},
                    .id = id,
                    .hintSourceText = hint,
                    .designatedFirmware = std::nullopt};
}
constexpr Stm32Memory l0Memory(std::string_view id, std::uint32_t flashKib,
                               std::uint32_t eepromBytes, std::uint32_t ramKib) {
    return Stm32Memory{
        .id = id,
        .flash = {.start = stm32FlashStart, .end = stm32FlashStart + (flashKib * 1024U)},
        .dataEeprom = eepromBytes == 0
                          ? std::nullopt
                          : std::optional<firmware::AddressRange>(firmware::AddressRange{
                                .start = stm32EepromStart, .end = stm32EepromStart + eepromBytes}),
        .ram = {.start = stm32RamStart, .end = stm32RamStart + (ramKib * 1024U)}};
}
} // namespace detail

// Display names are English source texts (QT_TRANSLATE_NOOP, context "hwio::device"): the same
// literal is the target's display name in the `stm32` profile.
inline constexpr std::array stm32PartList{
    detail::l0Part(
        "stm32l0-cat1-8k", QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 1, 8 KiB"),
        "STM32L011K3", 0x457, 8,
        QT_TRANSLATE_NOOP("hwio::device", "STM32L011 / L021 with 8 KiB flash. Not for STM32L010.")),
    detail::l0Part("stm32l0-cat1-16k", QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 1, 16 KiB"),
                   "STM32L011K4", 0x457, 16,
                   QT_TRANSLATE_NOOP("hwio::device",
                                     "STM32L011 / L021 with 16 KiB flash. Not for STM32L010.")),
    detail::l0Part("stm32l0-cat2-16k", QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 2, 16 KiB"),
                   "STM32L031K4", 0x425, 16,
                   QT_TRANSLATE_NOOP("hwio::device",
                                     "STM32L031 / L041 with 16 KiB flash. Not for STM32L010.")),
    detail::l0Part("stm32l0-cat2-32k", QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 2, 32 KiB"),
                   "STM32L031K6", 0x425, 32,
                   QT_TRANSLATE_NOOP("hwio::device",
                                     "STM32L031 / L041 with 32 KiB flash. Not for STM32L010.")),
    detail::l0Part("stm32l0-cat3-32k", QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 3, 32 KiB"),
                   "STM32L051K6", 0x417, 32,
                   QT_TRANSLATE_NOOP("hwio::device", "STM32L051 / L052 / L053 with 32 KiB flash. "
                                                     "Not for STM32L010.")),
    detail::l0Part("stm32l0-cat3-64k", QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 3, 64 KiB"),
                   "STM32L053R8", 0x417, 64,
                   QT_TRANSLATE_NOOP("hwio::device",
                                     "STM32L051 / L052 / L053 / L062 / L063 with 64 KiB flash, "
                                     "e.g. CHESTER-S1 (Clime IAQ). Not for STM32L010.")),
    detail::l0Part("stm32l0-cat5-64k", QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 5, 64 KiB"),
                   "STM32L071K8", 0x447, 64,
                   QT_TRANSLATE_NOOP("hwio::device",
                                     "STM32L071 / L072 / L073 / L083 with 64 KiB flash (data "
                                     "EEPROM not supported). Not for STM32L010.")),
    detail::l0Part(
        "stm32l0-cat5-128k", QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 5, 128 KiB"),
        "STM32L072CB", 0x447, 128,
        QT_TRANSLATE_NOOP("hwio::device", "STM32L071 / L072 / L073 / L081 / L082 / L083 with 128 "
                                          "KiB flash. Not for STM32L010.")),
    detail::l0Part("stm32l0-cat5-192k",
                   QT_TRANSLATE_NOOP("hwio::device", "STM32L0 cat. 5, 192 KiB"), "STM32L072CZ",
                   0x447, 192,
                   QT_TRANSLATE_NOOP("hwio::device",
                                     "STM32L071 / L072 / L073 / L081 / L082 / L083 with 192 KiB "
                                     "flash, e.g. Murata Type ABZ (CHESTER LoRaWAN).")),
};

// Data EEPROM: cat. 1 512 B, cat. 2 1 KiB, cat. 3 2 KiB, cat. 5 6 KiB (two banks, contiguous);
// cat. 5 64 KiB: J-Link maps 3 KiB at 0x08080C00, not confirmed by ST -> not supported.
inline constexpr std::array stm32PartMemory{
    detail::l0Memory("stm32l0-cat1-8k", 8, 512, 2),
    detail::l0Memory("stm32l0-cat1-16k", 16, 512, 2),
    detail::l0Memory("stm32l0-cat2-16k", 16, 1024, 8),
    detail::l0Memory("stm32l0-cat2-32k", 32, 1024, 8),
    detail::l0Memory("stm32l0-cat3-32k", 32, 2048, 8),
    detail::l0Memory("stm32l0-cat3-64k", 64, 2048, 8),
    detail::l0Memory("stm32l0-cat5-64k", 64, 0, 20),
    detail::l0Memory("stm32l0-cat5-128k", 128, 6144, 20),
    detail::l0Memory("stm32l0-cat5-192k", 192, 6144, 20),
};

static_assert(stm32PartList.size() == stm32PartMemory.size());
static_assert(stm32PartList.size() <= maxStm32Parts);
consteval bool stm32TablesAligned() {
    for (std::size_t i = 0; i < stm32PartList.size(); ++i) {
        if (stm32PartList.at(i).id != stm32PartMemory.at(i).id) {
            return false;
        }
    }
    return true;
}
static_assert(stm32TablesAligned(), "stm32PartList and stm32PartMemory differ");

// Index of a part id in the tables (compile time; an unknown id does not compile).
consteval std::size_t stm32PartIndex(std::string_view id) {
    for (std::size_t i = 0; i < stm32PartList.size(); ++i) {
        if (stm32PartList.at(i).id == id) {
            return i;
        }
    }
    detail::unknownStm32PartId();
    return 0;
}

// STM32L0 memory-write ranges (ADR-0160, ADR-0164; ST cmsis_device_l0): the APB / AHB peripherals
// 0x40000000-0x40026400 and the IOPORT GPIOs 0x50000000-0x50002000, the Cortex-M0+ Private
// Peripheral Bus; never the FLASH interface (its PECR / option-byte keys reach flash, data EEPROM
// and the option bytes), the BPU (FPB) and the System Control Space.
inline constexpr std::array stm32l0Peripherals{
    firmware::AddressRange{.start = 0x40000000, .end = 0x40026400},
    firmware::AddressRange{.start = 0x50000000, .end = 0x50002000},
    firmware::AddressRange{.start = 0xE0000000, .end = 0xE0100000},
};
inline constexpr std::array stm32l0WriteDenied{
    firmware::AddressRange{.start = 0x40022000, .end = 0x40022400}, // FLASH interface
    firmware::AddressRange{.start = 0xE0002000, .end = 0xE0003000}, // BPU (FPB)
    firmware::AddressRange{.start = 0xE000E000, .end = 0xE000F000}, // System Control Space
};

// The STM32 target of curated part `index` with its connection guide and designated firmware:
// the targets of the `stm32` profile and the CHESTER `lrw` target (plan 0040) are built from the
// same part data. No RTT console, mass erase by default (JLINK_EraseChip: flash and data EEPROM).
consteval TargetProfile
stm32Target(std::size_t index, std::string_view id, std::string_view displayName,
            std::span<const ConnectionGuideStep> guide,
            std::optional<firmware::DesignatedFirmware> designated = std::nullopt) {
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
                         .connectionGuide = guide,
                         .ram = memory.ram,
                         .peripherals = stm32l0Peripherals,
                         .writeDenied = stm32l0WriteDenied,
                         .shellPrompt = "",
                         .designatedFirmware = designated,
                         .sensitiveCommandPrefixes = {},
                         .sensitiveOutputKeys = {},
                         .resetReason = std::nullopt,
                         .vendor = ChipVendor::Stm32,
                         .stm32 = stm32l0Family,
                         .dataEeprom = memory.dataEeprom};
}

} // namespace hwio::device::profiles
