#include "hwio/device/profile.h"

#include "hwio/core/error.h"
#include "hwio/core/localized_text.h"
#include "hwio/core/result.h"
#include "hwio/device/line_assembler.h"
#include "hwio/firmware/designated_firmware.h"
#include "hwio/firmware/firmware_image.h"
#include "profiles/profiles.h"

#include <QCoreApplication>
#include <QString>
#include <QStringLiteral>
#include <QtTranslation>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::device {

namespace {

// Registration: one line per device profile (ADR-0032).
constexpr std::array<const DeviceProfile*, 2> registeredProfiles{
    &profiles::chester,
    &profiles::stm32,
};

} // namespace

const DeviceProfile* ProfileRegistry::find(std::string_view id) noexcept {
    // std::array's iterator is a pointer in libstdc++ and libc++ but a class in the MSVC STL
    // (`auto*` does not compile there).
    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto it = std::ranges::find_if(
        registeredProfiles, [id](const DeviceProfile* profile) { return profile->id == id; });
    return it == registeredProfiles.end() ? nullptr : *it;
}

std::span<const DeviceProfile* const> ProfileRegistry::all() noexcept { return registeredProfiles; }

const DeviceProfile* ProfileRegistry::findByAdvertisement(
    std::string_view name, [[maybe_unused]] std::span<const std::string> serviceUuids) noexcept {
    // ADR-0142: by the name pattern only; an advertised NUS UUID is no profile match (Ruuvi tags
    // and other Nordic devices advertise it too).
    for (const DeviceProfile* profile : registeredProfiles) {
        if (!profile->ble) {
            continue;
        }
        const BleProfile& ble = *profile->ble;
        if (!ble.namePattern.empty() && name.find(ble.namePattern) != std::string_view::npos) {
            return profile;
        }
    }
    return nullptr;
}

const DeviceProfile& genericConsoleProfile() noexcept { return profiles::generic; }

const TargetProfile* findTarget(const DeviceProfile& profile, std::string_view id) noexcept {
    const auto it = std::ranges::find(profile.targets, id, &TargetProfile::id);
    return it == profile.targets.end() ? nullptr : &*it;
}

std::span<const std::string_view> sensitivePrefixesOf(std::string_view profileId,
                                                      std::string_view targetId) noexcept {
    const DeviceProfile* profile = ProfileRegistry::find(profileId);
    if (profile == nullptr) {
        return {};
    }
    const TargetProfile* target = findTarget(*profile, targetId);
    return target != nullptr ? target->sensitiveCommandPrefixes
                             : std::span<const std::string_view>{};
}

namespace {

// The next word of `text` from `position` (spaces and tabs separate words); "" at the end.
std::string_view nextWord(std::string_view text, std::size_t& position) noexcept {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t'; };
    while (position < text.size() && isSpace(text[position])) {
        ++position;
    }
    const std::size_t start = position;
    while (position < text.size() && !isSpace(text[position])) {
        ++position;
    }
    return text.substr(start, position - start);
}

bool startsWithWords(std::string_view line, std::string_view prefix) noexcept {
    std::size_t linePosition = 0;
    std::size_t prefixPosition = 0;
    while (true) {
        const std::string_view expected = nextWord(prefix, prefixPosition);
        if (expected.empty()) {
            return prefixPosition > 0;
        }
        if (nextWord(line, linePosition) != expected) {
            return false;
        }
    }
}

} // namespace

std::optional<std::string_view>
matchSensitiveCommand(std::string_view line, std::span<const std::string_view> prefixes) noexcept {
    for (const std::string_view prefix : prefixes) {
        if (startsWithWords(line, prefix)) {
            return prefix;
        }
    }
    return std::nullopt;
}

