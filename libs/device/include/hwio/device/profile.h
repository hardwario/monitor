#pragma once

// Device profiles: compiled-in C++ data (ADR-0032). A profile describes a device and its targets
// (MCUs): J-Link device names, RTT buffer names, console rules, defaults and (M2,
// plan/features/0003) the memory layout and identification used for flashing. Profiles contain
// data only; behaviour differences are explicit typed fields (e.g. hashPrefixIsLog).
//
// Adding a device = one new file in src/profiles/ + one registration line in profile.cpp + tests.

#include "hwio/core/result.h"
#include "hwio/firmware/designated_firmware.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/hw/jlink/programmer.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hwio::device {

// Who made the MCU of a target (plan/features/0039, ADR-0274): decides identification, protection
// and the erase of the target.
enum class ChipVendor : std::uint8_t {
    Nordic, // FICR INFO.PART, CTRL-AP APPROTECT / recover, UICR and PIB
    Stm32,  // DBGMCU_IDCODE + F_SIZE, RDP option byte / unlock, data EEPROM
};

// How an STM32 part identifies itself (plan/features/0039, ADR-0275). Parts of one category and
// flash size share a DEV_ID and cannot be told apart; one part entry covers them (ADR-0279).
struct Stm32Id {
    std::uint16_t devId = 0;        // DBGMCU_IDCODE bits 11:0, e.g. 0x447
    std::uint16_t flashSizeKib = 0; // expected F_SIZE; 0 = any
};

// A part a target accepts. Nordic: the part number in FICR INFO.PART. STM32: `stm32` is set and
// `part` is 0.
struct ChipPart {
    std::uint32_t part = 0;       // e.g. 0x52840
    std::string_view displayName; // "nRF52840", "STM32L0 cat. 5, 192 KiB" (not translated)
    std::string_view jlinkDevice; // J-Link device name of exactly this part
    std::optional<Stm32Id> stm32 = std::nullopt;
    // STM32 parts (plan/features/0039): the curated part id (`hwio stm32 --chip`, lower case)
    // and a hint naming the chips it covers and known HARDWARIO uses, English source text in
    // context "hwio::device" (QT_TRANSLATE_NOOP, see partHint()).
    std::string_view id;
    const char* hintSourceText = "";
    // Plan 0039 G6 (ADR-0278): firmware HARDWARIO supports for this part; overrides the target's
    // designatedFirmware when set.
    std::optional<firmware::DesignatedFirmware> designatedFirmware = std::nullopt;
};

// What `recover` does after the CTRL-AP erase-all (ADR-0039).
enum class RecoverImage : std::uint8_t {
    None,
    Nrf91DisableApProtect, // program + verify the embedded nrf91_disable_ap_protect.hex
};

enum class EraseScope : std::uint8_t {
    Code, // the code flash (UICR and the PIB kept)
    All,  // flash and UICR (the PIB is lost)
};

// One step of the cabling guide of a target (ADR-0077), shown by the Connect and Flash wizards
// (plan/features/0009, 0010). Texts are English source texts in context "hwio::device"
// (QT_TRANSLATE_NOOP), translated by the GUI; the illustration is a slot id of
// app/gui/assets/wizard (ADR-0076), never an image (libs/device has no QtGui).
struct ConnectionGuideStep {
    std::string_view id;               // kebab-case, unique within the target ("cable")
    std::string_view illustrationSlot; // e.g. "chester-app-cable"
    const char* titleSourceText = "";  // short step title ("Cable")
    const char* bodySourceText = "";   // one or two sentences
};

// Plan 0033 H5 (ADR-0273): the MCU's reset reason register (write 1 to clear, as Nordic's
// POWER.RESETREAS) and the bit its reset pin sets.
struct ResetReasonRegister {
    std::uint32_t address = 0;
    std::uint32_t pinMask = 0;
};

struct TargetProfile {
    std::string_view id; // "app", "lte" (CLI, logs, file names)
    // In user-visible text ("the %1 MCU", "the %1 connector"): English source text, marked with
    // QT_TRANSLATE_NOOP in context "hwio::device" and shown through targetDisplayName().
    std::string_view displayName; // "application", "LTE"
    std::string_view jlinkDevice; // J-Link device name used to connect, e.g. "NRF52840_xxAA"
    std::string_view rttTerminal; // RTT shell buffer name ("" = no RTT console)
    std::string_view rttLogger;   // RTT log buffer name ("" = none)
    bool hashPrefixIsLog;         // shell lines starting with '#' are log lines (older CHESTER fw)
    // J-Link speed in kHz; nullopt = automatic (JLINKARM_SetSpeed(0), as pylink; ADR-0090).
    std::optional<int> defaultSpeedKhz;
    int defaultLatencyMs;
    int minTargetMillivolts; // below this VTref the target counts as unpowered

