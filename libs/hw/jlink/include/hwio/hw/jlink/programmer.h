#pragma once

// IProgrammer - programming an MCU through one probe (plan/features/0003-m2-flashing.md,
// ADR-0015, ADR-0035, ADR-0039). JLinkProgrammer implements it on IJLinkApi; it is used only on
// the session worker (ADR-0031), after the worker connected the target.
//
//   program  BeginDownload -> WriteMem in writeChunk pieces -> EndDownload (the flash loader
//            erases the touched sectors and programs them; < 0 -> target.program_failed)
//   verify   ReadMemEx of every written range in verifyChunk pieces, byte compare (first
//            mismatch -> firmware.verify_failed, the address in `detail`)
//   eraseCode  0xFF over the flash range through the flash loader (sectors that differ are
//            erased; UICR untouched; confirmed on an nRF52840 in Phase 0, G1 EraseCode gate).
//   eraseAll reset + halt, then JLINK_EraseChip (flash + UICR; HIL H5: after only a halt it
//            erased nothing on a running nRF52840)
//   recover  checks the expected family's CTRL-AP IDR, then CTRL-AP ERASEALL via CoreSight, poll
//            ERASEALLSTATUS, CTRL-AP RESET toggle
//   target operations (M6, plan/features/0013, ADR-0099, ADR-0158): halt / go (JLINKARM_Halt /
//            Go), isHalted, coreId, readMemory with an access width (8: ReadMemEx, 16: ReadMemEx
//            with access width 2, 32: ReadMemU32 with a count), writeMemory with a width (8:
//            JLINKARM_WriteMem outside a download, 16 / 32: WriteU16 / WriteU32 per unit), units
//            little-endian; readRegisters (the core must be halted), memoryZones. A missing
//            optional library function (ADR-0151) -> dependency.jlink_function_missing (detail:
//            the function name).
//   identify FICR INFO.PART / INFO.VARIANT; when FICR cannot be read, the CTRL-AP of the expected
//            family (a Nordic CTRL-AP IDR at its AP index first, then APPROTECTSTATUS); a Nordic
//            CTRL-AP at another family's AP index only names that family (never "protected").
//            STM32 (plan/features/0039, ProgrammerConfig::stm32): DBGMCU_IDCODE, the RDP byte of
//            FLASH_OPTR, FLASH_SR and the RDP option word, then F_SIZE; each read retried up to
//            stm32ReadAttempts times while it fails or reads 0 (Phase 0 P0.3: a running STM32L0
//            reads 0 now and then). Readable at RDP level 1 except F_SIZE may not be (0 =
//            unknown). No halt, no write.
//   programRdpLevel0 (STM32 only, ADR-0276, ADR-0282): clears the FLASH_SR error flags, unlocks
//            PECR and the option bytes, writes the RDP option word with RDP 0xAA (byte 1 kept from
//            FLASH_OPTR, complement in bits 31:16), waits for BSY and launches the option-byte
//            reload (the chip resets; at RDP level 1 it mass-erases flash and data EEPROM). 0xCC is
//            never written. The caller reconnects and identifies afterwards.
//
// Cancellation: `stop` is checked before a download starts and between verify chunks. The
// buffering of one download, EndDownload, EraseChip and an ERASEALL are never interrupted (a
// buffered download cannot be discarded through the library). Deadlines (timing below) are
// checked between chunks; a single library call is synchronous. A download that fails or runs out
// of time before EndDownload is abandoned by closing the probe (review R3); the library has no
// abort, and Phase 0 (item 0.10) showed that the close still programs the buffered data, so this is
// reported as an interrupted flash: target.program_failed, "may be partly written" (ADR-0109). The
// session reopens the probe.
//
// Thread safety: none (the session worker).

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/program_progress.h"
#include "hwio/hw/jlink/clock.h"
#include "hwio/hw/jlink/jlink_api.h"
#include "hwio/hw/jlink/jlink_probe.h"
#include "hwio/hw/jlink/types.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::jlink {

enum class ResetMode : std::uint8_t {
    Go,   // reset and let the CPU run
    Halt, // reset and keep the CPU halted
};

