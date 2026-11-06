#include "flash_workflows.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/intel_hex.h"
#include "hwio/firmware/program_progress.h"
#include "hwio/hw/jlink/programmer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringLiteral>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::device::detail {

namespace {

using firmware::FirmwareImage;
using firmware::ProgramPhase;
using firmware::Segment;

QString partName(const DeviceProfile& profile, std::uint32_t part) {
    for (const TargetProfile& target : profile.targets) {
        if (const ChipPart* known = findPart(target, part)) {
            return toQString(known->displayName);
        }
    }
    return QCoreApplication::translate("hwio::device", "an unknown MCU (part %1)")
        .arg(QStringLiteral("0x%1").arg(part, 0, 16));
}

void report(const WorkflowContext& context, ProgramPhase phase) {
    if (context.progress) {
        context.progress({.phase = phase, .done = 0, .total = 0});
    }
}

void log(const WorkflowContext& context, const std::string& text) {
    if (context.log) {
        context.log(text);
    }
}

// ADR-0186 (review M-4c): the PIB bytes that hold secrets (claim token at 0x45, BLE passkey up
// to 0x76) are never written to the app log, the console file or Error.detail; `pib read` shows
// them in full.
constexpr std::size_t pibSecretFirst = 0x45;
constexpr std::size_t pibSecretLast = 0x76;

// The PIB as hex for logs: the secret bytes as "..".
std::string pibLogHex(std::span<const std::byte> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i >= pibSecretFirst && i <= pibSecretLast) {
            text += "..";
            continue;
        }
        text += digits[std::to_integer<unsigned>(bytes[i]) >> 4U];
        text += digits[std::to_integer<unsigned>(bytes[i]) & 0x0FU];
    }
    return text;
}