    // ---- flashing (M2) ----
    std::span<const ChipPart> parts; // accepted parts; the first one is the default
    // Nordic only: FICR and CTRL-AP of this MCU family. STM32 targets leave it empty (checked by
    // the profile invariants test, plan 0039).
    jlink::ChipFamily chipFamily;
    firmware::AddressRange flash; // code flash
    firmware::AddressRange uicr;  // UICR page
    std::uint32_t pibOffset;      // Product Information Block: UICR + pibOffset, 128 B
    bool pibErasableWithUicr;     // a UICR erase also erases the PIB (nRF52: yes)
    RecoverImage recoverImage;
    EraseScope defaultErase; // `erase` without --all (app: Code, lte: All)

    // ---- wizards (M4, ADR-0077) ----
    std::span<const ConnectionGuideStep> connectionGuide; // cabling steps, in order

    // ---- low-level target operations (M6, plan/features/0013, ADR-0099, ADR-0160) ----
    // Half-open ranges. Memory reads may touch any 32-bit range that does not wrap (an unmapped
    // address fails in the backend). Memory writes only inside `ram` or one `peripherals` range
    // and never overlapping a `writeDenied` range (the flash controller: its ERASEALL /
    // ERASEUICR / ERASEPAGE tasks would wipe flash, UICR and the PIB; ADR-0164: the System
    // Control Space and the FPB, through which a write could run code); otherwise
    // target.address_out_of_range (checkMemoryWrite()). RAM stays writable, so a memory write can
    // still make the device run arbitrary code (ADR-0164).
    firmware::AddressRange ram{};
    std::span<const firmware::AddressRange> peripherals;
    std::span<const firmware::AddressRange> writeDenied;
    // The RTT shell prompt (ADR-0104), recognised like the BLE prompt (ADR-0082) and reported as
    // ConsoleSession::promptSeen; empty = unknown (no detection). Separate from
    // BleProfile::shellPrompt: the RTT and NUS shells are different transports.
    std::string_view shellPrompt;
    // ---- plan 0021 (ADR-0165) ----
    // The firmware HARDWARIO supports for this target, downloaded and SHA-256 verified by
    // FirmwareCatalog::downloadDesignated (CHESTER `lte`: the LTE bundle); nullopt = none.
    std::optional<firmware::DesignatedFirmware> designatedFirmware = std::nullopt;
    // ---- plan 0025 (ADR-0186) ----
    // Shell commands whose arguments are secrets (keys, passwords, passkeys): words separated by
    // single spaces, matched case-sensitively against the first words of a command line
    // (matchSensitiveCommand). Such commands are sent but never stored in the command history,
    // and the console file shows them as "<prefix> <redacted>".
    std::span<const std::string_view> sensitiveCommandPrefixes;
    // ---- plan 0028 (U2, ADR-0186 amendment) ----
    // Keys of shell output lines "<key>: <value>" whose value is a secret (CHESTER `info show`:
    // "claim token", "ble passkey"), compared after the `info show` key normalisation (lower case,
    // spaces as '-'). The console file writes "<key>: <redacted>" (matchSensitiveOutput).
    std::span<const std::string_view> sensitiveOutputKeys;
    // ---- plan 0033 H5 (ADR-0273) ----
    // Cleared before a flash or modem update of this target; when the step fails and the reset-pin
    // bit is set afterwards, the failure becomes target.reset_during_update (another MCU of the
    // device reset this one). nullopt = not checked.
    std::optional<ResetReasonRegister> resetReason = std::nullopt;
    // ---- plan 0039 (STM32, ADR-0274, ADR-0275) ----
    // Nordic targets keep the defaults. An STM32 target has exactly one part, no chipFamily, uicr,
    // pibOffset or recoverImage, and `defaultErase` All (JLINK_EraseChip: flash and data EEPROM).
    ChipVendor vendor = ChipVendor::Nordic;
    // The family's identification and flash-interface data (set for STM32 targets).
    std::optional<jlink::Stm32Protection> stm32 = std::nullopt;
    // STM32: the data EEPROM, written from an image only with allowEeprom (nullopt = images with
    // EEPROM records are rejected, e.g. where the J-Link loader's EEPROM bank is unconfirmed).
    std::optional<firmware::AddressRange> dataEeprom = std::nullopt;
};