// What identify() found.
struct ChipInfo {
    std::uint32_t part = 0;     // FICR INFO.PART (0x52840, 0x9160, 0x9120, ...); 0 when protected
    std::uint32_t variant = 0;  // FICR INFO.VARIANT (ASCII, e.g. "AAD0")
    bool protectedChip = false; // CTRL-AP reports APPROTECT enabled; FICR not readable
    std::string name;           // display name ("nRF52840"), filled by the device layer
    // The family that answered (ChipFamily::name, e.g. "nRF52"): from FICR, or - when FICR cannot
    // be read (part = 0) - the family whose CTRL-AP index holds a Nordic CTRL-AP. STM32: the
    // Stm32Protection::family ("STM32L0").
    std::string_view family;
    // ---- STM32 (plan/features/0039): set by an STM32 identify(), 0 otherwise ----
    std::uint16_t devId = 0;        // DBGMCU_IDCODE bits 11:0 (0x417 = STM32L0 cat. 3)
    std::uint16_t revId = 0;        // DBGMCU_IDCODE bits 31:16
    std::uint16_t flashSizeKib = 0; // F_SIZE; 0 = not readable
    int rdpLevel = 0;               // 0, 1 or 2 (protectedChip = rdpLevel >= 1)
    std::uint32_t optionWord = 0;   // the stored RDP option word (canonical: complement in 31:16)
    bool optionError = false;       // FLASH_SR OPTVERR: the last option-byte load failed
};

// STM32 (plan/features/0039, ADR-0279): identification addresses and the flash interface of one
// STM32 family, data only (ADR-0032). STM32L0 values (Phase 0, ST cmsis_device_l0 /
// stm32l0xx_hal_driver): IDCODE 0x40015800, F_SIZE 0x1FF8007C, FLASH 0x40022000 with PECR +0x04,
// PEKEYR +0x0C, OPTKEYR +0x14, SR +0x18, OPTR +0x1C, option bytes 0x1FF80000.
struct Stm32Protection {
    std::string_view family;                // "STM32L0" (logs, ChipInfo::family)
    std::uint32_t idcodeAddress = 0;        // DBGMCU_IDCODE
    std::uint32_t flashSizeAddress = 0;     // F_SIZE (16 bit, KiB, in the low half of the word)
    std::uint32_t flashInterface = 0;       // FLASH register base
    std::uint32_t pecrOffset = 0;           // PECR: PELOCK, OPTLOCK, OBL_LAUNCH
    std::uint32_t peKeyOffset = 0;          // PEKEYR
    std::uint32_t optKeyOffset = 0;         // OPTKEYR
    std::uint32_t srOffset = 0;             // SR: BSY, error flags (write 1 to clear)
    std::uint32_t optrOffset = 0;           // OPTR: RDP in bits 7:0
    std::array<std::uint32_t, 2> peKeys{};  // written to PEKEYR in order
    std::array<std::uint32_t, 2> optKeys{}; // written to OPTKEYR in order
    std::uint32_t peLockMask = 0;           // PECR.PELOCK
    std::uint32_t optLockMask = 0;          // PECR.OPTLOCK
    std::uint32_t oblLaunchMask = 0;        // PECR.OBL_LAUNCH
    std::uint32_t busyMask = 0;             // SR.BSY
    std::uint32_t errorMask = 0;            // SR error flags cleared before an option write
    std::uint32_t optionErrorMask = 0;      // SR.OPTVERR
    std::uint32_t optionBytes = 0;          // the RDP option word
    // Where the family's data EEPROM lies (L0: 0x08080000-0x08082000), for the error text of an
    // image with EEPROM records on a part whose EEPROM is not supported.
    firmware::AddressRange dataEepromArea{};
};

// RDP byte values (ST: OB_RDP_LEVEL_0 / OB_RDP_LEVEL_2; anything else is level 1).
inline constexpr std::uint8_t stm32RdpLevel0 = 0xAA;
inline constexpr std::uint8_t stm32RdpLevel2 = 0xCC;
// Attempts of each STM32 identification read (plan 0039 Bounds: ID read retries 3).
inline constexpr int stm32ReadAttempts = 3;

// The level of an RDP byte.
[[nodiscard]] constexpr int stm32RdpLevelOf(std::uint8_t rdp) noexcept {
    return rdp == stm32RdpLevel0 ? 0 : (rdp == stm32RdpLevel2 ? 2 : 1);
}

// The canonical RDP option word for level 0: `optr` byte 1 kept, RDP 0xAA, complement in 31:16.
[[nodiscard]] constexpr std::uint32_t stm32Level0OptionWord(std::uint32_t optr) noexcept {
    const std::uint32_t low = (optr & 0xFF00U) | stm32RdpLevel0;
    return ((~low & 0xFFFFU) << 16U) | low;
}