// The UICR page with the image's UICR bytes merged in (ADR-0036).
Result<FirmwareImage> mergedUicr(WorkflowContext& context, const FirmwareImage& uicr) {
    const TargetProfile& target = *context.target;
    auto device = context.programmer->readMemory(static_cast<std::uint32_t>(target.uicr.start),
                                                 static_cast<std::size_t>(target.uicr.size()),
                                                 jlink::AccessWidth::Bits8);
    if (!device) {
        return std::unexpected(std::move(device).error());
    }
    std::vector<std::byte> page = std::move(*device);
    for (const Segment& segment : uicr.segments()) {
        const std::size_t offset = segment.address - target.uicr.start;
        std::ranges::copy(segment.data, page.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    std::vector<Segment> segments;
    segments.push_back(
        Segment{.address = static_cast<std::uint32_t>(target.uicr.start), .data = std::move(page)});
    auto merged = FirmwareImage::fromSegments(std::move(segments));
    if (!merged) {
        return merged;
    }
    if (!target.pibErasableWithUicr) {
        // The PIB is not erased with the UICR (nRF91 OTP area): it is never written.
        if (auto pib = merged->splitOut(pibRange(target)); !pib) {
            return std::unexpected(std::move(pib).error());
        }
    }
    return merged;
}

Result<FirmwareImage> combine(const FirmwareImage& code, const std::optional<FirmwareImage>& uicr) {
    std::vector<Segment> segments = code.segments();
    if (uicr) {
        segments.insert(segments.end(), uicr->segments().begin(), uicr->segments().end());
    }
    return FirmwareImage::fromSegments(std::move(segments), code.startAddress());
}

} // namespace

Error forTarget(Error error, const DeviceProfile& profile, const TargetProfile& target) {
    const QString name = targetDisplayName(target); // user-visible (H4)
    switch (error.code) {
    case ErrorCode::TargetProgramFailed:
        error.message =
            QCoreApplication::translate("hwio::device", "Programming the %1 MCU failed.")
                .arg(name)
                .toStdString();
        break;
    case ErrorCode::TargetEraseFailed:
        error.message = QCoreApplication::translate("hwio::device", "Erasing the %1 MCU failed.")
                            .arg(name)
                            .toStdString();
        break;
    case ErrorCode::TargetRecoverFailed:
        error.message = QCoreApplication::translate("hwio::device", "Recovering the %1 MCU failed.")
                            .arg(name)
                            .toStdString();
        break;
    case ErrorCode::TargetProtected:
        if (target.vendor == ChipVendor::Stm32) {
            // Plan 0039 (ADR-0276): the GUI offers Unlock…; the CLI names the command.
            error.message = QCoreApplication::translate(
                                "hwio::device",
                                "The chip is read-out protected (RDP level 1). Unlocking erases "
                                "the whole chip, including its data EEPROM.")
                                .toStdString();
            error.cause = QCoreApplication::translate(
                              "hwio::device",
                              "Its firmware or a programming tool set the read-out protection.")
                              .toStdString();
            error.suggestion =
                (profile.id == "stm32"
                     ? QCoreApplication::translate("hwio::device",
                                                   "Unlock the chip (this erases it): `hwio "
                                                   "stm32 unlock --chip %1`.")
                           .arg(toQString(target.id))
                     : QCoreApplication::translate(
                           "hwio::device", "Unlock the chip (this erases it): `hwio %1 %2 unlock`.")
                           .arg(toQString(profile.id), toQString(target.id)))
                    .toStdString();
            break;
        }
        error.message =
            QCoreApplication::translate("hwio::device", "The %1 MCU is read-back protected.")
                .arg(name)
                .toStdString();
        error.suggestion = QCoreApplication::translate("hwio::device",
                                                       "Run `hwio %1 %2 recover` (erases the MCU).")
                               .arg(toQString(profile.id), toQString(target.id))
                               .toStdString();
        break;
    default:
        break;
    }
    return error;
}

Result<ImagePlan> planImage(const DeviceProfile& profile, const TargetProfile& target,
                            const FirmwareImage& image, bool allowUicr) {
    (void)profile;
    if (image.empty()) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, "the image has no data");
        error.message = QCoreApplication::translate("hwio::device", "The firmware image is empty.")
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    // The rules of checkFlashWrite(), per segment: first the ranges of all segments (PIB, flash /
    // UICR), then the UICR consent, so an image with both kinds of fault reports out_of_range.
    const auto checkSegments = [&](bool uicrAllowed) -> Status {
        for (const Segment& segment : image.segments()) {
            if (Status allowed =
                    checkFlashWrite(target, segment.address, segment.data.size(), uicrAllowed);
                !allowed) {
                return allowed;
            }
        }
        return {};
    };
    if (Status ranges = checkSegments(true); !ranges) {
        return std::unexpected(std::move(ranges).error());
    }
    if (!allowUicr) {
        if (Status uicr = checkSegments(false); !uicr) {
            return std::unexpected(std::move(uicr).error());
        }
    }
    ImagePlan plan{.code = image, .uicr = std::nullopt};
    if (image.intersects(target.uicr)) {
        auto uicr = plan.code.splitOut(target.uicr);
        if (!uicr) {
            return std::unexpected(std::move(uicr).error());
        }
        plan.uicr = std::move(*uicr);
    }
    return plan;
}

namespace {

Error wrongFamily(const TargetProfile& target, const QString& found, std::string detail) {
    const QString expected =
        toQString(target.parts.empty() ? std::string_view() : target.parts.front().displayName);
    Error error = makeError(ErrorCode::TargetWrongFamily, std::move(detail));
    error.message =
        QCoreApplication::translate("hwio::device", "Expected the %1 MCU (%2) but found %3.")
            .arg(targetDisplayName(target), expected, found)
            .toStdString();
    error.suggestion =
        QCoreApplication::translate("hwio::device", "Connect the J-Link to the %1 connector.")
            .arg(targetDisplayName(target))
            .toStdString();
    return error;
}

// FICR could not be read; the family comes from the CTRL-AP IDR (review R2).
Result<jlink::ChipInfo> unreadableChip(WorkflowContext& context, jlink::ChipInfo chip,
                                       bool allowProtected) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    if (chip.family != target.chipFamily.name) {
        return std::unexpected(wrongFamily(
            target,
            QCoreApplication::translate("hwio::device", "an %1 MCU").arg(toQString(chip.family)),
            std::format("Nordic CTRL-AP at the {} AP index", chip.family)));
    }
    if (chip.protectedChip) {
        log(context, std::format("{} MCU: APPROTECT enabled", target.id));
        if (allowProtected) {
            return chip;
        }
        return std::unexpected(forTarget(makeError(ErrorCode::TargetProtected), profile, target));
    }
    if (allowProtected) {
        log(context, std::format("{} MCU: FICR unreadable, its CTRL-AP answers", target.id));
        return chip; // recover: the expected CTRL-AP is there
    }
    Error error =
        makeError(ErrorCode::TargetConnectFailed, "FICR unreadable, APPROTECT not enabled");
    error.message =
        QCoreApplication::translate("hwio::device", "The type of the %1 MCU could not be read.")
            .arg(targetDisplayName(target))
            .toStdString();
    return std::unexpected(std::move(error));
}

} // namespace