// A URL for one UI language (ADR-0167): `language` is a BCP 47 primary language subtag ("en",
// "cs").
struct LocalizedUrl {
    std::string_view language;
    std::string_view url;
};

// BLE facts of a device (ADR-0089, plan 0011 B3). Matching rule (ADR-0142, amends ADR-0083): the
// advertised name contains namePattern (case-sensitive); the service UUID is not a match.
struct BleProfile {
    std::string_view namePattern;     // substring of the advertised name; CHESTER: "CHESTER"
    std::string_view serviceUuid;     // NUS 6e400001-b5a3-f393-e0a9-e50e24dcca9e (lowercase)
    std::string_view rxUuid;          // ...0002... (written by us)
    std::string_view txUuid;          // ...0003... (notified by the device)
    std::string_view shellPrompt;     // "bt_nus:~$ " (ADR-0082)
    std::string_view consoleTargetId; // "app": routing rules, history and file names of that target
    // English source text in context "hwio::device" (QT_TRANSLATE_NOOP), see passkeyHint().
    const char* passkeyHintSourceText = "";
    std::span<const ConnectionGuideStep> pairingGuide; // ADR-0077 step type; slots ble-*
};

struct DeviceProfile {
    std::string_view id;          // "chester"
    std::string_view displayName; // product name, not translated ("CHESTER")
    std::span<const TargetProfile> targets;
    std::string_view firmwareCatalogUrl; // ADR-0037: <url>/<id>/hex ("" = no catalog)
    // ADR-0077: how to power the device; English source text in context "hwio::device"
    // (QT_TRANSLATE_NOOP), translated by the GUI ("" = no hint).
    std::string_view powerHintSourceText;
    // M5 (ADR-0089): devices without BLE leave it empty.
    std::optional<BleProfile> ble = std::nullopt;
    // Plan 0021 (ADR-0167): the documentation page listing the application firmware, per UI
    // language; English first (the fallback). Empty = no link. firmwareCatalogUrl stays the
    // download base (ADR-0037).
    std::span<const LocalizedUrl> firmwareDocsUrl;
};

// Plan 0033 (ADR-0233): the target a J-Link console tab of `profile` always uses - the first
// target with an RTT console (CHESTER: app), else the first target; "" for a profile without one.
[[nodiscard]] std::string_view consoleTargetId(const DeviceProfile& profile) noexcept;

// Size of the Product Information Block.
inline constexpr std::uint32_t pibSize = 128;

class ProfileRegistry {
public:
    // nullptr when no profile has this id.
    [[nodiscard]] static const DeviceProfile* find(std::string_view id) noexcept;
    [[nodiscard]] static std::span<const DeviceProfile* const> all() noexcept;
    // The profile a BLE advertisement belongs to, by the name pattern only (ADR-0142); nullptr for
    // none. `serviceUuids` is not used for matching (kept from ADR-0089's signature).
    [[nodiscard]] static const DeviceProfile*
    findByAdvertisement(std::string_view name, std::span<const std::string> serviceUuids) noexcept;
};

// nullptr when the profile has no target with this id.
[[nodiscard]] const TargetProfile* findTarget(const DeviceProfile& profile,
                                              std::string_view id) noexcept;

// ADR-0186: the sensitive command prefixes of a console (profile, target; a BLE console uses its
// BleProfile::consoleTargetId). Empty for unknown ids.
[[nodiscard]] std::span<const std::string_view>
sensitivePrefixesOf(std::string_view profileId, std::string_view targetId) noexcept;

// ADR-0186: the prefix of `prefixes` whose words are the first words of `line` (words separated
// by spaces or tabs, compared case-sensitively), nullopt when none matches.
[[nodiscard]] std::optional<std::string_view>
matchSensitiveCommand(std::string_view line, std::span<const std::string_view> prefixes) noexcept;

// Plan 0028 (U2): the sensitive output keys of a console (as sensitivePrefixesOf).
[[nodiscard]] std::span<const std::string_view>
sensitiveOutputKeysOf(std::string_view profileId, std::string_view targetId) noexcept;

// Plan 0028 (U2): for a line "<key>: <value>" (ANSI codes ignored) whose normalised key is one of
// `keys`: the key as printed (without ANSI codes, trimmed), else nullopt.
[[nodiscard]] std::optional<std::string>
matchSensitiveOutput(std::string_view line, std::span<const std::string_view> keys);