std::span<const std::string_view> sensitiveOutputKeysOf(std::string_view profileId,
                                                        std::string_view targetId) noexcept {
    const DeviceProfile* profile = ProfileRegistry::find(profileId);
    if (profile == nullptr) {
        return {};
    }
    const TargetProfile* target = findTarget(*profile, targetId);
    return target != nullptr ? target->sensitiveOutputKeys : std::span<const std::string_view>{};
}

std::optional<std::string> matchSensitiveOutput(std::string_view line,
                                                std::span<const std::string_view> keys) {
    if (keys.empty()) {
        return std::nullopt;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string clean = stripAnsiCsi(line.substr(0, colon));
    const std::size_t first = clean.find_first_not_of(" \t");
    const std::size_t last = clean.find_last_not_of(" \t");
    if (first == std::string::npos) {
        return std::nullopt;
    }
    std::string key = clean.substr(first, last - first + 1);
    std::string normalised = key;
    for (char& c : normalised) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        } else if (c == ' ') {
            c = '-';
        }
    }
    for (const std::string_view sensitive : keys) {
        std::string wanted(sensitive);
        std::ranges::replace(wanted, ' ', '-');
        if (normalised == wanted) {
            return key;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view>
matchSensitiveOutputPrefix(std::string_view line, std::span<const std::string_view> prefixes) {
    if (prefixes.empty()) {
        return std::nullopt;
    }
    const std::string clean = stripAnsiCsi(line);
    return matchSensitiveCommand(clean, prefixes);
}

namespace {

// The number of words of `text` (spaces and tabs separate words).
std::size_t wordCount(std::string_view text) noexcept {
    std::size_t position = 0;
    std::size_t count = 0;
    while (!nextWord(text, position).empty()) {
        ++count;
    }
    return count;
}

} // namespace

bool setsSensitiveValue(std::string_view line,
                        std::span<const std::string_view> prefixes) noexcept {
    const auto prefix = matchSensitiveCommand(line, prefixes);
    return prefix && wordCount(line) > wordCount(*prefix);
}

bool refusedFromRemote(const TargetProfile& target, std::string_view line) noexcept {
    return (target.refuseRemoteSensitive &&
            setsSensitiveValue(line, target.sensitiveCommandPrefixes)) ||
           setsSensitiveValue(line, target.remoteRefusedSetters) ||
           matchSensitiveCommand(line, target.remoteRefusedCommands).has_value();
}

const ChipPart* findPart(const TargetProfile& target, std::uint32_t part) noexcept {
    // STM32 parts have part 0 and never match a FICR part number.
    const auto it = std::ranges::find_if(target.parts, [part](const ChipPart& candidate) {
        return !candidate.stm32 && candidate.part == part;
    });
    return it == target.parts.end() ? nullptr : &*it;
}

namespace {

// The part of `parts` with this DEV_ID and flash size; with flashSizeKib 0 the only part with this
// DEV_ID (nullptr when several share it).
const ChipPart* matchStm32(std::span<const ChipPart> parts, std::uint16_t devId,
                           std::uint16_t flashSizeKib) noexcept {
    const ChipPart* found = nullptr;
    for (const ChipPart& part : parts) {
        if (!part.stm32 || part.stm32->devId != devId) {
            continue;
        }
        if (flashSizeKib != 0) {
            if (part.stm32->flashSizeKib == 0 || part.stm32->flashSizeKib == flashSizeKib) {
                return &part;
            }
            continue;
        }
        if (found != nullptr) {
            return nullptr; // ambiguous without the flash size
        }
        found = &part;
    }
    return found;
}

} // namespace

const ChipPart* findStm32Part(const TargetProfile& target, std::uint16_t devId,
                              std::uint16_t flashSizeKib) noexcept {
    return matchStm32(target.parts, devId, flashSizeKib);
}

const ChipPart* findStm32PartById(std::string_view id) noexcept {
    const auto parts = stm32Parts();
    const auto it = std::ranges::find(parts, id, &ChipPart::id);
    return it == parts.end() ? nullptr : &*it;
}

const ChipPart* findStm32PartByChip(std::uint16_t devId, std::uint16_t flashSizeKib) noexcept {
    return matchStm32(stm32Parts(), devId, flashSizeKib);
}

QString partHint(const ChipPart& part) {
    if (std::string_view(part.hintSourceText).empty()) {
        return {};
    }
    return QCoreApplication::translate("hwio::device", part.hintSourceText);
}

std::optional<firmware::DesignatedFirmware>
designatedFirmwareOf(const TargetProfile& target) noexcept {
    if (!target.parts.empty() && target.parts.front().designatedFirmware) {
        return target.parts.front().designatedFirmware;
    }
    return target.designatedFirmware;
}

QString targetDisplayName(const TargetProfile& target) {
    const std::string source(target.displayName.empty() ? target.id : target.displayName);
    return QCoreApplication::translate("hwio::device", source.c_str());
}

LocalizedText targetDisplayNameText(const TargetProfile& target) {
    const std::string source(target.displayName.empty() ? target.id : target.displayName);
    return LocalizedText{.translated = targetDisplayName(target),
                         .english = QString::fromStdString(source)};
}

QString profileDisplayName(const DeviceProfile& profile) {
    const std::string source(profile.displayName);
    return QCoreApplication::translate("hwio::device", source.c_str());
}

QString guideStepTitle(const ConnectionGuideStep& step) {
    return QCoreApplication::translate("hwio::device", step.titleSourceText);
}

QString guideStepBody(const ConnectionGuideStep& step) {
    return QCoreApplication::translate("hwio::device", step.bodySourceText);
}

QString powerHint(const DeviceProfile& profile) {
    if (profile.powerHintSourceText.empty()) {
        return {};
    }
    const std::string source(profile.powerHintSourceText);
    return QCoreApplication::translate("hwio::device", source.c_str());
}

QString passkeyHint(const DeviceProfile& profile) {
    if (!profile.ble || std::string_view(profile.ble->passkeyHintSourceText).empty()) {
        return {};
    }
    return QCoreApplication::translate("hwio::device", profile.ble->passkeyHintSourceText);
}

std::string_view firmwareDocsUrlFor(const DeviceProfile& profile,
                                    std::string_view language) noexcept {
    if (profile.firmwareDocsUrl.empty()) {
        return {};
    }
    const std::string_view primary = language.substr(0, language.find_first_of("_-"));
    const auto it = std::ranges::find(profile.firmwareDocsUrl, primary, &LocalizedUrl::language);
    return it == profile.firmwareDocsUrl.end() ? profile.firmwareDocsUrl.front().url : it->url;
}

firmware::AddressRange pibRange(const TargetProfile& target) noexcept {
    const std::uint64_t start = target.uicr.start + target.pibOffset;
    return firmware::AddressRange{.start = start, .end = start + pibSize};
}

namespace {

// The end of the 32-bit address space (exclusive).
constexpr std::uint64_t addressSpaceEnd = std::uint64_t{1} << 32U;

std::string rangeText(const firmware::AddressRange& range) {
    return std::format("0x{:08X}-0x{:08X}", range.start, range.end);
}

// The start of the Cortex-M Private Peripheral Bus: denied ranges from here on are system or
// debug registers (ADR-0164), below it the flash controller (ADR-0160).
constexpr std::uint64_t privatePeripheralBus = 0xE0000000;

QString hexAddress(std::uint64_t address) {
    return QStringLiteral("0x%1").arg(address, 8, 16, QLatin1Char('0'));
}

} // namespace

Status checkMemoryRead(std::uint32_t address, std::size_t size) {
    const firmware::AddressRange requested{.start = address, .end = std::uint64_t{address} + size};
    if (requested.end <= addressSpaceEnd) {
        return {};
    }
    Error error = makeError(ErrorCode::TargetAddressOutOfRange,
                            std::format("requested {}; the address space ends at 0x{:X}",
                                        rangeText(requested), addressSpaceEnd));
    setText(error, ErrorText::Cause, "hwio::device",
            QT_TRANSLATE_NOOP("hwio::device", "The range runs past the end of the address space."));
    setText(error, ErrorText::Suggestion, "hwio::device",
            QT_TRANSLATE_NOOP("hwio::device", "Use a shorter range or a lower address."));
    return std::unexpected(std::move(error));
}

Status checkMemoryWrite(const TargetProfile& target, std::uint32_t address, std::size_t size) {
    const firmware::AddressRange requested{.start = address, .end = std::uint64_t{address} + size};
    const auto denied =
        std::ranges::find_if(target.writeDenied, [&](const firmware::AddressRange& range) {
            return range.overlaps(requested);
        });
    if (denied != target.writeDenied.end()) {
        Error error = makeError(
            ErrorCode::TargetAddressOutOfRange,
            std::format("requested {}; denied {}", rangeText(requested), rangeText(*denied)));
        setText(error, ErrorText::Cause, "hwio::device",
                denied->start >= privatePeripheralBus
                    ? QT_TRANSLATE_NOOP("hwio::device",
                                        "The range overlaps system or debug registers of the CPU, "
                                        "through which a write could make the device run "
                                        "arbitrary code.")
                    : QT_TRANSLATE_NOOP("hwio::device",
                                        "The range overlaps the flash controller registers, whose "
                                        "tasks erase flash, UICR and the product information."));
        return std::unexpected(std::move(error));
    }
    const auto inside = [&](const firmware::AddressRange& range) {
        return !range.empty() && range.contains(requested);
    };
    if (inside(target.ram) || std::ranges::any_of(target.peripherals, inside)) {
        return {};
    }
    std::string allowed = target.ram.empty() ? std::string() : rangeText(target.ram);
    for (const firmware::AddressRange& range : target.peripherals) {
        if (!range.empty()) {
            allowed += (allowed.empty() ? "" : ", ") + rangeText(range);
        }
    }
    return std::unexpected(
        makeError(ErrorCode::TargetAddressOutOfRange,
                  std::format("requested {}; allowed {}", rangeText(requested), allowed)));
}

namespace {

// Plan 0039 (ADR-0275): flash, or the data EEPROM with consent; nothing else.
Status checkStm32FlashWrite(const TargetProfile& target, const firmware::AddressRange& range,
                            bool allowEeprom) {
    if (target.flash.contains(range)) {
        return {};
    }
    const bool inEeprom = target.dataEeprom && target.dataEeprom->contains(range);
    if (inEeprom && allowEeprom) {
        return {};
    }
    std::string eepromText = "none";
    if (target.dataEeprom) {
        eepromText = rangeText(*target.dataEeprom);
    }
    Error error = makeError(ErrorCode::FirmwareOutOfRange,
                            std::format("segment {}; flash {}, data EEPROM {}", rangeText(range),
                                        rangeText(target.flash), eepromText));
    if (inEeprom) {
        setText(
            error, ErrorText::Message, "hwio::device",
            QT_TRANSLATE_NOOP("hwio::device",
                              "The firmware writes into the data EEPROM of the %1 (address %2)."),
            {targetDisplayNameText(target), hexAddress(range.start)});
        setText(error, ErrorText::Suggestion, "hwio::device",
                QT_TRANSLATE_NOOP("hwio::device",
                                  "Allow writing the data EEPROM if the file is meant to "
                                  "change it."));
        return std::unexpected(std::move(error));
    }
    // The family's data EEPROM area (a part without a usable EEPROM range rejects it by name).
    const bool eepromRecords =
        !target.dataEeprom && target.stm32 && target.stm32->dataEepromArea.contains(range.start);
    const std::uint64_t outside =
        target.flash.contains(range.start) ? target.flash.end : range.start;
    if (eepromRecords) {
        setText(error, ErrorText::Message, "hwio::device",
                QT_TRANSLATE_NOOP("hwio::device",
                                  "Data EEPROM is not supported for the %1 (address %2)."),
                {targetDisplayNameText(target), hexAddress(range.start)});
    } else {
        setText(error, ErrorText::Message, "hwio::device",
                QT_TRANSLATE_NOOP("hwio::device", "The firmware does not fit the %1 (address %2)."),
                {targetDisplayNameText(target), hexAddress(outside)});
    }
    setText(error, ErrorText::Suggestion, "hwio::device",
            QT_TRANSLATE_NOOP("hwio::device", "Check that the firmware is for the %1."),
            {targetDisplayNameText(target)});
    return std::unexpected(std::move(error));
}

} // namespace

Status checkFlashWrite(const TargetProfile& target, std::uint32_t address, std::size_t size,
                       bool allowUicr) {
    const firmware::AddressRange range{.start = address, .end = std::uint64_t{address} + size};
    if (target.vendor == ChipVendor::Stm32) {
        return checkStm32FlashWrite(target, range, allowUicr);
    }
    const firmware::AddressRange pib = pibRange(target);
    if (range.overlaps(pib)) {
        Error error = makeError(ErrorCode::FirmwareOutOfRange,
                                std::format("segment 0x{:08X}-0x{:08X} overlaps the PIB "
                                            "0x{:08X}-0x{:08X}",
                                            range.start, range.end, pib.start, pib.end));
        setText(error, ErrorText::Message, "hwio::device",
                QT_TRANSLATE_NOOP(
                    "hwio::device",
                    "The firmware writes into the Product Information Block (address %1)."),
                {hexAddress(std::max(range.start, pib.start))});
        return std::unexpected(std::move(error));
    }
    if (!target.flash.contains(range) && !target.uicr.contains(range)) {
        const std::uint64_t outside =
            target.flash.contains(range.start) ? target.flash.end : range.start;
        Error error = makeError(ErrorCode::FirmwareOutOfRange,
                                std::format("segment 0x{:08X}-0x{:08X}; flash 0x{:08X}-0x{:08X}, "
                                            "UICR 0x{:08X}-0x{:08X}",
                                            range.start, range.end, target.flash.start,
                                            target.flash.end, target.uicr.start, target.uicr.end));
        setText(
            error, ErrorText::Message, "hwio::device",
            QT_TRANSLATE_NOOP("hwio::device", "The firmware does not fit the %1 MCU (address %2)."),
            {targetDisplayNameText(target), hexAddress(outside)});
        setText(error, ErrorText::Suggestion, "hwio::device",
                QT_TRANSLATE_NOOP("hwio::device", "Check that the firmware is for the %1 MCU."),
                {targetDisplayNameText(target)});
        return std::unexpected(std::move(error));
    }
    if (!allowUicr && range.overlaps(target.uicr)) {
        return std::unexpected(
            makeError(ErrorCode::FirmwareUicrInImage,
                      std::format("UICR 0x{:08X}-0x{:08X}", target.uicr.start, target.uicr.end)));
    }
    return {};
}

bool hasConsole(const DeviceProfile& profile) noexcept {
    return std::ranges::any_of(
        profile.targets, [](const TargetProfile& target) { return !target.rttTerminal.empty(); });
}

std::string_view tabTargetId(const DeviceProfile& profile, std::string_view requested) noexcept {
    if (!hasConsole(profile) && findTarget(profile, requested) != nullptr) {
        return findTarget(profile, requested)->id;
    }
    return consoleTargetId(profile);
}

std::string_view consoleTargetId(const DeviceProfile& profile) noexcept {
    for (const TargetProfile& target : profile.targets) {
        if (!target.rttTerminal.empty()) {
            return target.id;
        }
    }
    return profile.targets.empty() ? std::string_view() : profile.targets.front().id;
}

} // namespace hwio::device
