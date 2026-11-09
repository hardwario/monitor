// Plan 0039 A1 / A2: the curated STM32 parts, the `stm32` profile and the STM32 flash-write rules.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/profile.h"
#include "hwio/hw/jlink/programmer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace {

using hwio::device::ChipVendor;
using hwio::device::findStm32PartByChip;
using hwio::device::findStm32PartById;
using hwio::device::ProfileRegistry;
using hwio::device::stm32Parts;

const hwio::device::TargetProfile& chip(std::string_view id) {
    const auto* profile = ProfileRegistry::find("stm32");
    const auto* target = profile != nullptr ? hwio::device::findTarget(*profile, id) : nullptr;
    if (target == nullptr) {
        ADD_FAILURE() << "no stm32 target " << id;
        return ProfileRegistry::find("stm32")->targets.front();
    }
    return *target;
}

TEST(Stm32Profile, IsRegisteredWithOneTargetPerPart) {
    const auto* profile = ProfileRegistry::find("stm32");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->displayName, "STM32 chip");
    EXPECT_FALSE(profile->ble.has_value());
    EXPECT_TRUE(profile->firmwareCatalogUrl.empty());
    ASSERT_EQ(profile->targets.size(), stm32Parts().size());
    for (std::size_t i = 0; i < profile->targets.size(); ++i) {
        const auto& target = profile->targets[i];
        EXPECT_EQ(target.id, stm32Parts()[i].id);
        EXPECT_EQ(target.vendor, ChipVendor::Stm32);
        EXPECT_EQ(&target.parts.front(), &stm32Parts()[i]) << target.id;
        EXPECT_FALSE(target.connectionGuide.empty()) << target.id;
    }
    // ADR-0233: a tab of the profile uses its first target (no RTT console anywhere).
    EXPECT_EQ(hwio::device::consoleTargetId(*profile), profile->targets.front().id);
}

TEST(Stm32Profile, PartIdsAreUniqueLowerCaseAndBounded) {
    std::set<std::string_view> ids;
    for (const auto& part : stm32Parts()) {
        EXPECT_TRUE(ids.insert(part.id).second) << part.id;
        for (const char c : part.id) {
            EXPECT_FALSE(c >= 'A' && c <= 'Z') << part.id;
        }
        EXPECT_FALSE(std::string_view(part.hintSourceText).empty()) << part.id;
        EXPECT_FALSE(part.jlinkDevice.empty()) << part.id;
        // Phase 0: never the "(allow opt. bytes)" names, which map the option bytes as flash.
        EXPECT_EQ(part.jlinkDevice.find('('), std::string_view::npos) << part.id;
    }
    EXPECT_LE(stm32Parts().size(), hwio::device::maxStm32Parts);
}

