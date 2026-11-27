// Plan 0039 A3: STM32 identify, RDP read and the RDP level 0 option-byte sequence of
// JLinkProgrammer on a mocked IJLinkApi with a small STM32L0 register model.

#include "fake_clock.h"
#include "hwio/core/error.h"
#include "hwio/hw/jlink/jlink_api.h"
#include "hwio/hw/jlink/jlink_probe.h"
#include "hwio/hw/jlink/programmer.h"
#include "mock_jlink_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <map>
#include <stop_token>
#include <utility>
#include <vector>

namespace {

using hwio::ErrorCode;
using hwio::jlink::JLinkProbe;
using hwio::jlink::JLinkProgrammer;
using hwio::jlink::OptionalFunction;
using hwio::jlink::ProgrammerConfig;
using hwio::jlink::Stm32Protection;
using hwio::jlink::test::FakeClock;
using hwio::jlink::test::MockJLinkApi;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// The STM32L0 data of the device profile (libs/device/src/profiles/stm32_parts.h).
constexpr Stm32Protection l0{
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
    .errorMask = 0x00032F00,
    .optionErrorMask = 1U << 11U,
    .optionBytes = 0x1FF80000,
    .dataEepromArea = {.start = 0x08080000, .end = 0x08082000},
};

constexpr std::uint32_t idcode = 0x40015800;
constexpr std::uint32_t fsize = 0x1FF8007C;
constexpr std::uint32_t pecr = 0x40022004;
constexpr std::uint32_t pekeyr = 0x4002200C;
constexpr std::uint32_t optkeyr = 0x40022014;
constexpr std::uint32_t sr = 0x40022018;
constexpr std::uint32_t optr = 0x4002201C;
constexpr std::uint32_t optionWord = 0x1FF80000;

class Stm32Programmer : public ::testing::Test {
protected:
    Stm32Programmer() {
        // The CHESTER-S1 controller at RDP level 1 (Phase 0).
        m_regs = {{idcode, 0x10386417}, {fsize, 0x03920040}, {pecr, 0x00000007},
                  {sr, 0x0000000C},     {optr, 0x807000BB},  {optionWord, 0xFF4400BB}};
        ON_CALL(m_api, has(_)).WillByDefault(Return(true));
        ON_CALL(m_api, halt()).WillByDefault(Return(0));
        ON_CALL(m_api, readU32(_))
            .WillByDefault([this](std::uint32_t address) -> std::expected<std::uint32_t, int> {
                if (m_zeroReads > 0) {
                    --m_zeroReads;
                    return 0U;
                }
                const auto it = m_regs.find(address);
                if (it == m_regs.end()) {
                    return std::unexpected(-1);
                }
                return it->second;
            });
        ON_CALL(m_api, writeU32(_, _))
            .WillByDefault([this](std::uint32_t address, std::uint32_t v) {
                m_writes.emplace_back(address, v);
                if (address == pekeyr) {
                    m_keys.push_back(v);
                    if (m_keys == std::vector<std::uint32_t>{l0.peKeys[0], l0.peKeys[1]}) {
                        m_regs[pecr] &= ~1U;
                    }
                } else if (address == optkeyr && (m_regs[pecr] & 1U) == 0) {
                    m_optKeys.push_back(v);
                    if (m_optKeys == std::vector<std::uint32_t>{l0.optKeys[0], l0.optKeys[1]}) {
                        m_regs[pecr] &= ~4U;
                    }
                } else if (address == optionWord) {
                    if ((m_regs[pecr] & 4U) != 0) {
                        m_regs[sr] |= 0x100U; // WRPERR
                        return 0;
                    }
                    m_regs[optionWord] = v;
                } else if (address == sr) {
                    m_regs[sr] &= ~v;
                } else if (address == pecr) {
                    m_regs[pecr] = v;
                    return m_failLaunch ? -1 : 0;
                }
                return 0;
            });
    }

    JLinkProgrammer& programmer() { return m_programmer; }
    NiceMock<MockJLinkApi>& api() { return m_api; }
    std::map<std::uint32_t, std::uint32_t>& regs() { return m_regs; }
    [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint32_t>>& writes() const {
        return m_writes;
    }
    void zeroReads(int count) { m_zeroReads = count; }
    void failLaunch() { m_failLaunch = true; }

private:
    std::map<std::uint32_t, std::uint32_t> m_regs;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> m_writes;
    std::vector<std::uint32_t> m_keys;
    std::vector<std::uint32_t> m_optKeys;
    int m_zeroReads = 0;
    bool m_failLaunch = false;
    NiceMock<MockJLinkApi> m_api;
    FakeClock m_clock;
    JLinkProbe m_probe{m_api, m_clock};
    JLinkProgrammer m_programmer{m_api, m_probe, m_clock,
                                 ProgrammerConfig{.families = {}, .timing = {}, .stm32 = l0}};
};

TEST_F(Stm32Programmer, IdentifiesDevIdSizeAndRdpLevel) {
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip) << chip.error().detail;
    EXPECT_EQ(chip->family, "STM32L0");
    EXPECT_EQ(chip->devId, 0x417);
    EXPECT_EQ(chip->revId, 0x1038);
    EXPECT_EQ(chip->flashSizeKib, 64);
    EXPECT_EQ(chip->rdpLevel, 1);
    EXPECT_TRUE(chip->protectedChip);
    EXPECT_EQ(chip->optionWord, 0xFF4400BBU);
    EXPECT_FALSE(chip->optionError);
    EXPECT_EQ(chip->part, 0U);
}

TEST_F(Stm32Programmer, Levels0And2) {
    regs()[optr] = 0x807000AA;
    EXPECT_EQ(programmer().identify()->rdpLevel, 0);
    regs()[optr] = 0x807000CC;
    EXPECT_EQ(programmer().identify()->rdpLevel, 2);
    regs()[sr] = 0x80C; // OPTVERR
    EXPECT_TRUE(programmer().identify()->optionError);
}

// Phase 0 P0.3: a running STM32L0 reads 0 now and then; each read is retried.
TEST_F(Stm32Programmer, ZeroReadsAreRetried) {
    zeroReads(2);
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip);
    EXPECT_EQ(chip->devId, 0x417);
}