const ChipPart* curatedPartOf(const jlink::ChipInfo& chip) noexcept {
    return chip.devId == 0 ? nullptr : findStm32PartByChip(chip.devId, chip.flashSizeKib);
}

const TargetProfile* targetOfChip(const DeviceProfile& profile,
                                  const jlink::ChipInfo& chip) noexcept {
    for (const TargetProfile& target : profile.targets) {
        if (target.vendor == ChipVendor::Stm32 || chip.devId != 0) {
            if (target.vendor == ChipVendor::Stm32 && chip.devId != 0 &&
                findStm32Part(target, chip.devId, chip.flashSizeKib) != nullptr) {
                return &target;
            }
            continue;
        }
        const bool matches = chip.part != 0
                                 ? findPart(target, chip.part) != nullptr
                                 : !chip.family.empty() && chip.family == target.chipFamily.name;
        if (matches) {
            return &target;
        }
    }
    return nullptr;
}

Error otherConnectorError(const TargetProfile& console, const TargetProfile& found,
                          std::string detail) {
    Error error = makeError(ErrorCode::TargetOtherConnector, std::move(detail));
    error.message =
        QCoreApplication::translate("hwio::device", "The J-Link is connected to the %1 MCU.")
            .arg(targetDisplayName(found))
            .toStdString();
    error.cause = QCoreApplication::translate(
                      "hwio::device",
                      "The console runs on the %1 MCU; the J-Link cable is on the other connector.")
                      .arg(targetDisplayName(console))
                      .toStdString();
    // The connector by its board label (the target id in upper case: "APP", "LTE").
    error.suggestion = QCoreApplication::translate(
                           "hwio::device",
                           "Move the J-Link cable to the %1 connector, or update the LTE firmware.")
                           .arg(toQString(console.id).toUpper())
                           .toStdString();
    return error;
}

namespace {

// Plan 0039 (ADR-0275): the chip is not the target's part.
Error wrongChip(const TargetProfile& target, const jlink::ChipInfo& chip) {
    const ChipPart* found = curatedPartOf(chip);
    const QString idText = QStringLiteral("0x%1").arg(chip.devId, 3, 16, QLatin1Char('0'));
    const std::string foundSource = found != nullptr ? std::string(found->displayName) : "";
    const QString foundName = found != nullptr
                                  ? QCoreApplication::translate("hwio::device", foundSource.c_str())
                                  : QCoreApplication::translate("hwio::device", "an unknown chip");
    Error error = makeError(
        ErrorCode::TargetWrongChip,
        std::format("DEV_ID 0x{:03X} REV_ID 0x{:04X}, flash {} KiB; expected {}{}", chip.devId,
                    chip.revId, chip.flashSizeKib,
                    target.parts.empty() ? std::string_view() : target.parts.front().id,
                    found != nullptr ? std::format("; found {}", found->id) : std::string()));
    error.message = QCoreApplication::translate(
                        "hwio::device", "The connected chip is %1 (ID %2), not the selected %3.")
                        .arg(foundName, idText, targetDisplayName(target))
                        .toStdString();
    error.suggestion =
        found != nullptr
            ? QCoreApplication::translate("hwio::device",
                                          "Check that the cable is on the right connector, or "
                                          "choose %1 as the chip.")
                  .arg(foundName)
                  .toStdString()
            : QCoreApplication::translate("hwio::device",
                                          "Check that the cable is on the right connector.")
                  .toStdString();
    return error;
}

// Plan 0039: identify of an STM32 target. RDP 2 always fails; RDP 1 unless `allowProtected`.
Result<jlink::ChipInfo> identifyStm32(WorkflowContext& context, jlink::ChipInfo chip,
                                      bool allowProtected) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    const ChipPart* part = findStm32Part(target, chip.devId, chip.flashSizeKib);
    if (part == nullptr) {
        context.foundChip = chip;
        return std::unexpected(wrongChip(target, chip));
    }
    chip.name = std::string(part->displayName);
    log(context,
        std::format("Target: {} ({}), DEV_ID 0x{:03X} REV_ID 0x{:04X}, {} KiB, RDP {}", chip.name,
                    target.id, chip.devId, chip.revId, chip.flashSizeKib, chip.rdpLevel));
    if (chip.rdpLevel >= 2) {
        return std::unexpected(makeError(ErrorCode::TargetPermanentlyProtected,
                                         std::format("{}: RDP level 2", target.id)));
    }
    if (chip.rdpLevel == 1 && !allowProtected) {
        return std::unexpected(forTarget(
            makeError(ErrorCode::TargetProtected, std::format("{}: RDP level 1", target.id)),
            profile, target));
    }
    return chip;
}