// Phase 0 (ST cmsis_device_l0, OpenOCD, J-Link 9.84, CHESTER-S1).
TEST(Stm32Profile, Stm32L0PartTable) {
    struct Expected {
        std::string_view id;
        std::uint16_t devId;
        std::uint16_t kib;
        std::string_view jlink;
        std::uint64_t eeprom; // 0 = not supported
        std::uint64_t ramKib;
    };
    constexpr std::array table{
        Expected{.id = "stm32l0-cat1-8k",
                 .devId = 0x457,
                 .kib = 8,
                 .jlink = "STM32L011K3",
                 .eeprom = 512,
                 .ramKib = 2},
        Expected{.id = "stm32l0-cat1-16k",
                 .devId = 0x457,
                 .kib = 16,
                 .jlink = "STM32L011K4",
                 .eeprom = 512,
                 .ramKib = 2},
        Expected{.id = "stm32l0-cat2-16k",
                 .devId = 0x425,
                 .kib = 16,
                 .jlink = "STM32L031K4",
                 .eeprom = 1024,
                 .ramKib = 8},
        Expected{.id = "stm32l0-cat2-32k",
                 .devId = 0x425,
                 .kib = 32,
                 .jlink = "STM32L031K6",
                 .eeprom = 1024,
                 .ramKib = 8},
        Expected{.id = "stm32l0-cat3-32k",
                 .devId = 0x417,
                 .kib = 32,
                 .jlink = "STM32L051K6",
                 .eeprom = 2048,
                 .ramKib = 8},
        Expected{.id = "stm32l0-cat3-64k",
                 .devId = 0x417,
                 .kib = 64,
                 .jlink = "STM32L053R8",
                 .eeprom = 2048,
                 .ramKib = 8},
        Expected{.id = "stm32l0-cat5-64k",
                 .devId = 0x447,
                 .kib = 64,
                 .jlink = "STM32L071K8",
                 .eeprom = 0,
                 .ramKib = 20},
        Expected{.id = "stm32l0-cat5-128k",
                 .devId = 0x447,
                 .kib = 128,
                 .jlink = "STM32L072CB",
                 .eeprom = 6144,
                 .ramKib = 20},
        Expected{.id = "stm32l0-cat5-192k",
                 .devId = 0x447,
                 .kib = 192,
                 .jlink = "STM32L072CZ",
                 .eeprom = 6144,
                 .ramKib = 20},
    };
    ASSERT_EQ(stm32Parts().size(), table.size());
    for (const Expected& row : table) {
        const auto* part = findStm32PartById(row.id);
        ASSERT_NE(part, nullptr) << row.id;
        ASSERT_TRUE(part->stm32.has_value());
        const hwio::device::Stm32Id id = part->stm32.value_or(hwio::device::Stm32Id{});
        EXPECT_EQ(id.devId, row.devId) << row.id;
        EXPECT_EQ(id.flashSizeKib, row.kib) << row.id;
        EXPECT_EQ(part->jlinkDevice, row.jlink) << row.id;
        EXPECT_EQ(findStm32PartByChip(row.devId, row.kib), part) << row.id;
        const auto& target = chip(row.id);
        EXPECT_EQ(target.dataEeprom.has_value(), row.eeprom != 0) << row.id;
        if (target.dataEeprom) {
            EXPECT_EQ(target.dataEeprom->start, 0x08080000U) << row.id;
            EXPECT_EQ(target.dataEeprom->size(), row.eeprom) << row.id;
        }
        EXPECT_EQ(target.ram.start, 0x20000000U) << row.id;
        EXPECT_EQ(target.ram.size(), row.ramKib * 1024) << row.id;
        ASSERT_TRUE(target.stm32.has_value());
        const hwio::jlink::Stm32Protection stm32 =
            target.stm32.value_or(hwio::jlink::Stm32Protection{});
        EXPECT_EQ(stm32.idcodeAddress, 0x40015800U);
        EXPECT_EQ(stm32.flashSizeAddress, 0x1FF8007CU);
        EXPECT_EQ(stm32.flashInterface + stm32.optrOffset, 0x4002201CU);
        EXPECT_EQ(stm32.optionBytes, 0x1FF80000U);
    }
    // Without the flash size the DEV_ID alone is ambiguous inside a category.
    EXPECT_EQ(findStm32PartByChip(0x417, 0), nullptr);
    EXPECT_EQ(findStm32PartByChip(0x417, 128), nullptr);
    EXPECT_EQ(findStm32PartByChip(0x123, 64), nullptr);
    EXPECT_EQ(findStm32PartById("nope"), nullptr);
}

TEST(Stm32Profile, FindPartOnATarget) {
    const auto& target = chip("stm32l0-cat3-64k");
    EXPECT_NE(hwio::device::findStm32Part(target, 0x417, 64), nullptr);
    EXPECT_NE(hwio::device::findStm32Part(target, 0x417, 0), nullptr); // size unknown: one part
    EXPECT_EQ(hwio::device::findStm32Part(target, 0x417, 32), nullptr);
    EXPECT_EQ(hwio::device::findStm32Part(target, 0x447, 64), nullptr);
    // FICR part numbers never match an STM32 part (part = 0).
    EXPECT_EQ(hwio::device::findPart(target, 0), nullptr);
}

// RDP levels and the canonical level-0 word (ADR-0276, ADR-0282).
TEST(Stm32Profile, RdpHelpers) {
    EXPECT_EQ(hwio::jlink::stm32RdpLevelOf(0xAA), 0);
    EXPECT_EQ(hwio::jlink::stm32RdpLevelOf(0xBB), 1);
    EXPECT_EQ(hwio::jlink::stm32RdpLevelOf(0x00), 1);
    EXPECT_EQ(hwio::jlink::stm32RdpLevelOf(0xCC), 2);
    EXPECT_EQ(hwio::jlink::stm32Level0OptionWord(0x807000BBU), 0xFF5500AAU);
    EXPECT_EQ(hwio::jlink::stm32Level0OptionWord(0x807001BBU), 0xFE5501AAU); // byte 1 kept
}