TEST_F(Stm32Programmer, UnreadableIdcodeFails) {
    regs().erase(idcode);
    const auto chip = programmer().identify();
    ASSERT_FALSE(chip);
    EXPECT_EQ(chip.error().code, ErrorCode::TargetConnectFailed);
    EXPECT_EQ(chip.error().message, "The chip ID could not be read (DBGMCU_IDCODE).");
}

TEST_F(Stm32Programmer, UnreadableFlashSizeIsUnknown) {
    regs().erase(fsize);
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip);
    EXPECT_EQ(chip->flashSizeKib, 0);
}

TEST_F(Stm32Programmer, IdentifyNeverWrites) {
    EXPECT_CALL(api(), writeU32(_, _)).Times(0);
    EXPECT_CALL(api(), halt()).Times(0);
    ASSERT_TRUE(programmer().identify());
}

TEST_F(Stm32Programmer, ProgramRdpLevel0Sequence) {
    ASSERT_TRUE(programmer().programRdpLevel0({}));
    // Clear SR errors, PECR keys, option keys, the RDP word, the reload.
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> expected{
        {sr, 0x00032F00},      {pekeyr, 0x89ABCDEF},  {pekeyr, 0x02030405},
        {optkeyr, 0xFBEAD9C8}, {optkeyr, 0x24252627}, {optionWord, 0xFF5500AA},
        {pecr, 1U << 18U},
    };
    EXPECT_EQ(writes(), expected);
    EXPECT_EQ(regs()[optionWord], 0xFF5500AAU);
}

TEST_F(Stm32Programmer, ByteOneOfTheOptionWordIsKept) {
    regs()[optr] = 0x807001BB;
    ASSERT_TRUE(programmer().programRdpLevel0({}));
    EXPECT_EQ(regs()[optionWord], 0xFE5501AAU);
}

// ADR-0276: no write of any test ever puts 0xCC into the RDP byte.
TEST_F(Stm32Programmer, NeverWritesLevel2) {
    regs()[optr] = 0x807000CC;
    ASSERT_TRUE(programmer().programRdpLevel0({}));
    for (const auto& [address, value] : writes()) {
        if (address == optionWord) {
            EXPECT_EQ(value & 0xFFU, 0xAAU);
        }
    }
}

TEST_F(Stm32Programmer, StillLockedFails) {
    ON_CALL(api(), writeU32(pekeyr, _)).WillByDefault(Return(0)); // keys ignored
    const auto status = programmer().programRdpLevel0({});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetUnlockFailed);
    for (const auto& [address, value] : writes()) {
        EXPECT_NE(address, optionWord) << "the option word must not be written while locked";
    }
}

TEST_F(Stm32Programmer, BusyThatNeverClearsTimesOut) {
    regs()[sr] = 0x0D; // BSY stays set
    ON_CALL(api(), writeU32(sr, _)).WillByDefault(Return(0));
    const auto status = programmer().programRdpLevel0({});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetUnlockFailed);
    EXPECT_NE(status.error().detail.find("BSY"), std::string::npos);
}

TEST_F(Stm32Programmer, AFailedReloadWriteIsExpected) {
    failLaunch(); // the chip resets during the PECR write
    EXPECT_TRUE(programmer().programRdpLevel0({}));
}

TEST_F(Stm32Programmer, CancelledBeforeItStarts) {
    std::stop_source stop;
    stop.request_stop();
    EXPECT_CALL(api(), writeU32(_, _)).Times(0);
    const auto status = programmer().programRdpLevel0(stop.get_token());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
}

TEST_F(Stm32Programmer, MissingWriteU32) {
    ON_CALL(api(), has(OptionalFunction::WriteU32)).WillByDefault(Return(false));
    const auto status = programmer().programRdpLevel0({});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::DependencyJLinkFunctionMissing);
}

TEST(Stm32ProgrammerConfig, NordicProgrammerRefusesTheUnlock) {
    NiceMock<MockJLinkApi> api;
    FakeClock clock;
    JLinkProbe probe{api, clock};
    JLinkProgrammer programmer{api, probe, clock, ProgrammerConfig{}};
    const auto status = programmer.programRdpLevel0({});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::UsageInvalidArgument);
}

} // namespace