// The part of `target` with this FICR part number, nullptr when it is not one of its parts.
[[nodiscard]] const ChipPart* findPart(const TargetProfile& target, std::uint32_t part) noexcept;

// Plan 0039: the STM32 part of `target` matching a DEV_ID and flash size (flashSizeKib 0 = not
// read: the DEV_ID alone decides when the target has one part with that DEV_ID), nullptr when none.
[[nodiscard]] const ChipPart* findStm32Part(const TargetProfile& target, std::uint16_t devId,
                                            std::uint16_t flashSizeKib) noexcept;

// Plan 0039 (ADR-0275): the curated STM32 parts (stm32_parts.cpp; at most maxStm32Parts), shared
// by the `stm32` profile and CHESTER targets.
inline constexpr std::size_t maxStm32Parts = 32;
[[nodiscard]] std::span<const ChipPart> stm32Parts() noexcept;
// The curated part with this id, nullptr when none.
[[nodiscard]] const ChipPart* findStm32PartById(std::string_view id) noexcept;
// The curated part with this DEV_ID and flash size (0 = unknown: the only part with that DEV_ID,
// else nullptr), nullptr when none.
[[nodiscard]] const ChipPart* findStm32PartByChip(std::uint16_t devId,
                                                  std::uint16_t flashSizeKib) noexcept;
// The translated hint of a part ("" for none).
[[nodiscard]] QString partHint(const ChipPart& part);
// The designated firmware of `target`: its (first) part's entry first, then the target's
// (ADR-0278, plan 0039 G6); nullopt when none.
[[nodiscard]] std::optional<firmware::DesignatedFirmware>
designatedFirmwareOf(const TargetProfile& target) noexcept;

// The translated display name of `target` (ADR-0027), e.g. "application".
[[nodiscard]] QString targetDisplayName(const TargetProfile& target);

// The translated title / body of a connection guide step and the translated power hint of a
// profile (ADR-0077).
[[nodiscard]] QString guideStepTitle(const ConnectionGuideStep& step);
[[nodiscard]] QString guideStepBody(const ConnectionGuideStep& step);
[[nodiscard]] QString powerHint(const DeviceProfile& profile);
// The translated BLE passkey hint of a profile ("" without BLE; ADR-0084).
[[nodiscard]] QString passkeyHint(const DeviceProfile& profile);

// The firmware docs URL of `profile` for `language` ("cs", "cs_CZ", "en-US": the primary subtag is
// compared); the first (English) entry for other languages; "" when the profile has none
// (ADR-0167).
[[nodiscard]] std::string_view firmwareDocsUrlFor(const DeviceProfile& profile,
                                                  std::string_view language) noexcept;

// The PIB address range of `target`.
[[nodiscard]] firmware::AddressRange pibRange(const TargetProfile& target) noexcept;

// ADR-0160: a memory read of [address, address + size) is allowed when it does not run past
// 0xFFFFFFFF. Error: target.address_out_of_range (detail: the requested range).
[[nodiscard]] Status checkMemoryRead(std::uint32_t address, std::size_t size);
// ADR-0160: a memory write of [address, address + size) must lie inside `target.ram` or one of
// `target.peripherals` and must not overlap any of `target.writeDenied`. Error:
// target.address_out_of_range (detail: the requested range and the allowed ranges, or the denied
// range it overlaps).
[[nodiscard]] Status checkMemoryWrite(const TargetProfile& target, std::uint32_t address,
                                      std::size_t size);
// ADR-0035, ADR-0036: a flash write (an image segment, `flash-write`, MCP write_flash) of
// [address, address + size) must not touch the PIB and must lie inside `target.flash` or
// `target.uicr`; bytes in the UICR need `allowUicr`. Errors: firmware.out_of_range (the PIB, or
// outside flash and UICR), firmware.uicr_in_image (UICR bytes without allowUicr). The one copy of
// these rules: planImage() and the CLI / MCP checks before the confirmation use it.
// STM32 targets (plan 0039, ADR-0275): inside `flash`, or inside `dataEeprom` with `allowUicr`
// (which then means "allow data EEPROM"); option bytes and system memory never
// (firmware.out_of_range). Errors as above; EEPROM bytes without consent: firmware.out_of_range
// naming the data EEPROM.
[[nodiscard]] Status checkFlashWrite(const TargetProfile& target, std::uint32_t address,
                                     std::size_t size, bool allowUicr);

} // namespace hwio::device
