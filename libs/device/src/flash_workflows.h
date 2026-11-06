#pragma once

// Flash, erase, recover, reset and memory-read workflows (plan/features/0003-m2-flashing.md, C2;
// ADR-0035, ADR-0036, ADR-0039). Pure logic over jlink::IProgrammer, run on the session worker
// (session_worker.cpp) and unit-tested with a GMock programmer.
//
// Flash:   image checks (ranges, PIB, UICR rule) -> Connecting: identify, family and protection
//          check, reset(Halt) -> program (Erasing / Programming) -> verify (Verifying) ->
//          reset(Go | Halt) (Resetting). With --allow-uicr the UICR page is read, the image's UICR
//          bytes are merged into it and the whole page is programmed with the code (the flash
//          loader rewrites the UICR sector; the PIB bytes come from the device; on targets whose
//          PIB is not erased with the UICR the PIB range is left out).
// Erase:   Connecting (identify) -> Erasing (code flash, or erase-all). The CPU stays halted.
// Recover: Connecting (identify; a protected chip is expected) -> Recovering (CTRL-AP ERASEALL,
//          reconnect) -> [nRF91: program + verify the disable-AP-protect image] -> reset(Go).
// Erase (code only): the PIB is read before and after; a change fails with target.erase_failed
//          (review R5).
// Reset:   Connecting (identify, family check; protected allowed) -> Resetting.
// STM32 targets (plan/features/0039, ADR-0275, ADR-0276): identify reads DBGMCU_IDCODE, F_SIZE
//          and the RDP level; another chip -> target.wrong_chip (the found chip in
//          WorkflowContext::foundChip), RDP 1 -> target.protected, RDP 2 ->
//          target.permanently_protected (always). Flash, erase and unlock read F_SIZE again after
//          the halt when the first (running) read failed (Phase 0 P0.3). Erase is always the
//          mass erase (JLINK_EraseChip: flash and data EEPROM).
// Unlock:  Connecting (identify; RDP 1 expected) -> Unlocking (RDP 0xAA, option reload, reconnect,
//          re-identify: RDP 0; a non-canonical option word is rewritten once at level 0, ADR-0282).
// ReadMemory: Connecting (identify, family check) -> read with the access width (review R7; M6).

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/program_progress.h"
#include "hwio/hw/jlink/programmer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::device::detail {

struct WorkflowContext {
    const DeviceProfile* profile = nullptr;
    const TargetProfile* target = nullptr;
    jlink::IProgrammer* programmer = nullptr;
    firmware::ProgressFn progress; // phase reports
    std::stop_token stop;
    std::function<void(const std::string&)> log; // console file "@" line + hwio.flash
    // Connects the target again with this J-Link device name (LTE: the exact part's name).
    std::function<Status(std::string_view jlinkDevice)> connectAs;
    // The J-Link device name the probe is connected with now.
    std::string connectedDevice;
    // Set by the workflows: the CPU is halted now (reset(Halt), erase); cleared by reset(Go).
    bool cpuHalted = false;
    // Plan 0039: the chip found when an STM32 identify failed with target.wrong_chip (the GUI
    // offers "Use <found>").
    std::optional<jlink::ChipInfo> foundChip;
};

// The image split into the bytes for the code flash and the target's UICR records.
struct ImagePlan {
    firmware::FirmwareImage code;
    std::optional<firmware::FirmwareImage> uicr;
};

// Checks the image against the target (ADR-0035, ADR-0036). Errors: firmware.out_of_range (a byte
// outside flash and UICR, or inside the PIB), firmware.uicr_in_image (UICR bytes without
// allowUicr), usage.invalid_argument (empty image).
[[nodiscard]] Result<ImagePlan> planImage(const DeviceProfile& profile, const TargetProfile& target,
                                          const firmware::FirmwareImage& image, bool allowUicr);

// identify -> family check -> protection check (unless `allowProtected`); on the LTE MCU it
// reconnects with the exact part's J-Link device name. The returned ChipInfo has its name.
// Errors: target.wrong_family, target.protected, those of identify().
[[nodiscard]] Result<jlink::ChipInfo> identifyTarget(WorkflowContext& context, bool allowProtected);

[[nodiscard]] Result<jlink::ChipInfo>
runFlash(WorkflowContext& context, const firmware::FirmwareImage& image, FlashOptions options);
[[nodiscard]] Result<jlink::ChipInfo> runErase(WorkflowContext& context, EraseScope scope);
// `reconnect` connects the target after the erase-all (the CTRL-AP reset drops the connection).
[[nodiscard]] Result<std::optional<jlink::ChipInfo>>
runRecover(WorkflowContext& context, const std::function<Status()>& reconnect);
[[nodiscard]] Status runReset(WorkflowContext& context, ResetMode mode);
// Plan 0039 (ADR-0276, ADR-0282). `reconnect` connects the target again after an option-byte
// reload (it retries until its own deadline). Returns the chip after the unlock. Errors:
// usage.invalid_argument (not an STM32 target), target.permanently_protected,
// target.wrong_chip, target.unlock_failed, those of identify().
[[nodiscard]] Result<jlink::ChipInfo> runUnlock(WorkflowContext& context,
                                                const std::function<Status()>& reconnect);
// Plan 0039: the curated part matching an STM32 chip (by DEV_ID and flash size), nullptr when
// none.
[[nodiscard]] const ChipPart* curatedPartOf(const jlink::ChipInfo& chip) noexcept;
[[nodiscard]] Result<std::vector<std::byte>> runReadMemory(WorkflowContext& context,
                                                           std::uint32_t address, std::size_t size,
                                                           jlink::AccessWidth width);

// The embedded nrf91_disable_ap_protect.hex (ADR-0039), parsed.
[[nodiscard]] Result<firmware::FirmwareImage> recoverImage(RecoverImage image);

// Adds the target to the message (and suggestion) of the M2 target / firmware errors:
// "Programming the lte MCU failed.", "Expected the app MCU (nRF52840) but found nRF9151.", ...
[[nodiscard]] Error forTarget(Error error, const DeviceProfile& profile,
                              const TargetProfile& target);

// Plan 0033 (ADR-0233): the target of `profile` whose chip answered as `chip` (by its FICR part,
// or - FICR unreadable - by the family whose CTRL-AP answered); nullptr when none matches.
[[nodiscard]] const TargetProfile* targetOfChip(const DeviceProfile& profile,
                                                const jlink::ChipInfo& chip) noexcept;

// target.other_connector (ADR-0233): the probe reaches `found` instead of the console target
// `console` ("The J-Link is connected to the LTE MCU."). `detail` names the found target id.
[[nodiscard]] Error otherConnectorError(const TargetProfile& console, const TargetProfile& found,
                                        std::string detail);

} // namespace hwio::device::detail