// Where an MCU family keeps its FICR and its CTRL-AP (nRF52: FICR 0x10000100/0x10000104, CTRL-AP
// index 1; nRF91: FICR 0x00FF0140/0x00FF0144, CTRL-AP index 4 - AP indexes to confirm in
// Phase 0). `ctrlApIdr`: the CTRL-AP identification register (bank 0xF, 0xFC) compared under
// ctrlApIdrMask (revision bits 31:28 ignored): nRF52840 0x02880000, nRF91 0x12880000 (product
// specifications; to confirm in Phase 0). Review R15: under the mask both values are the same
// Nordic CTRL-AP ID, so the IDR only proves that a Nordic CTRL-AP sits at that AP index; the AP
// index (1 vs 4) is what tells the families apart. An AP that does not exist reads 0 or fails.
struct ChipFamily {
    std::string_view name; // "nRF52", "nRF91" (logs)
    std::uint32_t ficrPart = 0;
    std::uint32_t ficrVariant = 0;
    std::uint8_t ctrlApIndex = 0;
    std::uint32_t ctrlApIdr = 0;
};

inline constexpr std::uint32_t ctrlApIdrMask = 0x0FFFFFFFU;

struct ProgrammerTiming {
    std::chrono::milliseconds programTimeout{std::chrono::minutes(5)};
    std::chrono::milliseconds verifyTimeout{std::chrono::minutes(5)};
    std::chrono::milliseconds eraseAllTimeout{std::chrono::seconds(60)};
    std::chrono::milliseconds recoverTimeout{std::chrono::seconds(60)};
    std::chrono::milliseconds recoverPoll{100};
    // STM32 option-byte write (programRdpLevel0): BSY must clear within this time.
    std::chrono::milliseconds optionWriteTimeout{std::chrono::seconds(5)};
    std::chrono::milliseconds optionWritePoll{10};
    std::size_t writeChunk = 4096;
    std::size_t verifyChunk = 16384;
};

struct ProgrammerConfig {
    // identify() tries them in order; the first one is the expected family (its CTRL-AP is used
    // by recover() and for the protection check).
    std::vector<ChipFamily> families;
    ProgrammerTiming timing;
    // Set for an STM32 target (plan/features/0039): identify() reads the STM32 registers instead
    // of FICR and `families` is unused.
    std::optional<Stm32Protection> stm32 = std::nullopt;
};

// Largest readMemory() request (plan/features/0003 Bounds).
inline constexpr std::size_t maxReadMemoryBytes = 64ULL * 1024;
// Largest writeMemory() request (plan/features/0013 Bounds).
inline constexpr std::size_t maxWriteMemoryBytes = 64ULL * 1024;
// readRegisters() returns at most this many registers, memoryZones() at most this many zones
// (plan/features/0013 Bounds).
inline constexpr std::size_t maxRegisters = 128;
inline constexpr std::size_t maxMemoryZones = 32;

// The unit of a memory access (plan/features/0013); the value is the unit size in bytes.
// Re-exported as hwio::device::AccessWidth.
enum class AccessWidth : std::uint8_t { Bits8 = 1, Bits16 = 2, Bits32 = 4 };

[[nodiscard]] constexpr std::size_t unitBytes(AccessWidth width) noexcept {
    return static_cast<std::size_t>(width);
}

class IProgrammer {
public:
    IProgrammer() = default;
    virtual ~IProgrammer() = default;
    IProgrammer(const IProgrammer&) = delete;
    IProgrammer& operator=(const IProgrammer&) = delete;
    IProgrammer(IProgrammer&&) = delete;
    IProgrammer& operator=(IProgrammer&&) = delete;