// Plan 0039 (Phase 0 P0.3): after the halt of flash / erase / unlock, F_SIZE is read again when
// the first (running) identify could not read it; a wrong size is target.wrong_chip.
Status confirmStm32Size(WorkflowContext& context, const jlink::ChipInfo& chip) {
    if (context.target->vendor != ChipVendor::Stm32 || chip.flashSizeKib != 0 ||
        chip.rdpLevel != 0) {
        return {};
    }
    auto halted = context.programmer->identify();
    if (!halted) {
        return std::unexpected(
            forTarget(std::move(halted).error(), *context.profile, *context.target));
    }
    if (halted->flashSizeKib == 0) {
        log(context, "Flash size still not readable; the DEV_ID decides");
        return {};
    }
    if (findStm32Part(*context.target, halted->devId, halted->flashSizeKib) == nullptr) {
        context.foundChip = *halted;
        return std::unexpected(wrongChip(*context.target, *halted));
    }
    return {};
}

} // namespace

Result<jlink::ChipInfo> identifyTarget(WorkflowContext& context, bool allowProtected) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    report(context, ProgramPhase::Connecting);
    auto chip = context.programmer->identify();
    if (!chip) {
        return std::unexpected(forTarget(std::move(chip).error(), profile, target));
    }
    if (target.vendor == ChipVendor::Stm32) {
        return identifyStm32(context, std::move(*chip), allowProtected);
    }
    if (chip->part == 0) {
        return unreadableChip(context, std::move(*chip), allowProtected);
    }
    const ChipPart* part = findPart(target, chip->part);
    if (part == nullptr) {
        return std::unexpected(wrongFamily(target, partName(profile, chip->part),
                                           std::format("FICR part 0x{:X}", chip->part)));
    }
    chip->name = std::string(part->displayName);
    // LTE: connected with the first part's J-Link name; use the exact part's name when it differs
    // (to confirm in Phase 0).
    if (context.connectAs && !context.connectedDevice.empty() &&
        part->jlinkDevice != context.connectedDevice) {
        log(context, std::format("Reconnecting as {}", part->jlinkDevice));
        if (Status reconnected = context.connectAs(part->jlinkDevice); !reconnected) {
            return std::unexpected(std::move(reconnected).error());
        }
        context.connectedDevice = std::string(part->jlinkDevice);
    }
    log(context, std::format("Target: {} ({})", chip->name, target.id));
    return chip;
}