std::optional<hwio::ErrorCode> flashWrite(std::string_view id, std::uint32_t address,
                                          std::size_t size, bool allowEeprom) {
    const hwio::Status status = hwio::device::checkFlashWrite(chip(id), address, size, allowEeprom);
    if (status) {
        return std::nullopt;
    }
    return status.error().code;
}

constexpr std::optional<hwio::ErrorCode> outOfRange{hwio::ErrorCode::FirmwareOutOfRange};

TEST(Stm32Profile, FlashWriteRules) {
    // Flash: inside only.
    EXPECT_EQ(flashWrite("stm32l0-cat3-64k", 0x08000000, 0x10000, false), std::nullopt);
    EXPECT_EQ(flashWrite("stm32l0-cat3-64k", 0x0800FFFF, 2, false), outOfRange);
    EXPECT_EQ(flashWrite("stm32l0-cat3-32k", 0x08008000, 4, false), outOfRange);
    // Data EEPROM: only with consent, only inside.
    EXPECT_EQ(flashWrite("stm32l0-cat3-64k", 0x08080000, 16, false), outOfRange);
    EXPECT_EQ(flashWrite("stm32l0-cat3-64k", 0x08080000, 16, true), std::nullopt);
    EXPECT_EQ(flashWrite("stm32l0-cat3-64k", 0x080807F0, 32, true), outOfRange);
    EXPECT_EQ(flashWrite("stm32l0-cat5-192k", 0x08081000, 0x800, true), std::nullopt);
    // cat. 5 64 KiB: EEPROM not supported.
    EXPECT_EQ(flashWrite("stm32l0-cat5-64k", 0x08080000, 4, true), outOfRange);
    // Option bytes, system memory, RAM and address 0 never.
    for (const std::uint32_t address : {0x1FF80000U, 0x1FF00000U, 0x20000000U, 0x00000000U}) {
        EXPECT_EQ(flashWrite("stm32l0-cat5-192k", address, 4, true), outOfRange) << address;
    }
}

TEST(Stm32Profile, EepromMessageNamesTheReason) {
    const auto status =
        hwio::device::checkFlashWrite(chip("stm32l0-cat5-64k"), 0x08080000, 4, true);
    ASSERT_FALSE(status);
    EXPECT_NE(status.error().message.find("Data EEPROM is not supported"), std::string::npos)
        << status.error().message;
    const auto consent =
        hwio::device::checkFlashWrite(chip("stm32l0-cat3-64k"), 0x08080000, 4, false);
    ASSERT_FALSE(consent);
    EXPECT_NE(consent.error().message.find("data EEPROM"), std::string::npos);
}

// Memory writes (ADR-0160): RAM and peripherals, never the FLASH interface.
TEST(Stm32Profile, MemoryWriteRanges) {
    const auto& target = chip("stm32l0-cat5-192k");
    EXPECT_TRUE(hwio::device::checkMemoryWrite(target, 0x20000000, 4));
    EXPECT_TRUE(hwio::device::checkMemoryWrite(target, 0x50000000, 4));  // GPIOA
    EXPECT_FALSE(hwio::device::checkMemoryWrite(target, 0x40022004, 4)); // FLASH_PECR
    EXPECT_FALSE(hwio::device::checkMemoryWrite(target, 0x1FF80000, 4)); // option bytes
    EXPECT_FALSE(hwio::device::checkMemoryWrite(target, 0x08000000, 4)); // flash
    EXPECT_FALSE(hwio::device::checkMemoryWrite(target, 0xE000ED0C, 4)); // SCB AIRCR
}

TEST(Stm32Profile, DesignatedFirmwareOfAGenericPartIsNone) {
    EXPECT_FALSE(hwio::device::designatedFirmwareOf(chip("stm32l0-cat3-64k")).has_value());
}

} // namespace