    // Errors: target.connect_failed (FICR unreadable and the chip is not protected).
    [[nodiscard]] virtual Result<ChipInfo> identify() = 0;
    // Errors: target.program_failed, timeout, cancelled.
    [[nodiscard]] virtual Status program(const firmware::FirmwareImage& image,
                                         const firmware::ProgressFn& progress,
                                         std::stop_token stop) = 0;
    // Errors: firmware.verify_failed, target.connect_failed (read failed), timeout, cancelled.
    [[nodiscard]] virtual Status verify(const firmware::FirmwareImage& image,
                                        const firmware::ProgressFn& progress,
                                        std::stop_token stop) = 0;
    // Errors: target.erase_failed, target.program_failed (abandoned download, ADR-0109), timeout,
    // cancelled.
    [[nodiscard]] virtual Status eraseCode(firmware::AddressRange flash,
                                           const firmware::ProgressFn& progress,
                                           std::stop_token stop) = 0;
    // Flash + UICR, after a reset and halt (HIL H5). Errors: target.connect_failed (reset failed),
    // target.erase_failed, timeout.
    [[nodiscard]] virtual Status eraseAll() = 0;
    // CTRL-AP ERASEALL. Errors: target.recover_failed, timeout, cancelled (only before it starts).
    [[nodiscard]] virtual Status recover(std::stop_token stop) = 0;
    // STM32 only (see the header comment). Errors: usage.invalid_argument (not an STM32 target),
    // target.unlock_failed (locked registers, a write failed, BSY did not clear),
    // dependency.jlink_function_missing (JLINKARM_WriteU32), cancelled (only before it starts).
    [[nodiscard]] virtual Status programRdpLevel0(std::stop_token stop) = 0;
    // At most maxReadMemoryBytes, `size` a multiple of the width and `address` aligned to it;
    // 16/32-bit units are returned little-endian. Errors: usage.invalid_argument,
    // target.memory_access_failed (detail: address and the vendor code).
    [[nodiscard]] virtual Result<std::vector<std::byte>>
    readMemory(std::uint32_t address, std::size_t size, AccessWidth width) = 0;
    // Errors: target.connect_failed.
    [[nodiscard]] virtual Status reset(ResetMode mode) = 0;

    // ---- target operations (M6, plan/features/0013, ADR-0158) ----
    // At most maxWriteMemoryBytes, size and alignment as readMemory(); `data` holds the units
    // little-endian. Errors: usage.invalid_argument, target.memory_access_failed,
    // dependency.jlink_function_missing (16/32-bit units).
    [[nodiscard]] virtual Status writeMemory(std::uint32_t address, std::span<const std::byte> data,
                                             AccessWidth width) = 0;
    // JLINKARM_Halt. Errors: target.connect_failed.
    [[nodiscard]] virtual Status halt() = 0;
    // JLINKARM_Go (never fails in the library API).
    [[nodiscard]] virtual Status go() = 0;
    // Errors: target.connect_failed, dependency.jlink_function_missing.
    [[nodiscard]] virtual Result<bool> isHalted() = 0;
    // Errors: dependency.jlink_function_missing.
    [[nodiscard]] virtual Result<std::uint32_t> coreId() = 0;
    // At most maxRegisters, in the library's order. Errors: target.not_halted,
    // target.connect_failed, dependency.jlink_function_missing.
    [[nodiscard]] virtual Result<std::vector<RegisterValue>> readRegisters() = 0;
    // At most maxMemoryZones (empty when the library reports none). Errors:
    // target.connect_failed, dependency.jlink_function_missing.
    [[nodiscard]] virtual Result<std::vector<MemoryZone>> memoryZones() = 0;
};

class JLinkProgrammer final : public IProgrammer {
public:
    // `api`, `probe` and `clock` must outlive the programmer.
    JLinkProgrammer(IJLinkApi& api, JLinkProbe& probe, Clock& clock, ProgrammerConfig config);

    [[nodiscard]] Result<ChipInfo> identify() override;
    [[nodiscard]] Status program(const firmware::FirmwareImage& image,
                                 const firmware::ProgressFn& progress,
                                 std::stop_token stop) override;
    [[nodiscard]] Status verify(const firmware::FirmwareImage& image,
                                const firmware::ProgressFn& progress,
                                std::stop_token stop) override;
    [[nodiscard]] Status eraseCode(firmware::AddressRange flash,
                                   const firmware::ProgressFn& progress,
                                   std::stop_token stop) override;
    [[nodiscard]] Status eraseAll() override;
    [[nodiscard]] Status recover(std::stop_token stop) override;
    [[nodiscard]] Status programRdpLevel0(std::stop_token stop) override;
    static constexpr std::uint8_t ctrlApIdr = 3;         // 0x0FC IDR (bank 0xF)
    static constexpr std::uint32_t idrBank = 0xFU << 4U; // DP SELECT APBANKSEL = 0xF
    [[nodiscard]] Result<std::vector<std::byte>> readMemory(std::uint32_t address, std::size_t size,
                                                            AccessWidth width) override;
    [[nodiscard]] Status reset(ResetMode mode) override;
    [[nodiscard]] Status writeMemory(std::uint32_t address, std::span<const std::byte> data,
                                     AccessWidth width) override;
    [[nodiscard]] Status halt() override;
    [[nodiscard]] Status go() override;
    [[nodiscard]] Result<bool> isHalted() override;
    [[nodiscard]] Result<std::uint32_t> coreId() override;
    [[nodiscard]] Result<std::vector<RegisterValue>> readRegisters() override;
    [[nodiscard]] Result<std::vector<MemoryZone>> memoryZones() override;