Result<jlink::ChipInfo> runFlash(WorkflowContext& context, const FirmwareImage& image,
                                 FlashOptions options) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    auto plan = planImage(profile, target, image, options.allowUicr);
    if (!plan) {
        return std::unexpected(std::move(plan).error());
    }
    auto chip = identifyTarget(context, false);
    if (!chip) {
        return chip;
    }
    if (Status halted = context.programmer->reset(jlink::ResetMode::Halt); !halted) {
        return std::unexpected(forTarget(std::move(halted).error(), profile, target));
    }
    context.cpuHalted = true;
    if (Status size = confirmStm32Size(context, *chip); !size) {
        return std::unexpected(std::move(size).error());
    }
    if (options.eraseAll && target.vendor == ChipVendor::Stm32) {
        report(context, ProgramPhase::Erasing);
        log(context, "Erasing the whole chip (flash and data EEPROM)");
        if (Status erased = context.programmer->eraseAll(); !erased) {
            return std::unexpected(forTarget(std::move(erased).error(), profile, target));
        }
    }
    std::optional<FirmwareImage> uicr;
    if (plan->uicr) {
        auto merged = mergedUicr(context, *plan->uicr);
        if (!merged) {
            return std::unexpected(std::move(merged).error());
        }
        uicr = std::move(*merged);
        log(context, "UICR records merged (the Product Information Block is kept)");
    }
    auto written = combine(plan->code, uicr);
    if (!written) {
        return std::unexpected(std::move(written).error());
    }
    log(context,
        std::format("Programming {} bytes (SHA-256 {})", image.totalBytes(), image.sha256()));
    if (Status programmed = context.programmer->program(*written, context.progress, context.stop);
        !programmed) {
        return std::unexpected(forTarget(std::move(programmed).error(), profile, target));
    }
    log(context, "Verifying");
    if (Status verified = context.programmer->verify(*written, context.progress, context.stop);
        !verified) {
        return std::unexpected(forTarget(std::move(verified).error(), profile, target));
    }
    report(context, ProgramPhase::Resetting);
    const jlink::ResetMode mode = options.halt ? jlink::ResetMode::Halt : jlink::ResetMode::Go;
    log(context, options.halt ? "Resetting (halt)" : "Resetting (go)");
    if (Status reset = context.programmer->reset(mode); !reset) {
        return std::unexpected(forTarget(std::move(reset).error(), profile, target));
    }
    context.cpuHalted = options.halt;
    log(context, "Flashing completed");
    return chip;
}

Result<jlink::ChipInfo> runErase(WorkflowContext& context, EraseScope scope) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    auto chip = identifyTarget(context, false);
    if (!chip) {
        return chip;
    }
    report(context, ProgramPhase::Erasing);
    context.cpuHalted = true; // the erase halts the CPU; nothing runs afterwards
    Status erased;
    if (target.vendor == ChipVendor::Stm32) {
        // Plan 0039 (Phase 0 P0.5): the STM32 erase is JLINK_EraseChip, which erases flash and
        // data EEPROM (the nRF code-only erase writes 0xFF, which is not the STM32 erased state).
        if (Status halted = context.programmer->reset(jlink::ResetMode::Halt); !halted) {
            return std::unexpected(forTarget(std::move(halted).error(), profile, target));
        }
        if (Status size = confirmStm32Size(context, *chip); !size) {
            return std::unexpected(std::move(size).error());
        }
        log(context, "Erasing the whole chip (flash and data EEPROM)");
        erased = context.programmer->eraseAll();
    } else if (scope == EraseScope::Code) {
        // HIL H7 (2026-11-03): on a running CHESTER the loader's erase failed at 0x0 ("Verification
        // failed", "RAMCode did not respond in time"); reset and halt first, as runFlash() does and
        // as J-Link Commander's loadbin does implicitly. eraseAll() resets itself.
        if (Status halted = context.programmer->reset(jlink::ResetMode::Halt); !halted) {
            return std::unexpected(forTarget(std::move(halted).error(), profile, target));
        }
        // Review R5: the PIB must survive a code-only erase. Phase 0.3 confirms the loader keeps
        // UICR; until then (and afterwards, as a guard) the PIB is compared before and after.
        const firmware::AddressRange pib = pibRange(target);
        const auto pibAddress = static_cast<std::uint32_t>(pib.start);
        auto before =
            context.programmer->readMemory(pibAddress, pibSize, jlink::AccessWidth::Bits8);
        if (!before) {
            return std::unexpected(forTarget(std::move(before).error(), profile, target));
        }
        log(context, "Erasing the code flash (UICR and the Product Information Block are kept)");
        erased = context.programmer->eraseCode(target.flash, context.progress, context.stop);
        if (erased) {
            auto after =
                context.programmer->readMemory(pibAddress, pibSize, jlink::AccessWidth::Bits8);
            if (!after) {
                // Review R17: say that the PIB state is unknown; keep its old bytes in the log.
                log(context, "ERROR: the Product Information Block could not be read after the "
                             "code-only erase; it was " +
                                 pibLogHex(*before));
                Error error =
                    makeError(ErrorCode::TargetEraseFailed,
                              std::format("PIB at 0x{:08X} not readable after the erase: "
                                          "{}; before: {}",
                                          pib.start, after.error().detail, pibLogHex(*before)));
                error.message =
                    QCoreApplication::translate("hwio::device",
                                                "The Product Information Block of the %1 MCU could "
                                                "not be checked after the erase.")
                        .arg(targetDisplayName(target))
                        .toStdString();
                // Review R19: a second erase would read the possibly wiped PIB as "before".
                error.suggestion =
                    QCoreApplication::translate(
                        "hwio::device",
                        "Do not erase again. Run `hwio %1 %2 pib read` and compare it with the "
                        "bytes "
                        "in the log (see Details); if they differ, contact HARDWARIO support.")
                        .arg(toQString(profile.id), toQString(target.id))
                        .toStdString();
                return std::unexpected(std::move(error));
            }
            if (*after != *before) {
                log(context, "ERROR: the code-only erase changed the Product Information Block");
                Error error = makeError(ErrorCode::TargetEraseFailed,
                                        std::format("PIB at 0x{:08X} changed by the code-only "
                                                    "erase",
                                                    pib.start));
                error.message = QCoreApplication::translate(
                                    "hwio::device", "Erasing the code flash of the %1 MCU also "
                                                    "changed its Product Information Block.")
                                    .arg(targetDisplayName(target))
                                    .toStdString();
                error.suggestion =
                    QCoreApplication::translate(
                        "hwio::device",
                        "Do not erase again; report this with the log file to HARDWARIO support.")
                        .toStdString();
                return std::unexpected(std::move(error));
            }
        }
    } else {
        log(context, "Erasing all flash and UICR");
        erased = context.programmer->eraseAll();
    }
    if (!erased) {
        return std::unexpected(forTarget(std::move(erased).error(), profile, target));
    }
    log(context, "Erase completed");
    return chip;
}

Result<std::optional<jlink::ChipInfo>> runRecover(WorkflowContext& context,
                                                  const std::function<Status()>& reconnect) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    if (target.vendor == ChipVendor::Stm32) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, "recover on an STM32 target");
        error.message = QCoreApplication::translate("hwio::device",
                                                    "An STM32 chip is not recovered but unlocked.")
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    // A protected chip is what recover is for. Only the expected family's CTRL-AP is recovered
    // (its FICR or its CTRL-AP IDR answered; review R2).
    auto before = identifyTarget(context, true);
    if (!before) {
        return std::unexpected(std::move(before).error());
    }
    report(context, ProgramPhase::Recovering);
    log(context, "Recovering (CTRL-AP ERASEALL)");
    context.cpuHalted = true;
    if (Status recovered = context.programmer->recover(context.stop); !recovered) {
        return std::unexpected(forTarget(std::move(recovered).error(), profile, target));
    }
    if (reconnect) {
        if (Status connected = reconnect(); !connected) {
            Error error = makeError(ErrorCode::TargetRecoverFailed, connected.error().detail);
            return std::unexpected(forTarget(std::move(error), profile, target));
        }
    }
    std::optional<jlink::ChipInfo> chip;
    if (auto after = context.programmer->identify(); after && !after->protectedChip) {
        if (const ChipPart* part = findPart(target, after->part)) {
            after->name = std::string(part->displayName);
        }
        chip = std::move(*after);
    }
    if (target.recoverImage == RecoverImage::Nrf91DisableApProtect) {
        auto image = recoverImage(target.recoverImage);
        if (!image) {
            return std::unexpected(std::move(image).error());
        }
        log(context, "Programming the debug-port unlock image");
        if (Status programmed = context.programmer->program(*image, context.progress, {});
            !programmed) {
            return std::unexpected(forTarget(std::move(programmed).error(), profile, target));
        }
        if (Status verified = context.programmer->verify(*image, context.progress, {}); !verified) {
            return std::unexpected(forTarget(std::move(verified).error(), profile, target));
        }
    }
    report(context, ProgramPhase::Resetting);
    if (Status reset = context.programmer->reset(jlink::ResetMode::Go); !reset) {
        return std::unexpected(forTarget(std::move(reset).error(), profile, target));
    }
    context.cpuHalted = false;
    log(context, "Recover completed");
    return chip;
}

namespace {

// ADR-0282: the stored RDP word has its complement in bits 31:16.
bool canonicalOptionWord(std::uint32_t word) noexcept { return (word >> 16U) == (~word & 0xFFFFU); }

} // namespace