    // CTRL-AP registers (bank 0, register index = offset / 4) [nRF52840 / nRF91 product specs].
    static constexpr std::uint8_t ctrlApReset = 0;          // 0x000 RESET
    static constexpr std::uint8_t ctrlApEraseAll = 1;       // 0x004 ERASEALL
    static constexpr std::uint8_t ctrlApEraseAllStatus = 2; // 0x008 ERASEALLSTATUS (1 = busy)
    static constexpr std::uint8_t ctrlApApProtectStatus =
        3;                                      // 0x00C APPROTECTSTATUS (bit 0: 0 = protected)
    static constexpr std::uint8_t dpSelect = 2; // DP SELECT: APSEL in bits 31:24

private:
    [[nodiscard]] Result<ChipInfo> identifyStm32(const Stm32Protection& stm32);
    // One STM32 identification read with retries (0 or a failed read is retried); nullopt when
    // every attempt failed or read 0.
    [[nodiscard]] std::optional<std::uint32_t> readStm32Register(std::uint32_t address,
                                                                 std::string& tried);
    // Selects the CTRL-AP of `family` (bank 0). Returns the vendor code (< 0: error).
    [[nodiscard]] int selectCtrlAp(const ChipFamily& family);
    // nullopt when the CTRL-AP cannot be read.
    [[nodiscard]] std::optional<bool> isProtected(const ChipFamily& family);
    // The CTRL-AP of `family` answers with the expected IDR.
    [[nodiscard]] bool ctrlApPresent(const ChipFamily& family, std::string& detail);
    // The family whose CTRL-AP answers, starting with the expected one; nullptr when none.
    [[nodiscard]] const ChipFamily* answeringFamily(std::string& detail);
    // Closes the probe instead of calling EndDownload (review R3) and returns the
    // target.program_failed error of an interrupted flash (ADR-0109); `detail` names the reason.
    [[nodiscard]] Error abandonDownload(std::string detail);
    // One BeginDownload / WriteMem / EndDownload pass. `programsData`: the loader's non-erase
    // progress is reported as Programming (program), otherwise everything as Erasing (eraseCode).
    [[nodiscard]] Status download(const std::vector<firmware::Segment>& segments, bool programsData,
                                  ErrorCode failure, const firmware::ProgressFn& progress,
                                  const std::stop_token& stop);
    // A 32-bit STM32 write that refuses an RDP level 2 option word (ADR-0276); the vendor code.
    [[nodiscard]] int writeStm32(std::uint32_t address, std::uint32_t value);
    // target.unlock_failed with `detail` and the library's last message.
    [[nodiscard]] Error stm32UnlockFailed(std::string detail);
    // programRdpLevel0: clears the FLASH_SR error flags and unlocks PECR and the option bytes.
    [[nodiscard]] Status unlockStm32Flash(const Stm32Protection& stm32);
    // programRdpLevel0: waits until FLASH_SR BSY clears (optionWriteTimeout).
    [[nodiscard]] Status awaitStm32OptionWrite(const Stm32Protection& stm32);
    [[nodiscard]] std::string libraryDetail(std::string detail);
    // dependency.jlink_function_missing when `function` is not exported (ADR-0151).
    [[nodiscard]] Status require(OptionalFunction function);
    // target.memory_access_failed for `function` at `address` returning `code`.
    [[nodiscard]] Error memoryAccessFailed(std::string_view function, std::uint32_t address,
                                           std::size_t size, int code);

    IJLinkApi& m_api;
    JLinkProbe& m_probe;
    Clock& m_clock;
    ProgrammerConfig m_config;
};

} // namespace hwio::jlink