Result<jlink::ChipInfo> runUnlock(WorkflowContext& context,
                                  const std::function<Status()>& reconnect) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    if (target.vendor != ChipVendor::Stm32) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "unlock needs an STM32 target"));
    }
    auto before = identifyTarget(context, true); // RDP 2 and another chip stop here
    if (!before) {
        return before;
    }
    const auto unlockFailed = [&](std::string detail) {
        return std::unexpected(forTarget(
            makeError(ErrorCode::TargetUnlockFailed, std::move(detail)), profile, target));
    };
    // One option-word write + reload + reconnect + identify.
    const auto writeLevel0 = [&](std::string_view why) -> Result<jlink::ChipInfo> {
        log(context, std::format("Unlock: writing RDP level 0 ({})", why));
        context.cpuHalted = true;
        if (Status written = context.programmer->programRdpLevel0(context.stop); !written) {
            return std::unexpected(forTarget(std::move(written).error(), profile, target));
        }
        if (reconnect) {
            if (Status connected = reconnect(); !connected) {
                return unlockFailed("no connection after the option-byte reload: " +
                                    connected.error().detail);
            }
        }
        auto after = context.programmer->identify();
        if (!after) {
            return unlockFailed("identify after the option-byte reload: " + after.error().detail);
        }
        if (after->rdpLevel != 0 || after->optionError) {
            return unlockFailed(std::format("RDP level {} after the reload{}", after->rdpLevel,
                                            after->optionError ? ", OPTVERR set" : ""));
        }
        return after;
    };
    report(context, ProgramPhase::Unlocking);
    if (before->rdpLevel == 0 && canonicalOptionWord(before->optionWord)) {
        log(context, "Unlock: the chip is not read-out protected; nothing to do");
        return before;
    }
    auto after = writeLevel0(before->rdpLevel == 0 ? "non-canonical option word"
                                                   : "RDP level 1, the chip mass-erases");
    if (!after) {
        return after;
    }
    if (!canonicalOptionWord(after->optionWord)) {
        // ADR-0282: the regression left a non-canonical word (Phase 0: 0x015500AA); rewritten
        // once at level 0, which erases nothing.
        log(context,
            std::format("Unlock: option word 0x{:08X} is not canonical", after->optionWord));
        after = writeLevel0("canonical option word");
        if (!after) {
            return after;
        }
        if (!canonicalOptionWord(after->optionWord)) {
            log(context, std::format("WARNING: option word still 0x{:08X} (RDP level 0, usable)",
                                     after->optionWord));
        }
    }
    after->name = before->name;
    log(context, "Unlock completed: RDP level 0");
    return after;
}

Status runReset(WorkflowContext& context, ResetMode mode) {
    // Review R7: the family check first, so the wrong connector never resets the other MCU.
    if (auto chip = identifyTarget(context, true); !chip) {
        return std::unexpected(std::move(chip).error());
    }
    report(context, ProgramPhase::Resetting);
    log(context, mode == ResetMode::Halt ? "Reset (halt)" : "Reset (go)");
    if (Status reset = context.programmer->reset(mode); !reset) {
        return std::unexpected(
            forTarget(std::move(reset).error(), *context.profile, *context.target));
    }
    context.cpuHalted = mode == ResetMode::Halt;
    return {};
}

Result<std::vector<std::byte>> runReadMemory(WorkflowContext& context, std::uint32_t address,
                                             std::size_t size, jlink::AccessWidth width) {
    // ADR-0160: any range that does not wrap (checked again; DeviceSession checks it first).
    if (Status inside = checkMemoryRead(address, size); !inside) {
        return std::unexpected(std::move(inside).error());
    }
    // Review R7: the family check first (pib read / uicr read on the wrong connector).
    if (auto chip = identifyTarget(context, false); !chip) {
        return std::unexpected(std::move(chip).error());
    }
    return context.programmer->readMemory(address, size, width);
}

Result<FirmwareImage> recoverImage(RecoverImage image) {
    if (image != RecoverImage::Nrf91DisableApProtect) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no recover image"));
    }
    QFile file(QStringLiteral(":/hwio/device/nrf91_disable_ap_protect.hex"));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected,
                                         "embedded nrf91_disable_ap_protect.hex missing"));
    }
    const QByteArray text = file.readAll();
    return firmware::parseIntelHex(
        std::as_bytes(std::span(text.constData(), static_cast<std::size_t>(text.size()))),
        "nrf91_disable_ap_protect.hex");
}

} // namespace hwio::device::detail
