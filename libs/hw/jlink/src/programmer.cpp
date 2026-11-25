#include "hwio/hw/jlink/programmer.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/localized_text.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/program_progress.h"
#include "hwio/hw/jlink/clock.h"
#include "hwio/hw/jlink/jlink_api.h"
#include "hwio/hw/jlink/jlink_probe.h"
#include "hwio/hw/jlink/types.h"

#include <QChar>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringLiteral>
#include <QtLogging>
#include <QtTranslation>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::jlink {

namespace {

// The library log lines of a download that parseDownloadMessages() reads (plan 0050).
constexpr std::string_view downloadMessageMarker = "Flash download:";

using firmware::FirmwareImage;
using firmware::ProgramPhase;
using firmware::ProgramProgress;
using firmware::ProgressFn;
using firmware::Segment;

void report(const ProgressFn& progress, ProgramPhase phase, std::uint64_t done,
            std::uint64_t total) {
    if (progress) {
        progress(ProgramProgress{.phase = phase, .done = done, .total = total});
    }
}

bool containsIgnoringCase(std::string_view text, std::string_view word) {
    return std::ranges::search(text, word, [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           }).begin() != text.end();
}

Error timedOut(std::string detail) { return makeError(ErrorCode::Timeout, std::move(detail)); }

} // namespace

JLinkProgrammer::JLinkProgrammer(IJLinkApi& api, JLinkProbe& probe, Clock& clock,
                                 ProgrammerConfig config)
    : m_api(api), m_probe(probe), m_clock(clock), m_config(std::move(config)) {}

std::string JLinkProgrammer::libraryDetail(std::string detail) {
    const std::string libraryText = m_api.takeErrorText();
    if (!libraryText.empty()) {
        detail += "; J-Link: " + libraryText;
    }
    return detail;
}

int JLinkProgrammer::selectCtrlAp(const ChipFamily& family) {
    if (const int configured = m_api.coresightConfigure(); configured < 0) {
        return configured;
    }
    return m_api.coresightWrite(dpSelect, false, std::uint32_t{family.ctrlApIndex} << 24U);
}

std::optional<bool> JLinkProgrammer::isProtected(const ChipFamily& family) {
    if (selectCtrlAp(family) < 0) {
        return std::nullopt;
    }
    const auto status = m_api.coresightRead(ctrlApApProtectStatus, true);
    if (!status) {
        return std::nullopt;
    }
    return (*status & 1U) == 0;
}

bool JLinkProgrammer::ctrlApPresent(const ChipFamily& family, std::string& detail) {
    if (m_api.coresightConfigure() < 0 ||
        m_api.coresightWrite(dpSelect, false,
                             (std::uint32_t{family.ctrlApIndex} << 24U) | idrBank) < 0) {
        detail += std::format("; {} CTRL-AP {}: not selectable", family.name, family.ctrlApIndex);
        return false;
    }
    const auto idr = m_api.coresightRead(ctrlApIdr, true);
    const bool present = idr && (*idr & ctrlApIdrMask) == (family.ctrlApIdr & ctrlApIdrMask);
    detail += std::format("; {} CTRL-AP {} IDR {}", family.name, family.ctrlApIndex,
                          idr ? std::format("0x{:08X}", *idr)
                              : std::format("read error {}", idr.error()));
    return present;
}

const ChipFamily* JLinkProgrammer::answeringFamily(std::string& detail) {
    for (const ChipFamily& family : m_config.families) {
        if (ctrlApPresent(family, detail)) {
            return &family;
        }
    }
    return nullptr;
}

std::optional<std::uint32_t> JLinkProgrammer::readStm32Register(std::uint32_t address,
                                                                std::string& tried) {
    for (int attempt = 1; attempt <= stm32ReadAttempts; ++attempt) {
        const auto value = m_api.readU32(address);
        if (value && *value != 0) {
            return *value;
        }
        tried +=
            std::format("{}0x{:08X}#{}: {}", tried.empty() ? "" : "; ", address, attempt,
                        value ? std::string("0") : std::format("read error {}", value.error()));
    }
    return std::nullopt;
}

Result<ChipInfo> JLinkProgrammer::identifyStm32(const Stm32Protection& stm32) {
    std::string tried;
    const auto idcode = readStm32Register(stm32.idcodeAddress, tried);
    const auto optr = readStm32Register(stm32.flashInterface + stm32.optrOffset, tried);
    if (!idcode || !optr) {
        Error error = makeError(ErrorCode::TargetConnectFailed, libraryDetail(tried));
        setText(error, ErrorText::Message, "hwio::jlink",
                QT_TRANSLATE_NOOP("hwio::jlink", "The chip ID could not be read (DBGMCU_IDCODE)."));
        return std::unexpected(std::move(error));
    }
    ChipInfo info;
    info.family = stm32.family;
    info.devId = static_cast<std::uint16_t>(*idcode & 0x0FFFU);
    info.revId = static_cast<std::uint16_t>(*idcode >> 16U);
    info.rdpLevel = stm32RdpLevelOf(static_cast<std::uint8_t>(*optr & 0xFFU));
    info.protectedChip = info.rdpLevel >= 1;
    // FLASH_SR and the stored option word are informational (unlock checks them); a failed read
    // leaves them 0.
    if (const auto sr = m_api.readU32(stm32.flashInterface + stm32.srOffset); sr) {
        info.optionError = (*sr & stm32.optionErrorMask) != 0;
    }
    if (const auto word = m_api.readU32(stm32.optionBytes); word) {
        info.optionWord = *word;
    }
    // F_SIZE may not be readable at RDP level 1 and reads 0 now and then while the CPU runs.
    std::string sizeTried;
    if (const auto size = readStm32Register(stm32.flashSizeAddress, sizeTried); size) {
        info.flashSizeKib = static_cast<std::uint16_t>(*size & 0xFFFFU);
    } else {
        qCInfo(log::lcFlash).noquote() << "STM32 flash size not readable:" << toQString(sizeTried);
    }
    qCInfo(log::lcFlash).noquote()
        << QStringLiteral("%1: DEV_ID 0x%2 REV_ID 0x%3, %4 KiB, RDP level %5 (OPTR 0x%6)")
               .arg(toQString(stm32.family))
               .arg(info.devId, 3, 16, QLatin1Char('0'))
               .arg(info.revId, 4, 16, QLatin1Char('0'))
               .arg(info.flashSizeKib)
               .arg(info.rdpLevel)
               .arg(*optr, 8, 16, QLatin1Char('0'));
    return info;
}

Result<ChipInfo> JLinkProgrammer::identify() {
    if (m_config.stm32) {
        return identifyStm32(*m_config.stm32);
    }
    std::string tried;
    for (const ChipFamily& family : m_config.families) {
        const auto part = m_api.readU32(family.ficrPart);
        if (part && *part != 0 && *part != 0xFFFFFFFFU) {
            const auto variant = m_api.readU32(family.ficrVariant);
            ChipInfo info{.part = *part,
                          .variant = variant.value_or(0),
                          .protectedChip = false,
                          .name = {},
                          .family = family.name};
            qCInfo(log::lcFlash).noquote() << QStringLiteral("FICR (%1): part 0x%2 variant 0x%3")
                                                  .arg(toQString(family.name))
                                                  .arg(info.part, 0, 16)
                                                  .arg(info.variant, 8, 16, QLatin1Char('0'));
            return info;
        }
        tried += std::format(
            "{}{} FICR 0x{:08X}: {}", tried.empty() ? "" : "; ", family.name, family.ficrPart,
            part ? std::format("0x{:08X}", *part) : std::format("read error {}", part.error()));
    }
    // FICR unreadable (a protected chip blocks the memory access port). Only an AP index that holds
    // a Nordic CTRL-AP (IDR) is trusted (review R2: an absent AP reads 0, which would look like
    // "protected"); the AP index tells the families apart (review R15).
    if (const ChipFamily* family = answeringFamily(tried)) {
        const bool expected = family == &m_config.families.front();
        const auto protectedChip = expected ? isProtected(*family) : std::optional<bool>(false);
        if (protectedChip) {
            qCInfo(log::lcFlash).noquote()
                << "CTRL-AP" << family->ctrlApIndex << "of" << toQString(family->name)
                << (*protectedChip ? "reports APPROTECT enabled" : "answers");
            return ChipInfo{.part = 0,
                            .variant = 0,
                            .protectedChip = *protectedChip,
                            .name = {},
                            .family = family->name};
        }
    }
    Error error = makeError(ErrorCode::TargetConnectFailed, libraryDetail(tried));
    setText(error, ErrorText::Message, "hwio::jlink",
            QT_TRANSLATE_NOOP("hwio::jlink", "The MCU type could not be read (FICR)."));
    return std::unexpected(std::move(error));
}

DownloadReport parseDownloadMessages(std::span<const std::string> lines) {
    // J-Link V9.84 wording of the flash loader's per-bank summary (plan 0050, ADR-0376); another
    // wording leaves the report empty (never guessed).
    static const QRegularExpression skipped(QStringLiteral(
        R"(Flash download: Bank \d+ @ 0x[0-9A-Fa-f]+: Skipped\. Contents already match)"));
    static const QRegularExpression affected(QStringLiteral(
        R"(Flash download: Bank \d+ @ 0x[0-9A-Fa-f]+: \d+ ranges? affected \((\d+) bytes\))"));
    DownloadReport report;
    std::uint64_t bytes = 0;
    bool anyChanged = false;
    bool anyBank = false;
    for (const std::string& line : lines) {
        const QString text = QString::fromStdString(line);
        if (skipped.match(text).hasMatch()) {
            anyBank = true;
            continue;
        }
        if (const QRegularExpressionMatch match = affected.match(text); match.hasMatch()) {
            bool ok = false;
            const qulonglong count = match.captured(1).toULongLong(&ok);
            if (!ok) {
                return {}; // a byte count we cannot read: say nothing
            }
            anyBank = true;
            anyChanged = true;
            bytes += count;
        }
    }
    if (anyBank) {
        report.unchanged = !anyChanged;
        report.affectedBytes = bytes;
    }
    return report;
}

Error JLinkProgrammer::abandonDownload(std::string detail) {
    // ADR-0109 (Phase 0 item 0.10): closing the J-Link without EndDownload still programs the
    // buffered data, so an abandoned download is an interrupted flash.
    qCWarning(log::lcFlash).noquote()
        << "Flash download abandoned (" << toQString(detail)
        << "): closing the J-Link; the buffered data may be partly programmed";
    m_probe.close();
    Error error =
        makeError(ErrorCode::TargetProgramFailed, "flash download abandoned: " + std::move(detail));
    setText(error, ErrorText::Cause, "hwio::jlink",
            QT_TRANSLATE_NOOP("hwio::jlink",
                              "Programming was interrupted; the firmware may be partly written."));
    setText(error, ErrorText::Suggestion, "hwio::jlink",
            QT_TRANSLATE_NOOP("hwio::jlink",
                              "Check the cables and power, then flash the firmware again."));
    return error;
}

Result<DownloadReport> JLinkProgrammer::download(const std::vector<Segment>& segments,
                                                 bool programsData, ErrorCode failure,
                                                 const ProgressFn& progress,
                                                 const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::Cancelled);
    }
    std::uint64_t total = 0;
    for (const Segment& segment : segments) {
        total += segment.data.size();
    }
    const auto deadline = m_clock.now() + m_config.timing.programTimeout;
    report(progress, ProgramPhase::Erasing, 0, 0);
    m_api.beginDownload();
    for (const Segment& segment : segments) {
        const std::span<const std::byte> data(segment.data);
        for (std::size_t offset = 0; offset < data.size(); offset += m_config.timing.writeChunk) {
            if (m_clock.now() > deadline) {
                return std::unexpected(abandonDownload(std::format(
                    "buffering exceeded the deadline of {} s",
                    std::chrono::duration_cast<std::chrono::seconds>(m_config.timing.programTimeout)
                        .count())));
            }
            const auto chunk =
                data.subspan(offset, std::min(m_config.timing.writeChunk, data.size() - offset));
            const auto address = static_cast<std::uint32_t>(segment.address + offset);
            const int written = m_api.writeMemory(address, chunk);
            if (written < 0) {
                // The library's error text is taken before the probe is closed.
                return std::unexpected(abandonDownload(
                    libraryDetail(std::format("JLINKARM_WriteMem(0x{:08X}, {}) returned {}",
                                              address, chunk.size(), written))));
            }
        }
    }
    // The loader's own actions (review R6): "Compare" and "Erase" -> Erasing, "Program" and
    // "Verify" -> Programming (the loader's own verify), anything else keeps the phase; never back
    // to Erasing. An erase (eraseCode) reports everything as Erasing.
    ProgramPhase current = ProgramPhase::Erasing;
    const FlashProgressFn loaderProgress = [&](std::string_view action, int percent) {
        if (programsData &&
            (containsIgnoringCase(action, "program") || containsIgnoringCase(action, "verify"))) {
            current = ProgramPhase::Programming;
        }
        const auto clamped = static_cast<std::uint64_t>(std::clamp(percent, 0, 100));
        report(progress, current, total * clamped / 100, total);
    };
    // Plan 0050 (ADR-0376): only the messages of EndDownload are parsed (the queue is drained of
    // the buffering's messages first, which also leaves room for the loader's lines).
    m_api.flushMessages();
    const int result = m_api.endDownload(loaderProgress);
    const std::vector<std::string> loaderLines =
        m_api.flushMessagesContaining(downloadMessageMarker, maxDownloadMessages);
    if (result < 0) {
        return std::unexpected(makeError(
            failure, libraryDetail(std::format("JLINKARM_EndDownload returned {}", result))));
    }
    if (m_clock.now() > deadline) {
        return std::unexpected(timedOut("flash download exceeded the deadline"));
    }
    return parseDownloadMessages(loaderLines);
}

Result<DownloadReport> JLinkProgrammer::program(const FirmwareImage& image,
                                                const ProgressFn& progress, std::stop_token stop) {
    auto downloaded =
        download(image.segments(), true, ErrorCode::TargetProgramFailed, progress, stop);
    if (!downloaded) {
        return downloaded;
    }
    report(progress, ProgramPhase::Programming, image.totalBytes(), image.totalBytes());
    if (downloaded->unchanged.value_or(false)) {
        qCInfo(log::lcFlash).noquote()
            << QStringLiteral("Flash contents already matched; nothing programmed (%1 bytes in %2 "
                              "segment(s))")
                   .arg(image.totalBytes())
                   .arg(image.segments().size());
    } else {
        qCInfo(log::lcFlash).noquote()
            << QStringLiteral("Programmed %1 bytes in %2 segment(s)%3")
                   .arg(image.totalBytes())
                   .arg(image.segments().size())
                   .arg(downloaded->affectedBytes
                            ? QStringLiteral(" (%1 bytes affected)").arg(*downloaded->affectedBytes)
                            : QString());
    }
    return downloaded;
}

Status JLinkProgrammer::verify(const FirmwareImage& image, const ProgressFn& progress,
                               std::stop_token stop) {
    const std::uint64_t total = image.totalBytes();
    const auto deadline = m_clock.now() + m_config.timing.verifyTimeout;
    std::vector<std::byte> buffer(m_config.timing.verifyChunk);
    std::uint64_t done = 0;
    report(progress, ProgramPhase::Verifying, 0, total);
    for (const Segment& segment : image.segments()) {
        const std::span<const std::byte> expected(segment.data);
        for (std::size_t offset = 0; offset < expected.size(); offset += buffer.size()) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::Cancelled);
            }
            if (m_clock.now() > deadline) {
                return std::unexpected(timedOut("verify exceeded the deadline"));
            }
            const auto want =
                expected.subspan(offset, std::min(buffer.size(), expected.size() - offset));
            const auto address = static_cast<std::uint32_t>(segment.address + offset);
            const auto read = std::span(buffer).first(want.size());
            const int count = m_api.readMemory(address, read);
            if (count < 0 || static_cast<std::size_t>(count) < want.size()) {
                return std::unexpected(makeError(
                    ErrorCode::TargetConnectFailed,
                    libraryDetail(std::format("JLINKARM_ReadMemEx(0x{:08X}, {}) returned {}",
                                              address, want.size(), count))));
            }
            const auto mismatch = std::ranges::mismatch(want, read);
            if (mismatch.in1 != want.end()) {
                const auto index = static_cast<std::size_t>(mismatch.in1 - want.begin());
                const std::uint32_t at = address + static_cast<std::uint32_t>(index);
                Error error = makeError(ErrorCode::FirmwareVerifyFailed,
                                        std::format("0x{:08X}: expected 0x{:02X}, read 0x{:02X}",
                                                    at, static_cast<unsigned>(*mismatch.in1),
                                                    static_cast<unsigned>(*mismatch.in2)));
                setText(error, ErrorText::Message, "hwio::jlink",
                        QT_TRANSLATE_NOOP("hwio::jlink", "Verification failed at address %1."),
                        {QStringLiteral("0x%1").arg(at, 8, 16, QLatin1Char('0'))});
                return std::unexpected(std::move(error));
            }
            done += want.size();
            report(progress, ProgramPhase::Verifying, done, total);
        }
    }
    qCInfo(log::lcFlash) << "Verified" << total << "bytes";
    return {};
}

Result<DownloadReport> JLinkProgrammer::eraseCode(firmware::AddressRange flash,
                                                  const ProgressFn& progress,
                                                  std::stop_token stop) {
    // To confirm in Phase 0 (plan 0003, 0.3): 0xFF over the whole range through the flash loader,
    // which erases every sector whose content differs; the UICR is outside the range.
    std::vector<Segment> segments;
    constexpr std::uint64_t piece = 64ULL * 1024;
    for (std::uint64_t address = flash.start; address < flash.end; address += piece) {
        const std::uint64_t size = std::min(piece, flash.end - address);
        segments.push_back(Segment{.address = static_cast<std::uint32_t>(address),
                                   .data = std::vector<std::byte>(size, std::byte{0xFF})});
    }
    auto downloaded = download(segments, false, ErrorCode::TargetEraseFailed, progress, stop);
    if (!downloaded) {
        return downloaded;
    }
    report(progress, ProgramPhase::Erasing, flash.size(), flash.size());
    qCInfo(log::lcFlash).noquote()
        << QStringLiteral("Erased the code flash 0x%1-0x%2%3")
               .arg(flash.start, 8, 16, QLatin1Char('0'))
               .arg(flash.end, 8, 16, QLatin1Char('0'))
               .arg(downloaded->unchanged.value_or(false) ? QStringLiteral(" (already erased)")
                                                          : QString());
    return downloaded;
}

Status JLinkProgrammer::eraseAll() {
    // HIL H5: with only a halt (pylink erase()), JLINK_EraseChip on a running nRF52840 returned 0
    // and erased nothing ("Erase: 0.001s"). Reset and halt first, as J-Link Commander's erase does.
    if (auto halted = m_probe.reset(/*halt=*/true); !halted) {
        return halted;
    }
    const auto started = m_clock.now();
    const int result = m_api.eraseChip();
    m_api.flushMessages();
    if (result < 0) {
        return std::unexpected(
            makeError(ErrorCode::TargetEraseFailed,
                      libraryDetail(std::format("JLINK_EraseChip returned {}", result))));
    }
    if (m_clock.now() - started > m_config.timing.eraseAllTimeout) {
        return std::unexpected(timedOut("JLINK_EraseChip exceeded the deadline"));
    }
    qCInfo(log::lcFlash) << "Erased the chip (flash and UICR)";
    return {};
}

Status JLinkProgrammer::recover(std::stop_token stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::Cancelled);
    }
    if (m_config.families.empty()) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no chip family"));
    }
    const ChipFamily& family = m_config.families.front();
    const auto failed = [this](std::string detail) {
        return std::unexpected(
            makeError(ErrorCode::TargetRecoverFailed, libraryDetail(std::move(detail))));
    };
    // Review R2: ERASEALL is written only to the expected family's CTRL-AP, after its IDR matched.
    std::string idrDetail;
    if (const ChipFamily* answering = answeringFamily(idrDetail); answering != &family) {
        if (answering != nullptr) {
            return std::unexpected(
                makeError(ErrorCode::TargetWrongFamily,
                          libraryDetail(std::format("expected {}, found {}{}", family.name,
                                                    answering->name, idrDetail))));
        }
        return std::unexpected(
            makeError(ErrorCode::TargetConnectFailed,
                      libraryDetail("no CTRL-AP answers with the expected IDR" + idrDetail)));
    }
    if (const int selected = selectCtrlAp(family); selected < 0) {
        return failed(
            std::format("selecting CTRL-AP {} returned {}", family.ctrlApIndex, selected));
    }
    qCInfo(log::lcFlash) << "Recover: CTRL-AP" << family.ctrlApIndex << "ERASEALL";
    if (const int written = m_api.coresightWrite(ctrlApEraseAll, true, 1); written < 0) {
        return failed(std::format("CTRL-AP ERASEALL write returned {}", written));
    }
    // Never interrupted (plan: no cancel in the middle of a recover).
    const auto deadline = m_clock.now() + m_config.timing.recoverTimeout;
    while (true) {
        const auto status = m_api.coresightRead(ctrlApEraseAllStatus, true);
        if (!status) {
            return failed(std::format("CTRL-AP ERASEALLSTATUS read returned {}", status.error()));
        }
        if (*status == 0) {
            break;
        }
        if (m_clock.now() >= deadline) {
            return std::unexpected(makeError(ErrorCode::TargetRecoverFailed,
                                             libraryDetail("ERASEALL did not finish within 60 s")));
        }
        (void)m_clock.sleepFor(m_config.timing.recoverPoll, std::stop_token{});
    }
    // CTRL-AP RESET toggle: a system reset that ends the erase-all state.
    if (m_api.coresightWrite(ctrlApReset, true, 1) < 0 ||
        m_api.coresightWrite(ctrlApReset, true, 0) < 0 ||
        m_api.coresightWrite(ctrlApEraseAll, true, 0) < 0) {
        return failed("CTRL-AP RESET toggle failed");
    }
    m_api.flushMessages();
    qCInfo(log::lcFlash) << "Recover: ERASEALL done";
    return {};
}

Status JLinkProgrammer::programRdpLevel0(std::stop_token stop) {
    if (!m_config.stm32) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument, "not an STM32 target"));
    }
    if (stop.stop_requested()) {
        return fail(ErrorCode::Cancelled);
    }
    if (Status available = require(OptionalFunction::WriteU32); !available) {
        return available;
    }
    const Stm32Protection& stm32 = *m_config.stm32;
    // The CPU is halted so that the firmware cannot touch the flash interface meanwhile.
    if (const int halted = m_api.halt(); halted != 0) {
        qCInfo(log::lcFlash) << "Unlock: halt returned" << halted << "(continuing)";
    }
    const auto optr = m_api.readU32(stm32.flashInterface + stm32.optrOffset);
    if (!optr) {
        return std::unexpected(
            stm32UnlockFailed(std::format("FLASH_OPTR read returned {}", optr.error())));
    }
    if (Status unlocked = unlockStm32Flash(stm32); !unlocked) {
        return unlocked;
    }
    const std::uint32_t word = stm32Level0OptionWord(*optr);
    qCInfo(log::lcFlash).noquote()
        << QStringLiteral("Unlock: writing the RDP option word 0x%1 (OPTR 0x%2)")
               .arg(word, 8, 16, QLatin1Char('0'))
               .arg(*optr, 8, 16, QLatin1Char('0'));
    if (const int written = writeStm32(stm32.optionBytes, word); written != 0) {
        return std::unexpected(
            stm32UnlockFailed(std::format("option word write returned {}", written)));
    }
    // Never interrupted once written: BSY is awaited, then the reload is launched.
    if (Status done = awaitStm32OptionWrite(stm32); !done) {
        return done;
    }
    // The reload resets the chip at once, so the write itself may report an error.
    const int launched = writeStm32(stm32.flashInterface + stm32.pecrOffset, stm32.oblLaunchMask);
    m_api.flushMessages();
    qCInfo(log::lcFlash) << "Unlock: option-byte reload launched (" << launched << ")";
    return {};
}

int JLinkProgrammer::writeStm32(std::uint32_t address, std::uint32_t value) {
    // ADR-0276: RDP level 2 is never programmed by any path (nor anything without an STM32).
    if (!m_config.stm32 ||
        (address == m_config.stm32->optionBytes && (value & 0xFFU) == stm32RdpLevel2)) {
        return -1;
    }
    return m_api.writeU32(address, value);
}

Error JLinkProgrammer::stm32UnlockFailed(std::string detail) {
    return makeError(ErrorCode::TargetUnlockFailed, libraryDetail(std::move(detail)));
}

Status JLinkProgrammer::unlockStm32Flash(const Stm32Protection& stm32) {
    const std::uint32_t pecr = stm32.flashInterface + stm32.pecrOffset;
    if (writeStm32(stm32.flashInterface + stm32.srOffset, stm32.errorMask) != 0) {
        return std::unexpected(stm32UnlockFailed("clearing FLASH_SR failed"));
    }
    auto control = m_api.readU32(pecr);
    if (!control) {
        return std::unexpected(
            stm32UnlockFailed(std::format("FLASH_PECR read returned {}", control.error())));
    }
    const auto writeKeys = [&](std::uint32_t offset, const std::array<std::uint32_t, 2>& keys) {
        return writeStm32(stm32.flashInterface + offset, keys[0]) == 0 &&
               writeStm32(stm32.flashInterface + offset, keys[1]) == 0;
    };
    if ((*control & stm32.peLockMask) != 0 && !writeKeys(stm32.peKeyOffset, stm32.peKeys)) {
        return std::unexpected(stm32UnlockFailed("writing the PECR keys failed"));
    }
    if ((*control & stm32.optLockMask) != 0 && !writeKeys(stm32.optKeyOffset, stm32.optKeys)) {
        return std::unexpected(stm32UnlockFailed("writing the option-byte keys failed"));
    }
    control = m_api.readU32(pecr);
    if (!control || (*control & (stm32.peLockMask | stm32.optLockMask)) != 0) {
        return std::unexpected(stm32UnlockFailed(
            control ? std::format("FLASH_PECR still locked (0x{:08X})", *control)
                    : std::format("FLASH_PECR read returned {}", control.error())));
    }
    return {};
}

Status JLinkProgrammer::awaitStm32OptionWrite(const Stm32Protection& stm32) {
    const std::uint32_t sr = stm32.flashInterface + stm32.srOffset;
    const auto deadline = m_clock.now() + m_config.timing.optionWriteTimeout;
    while (true) {
        const auto status = m_api.readU32(sr);
        if (status && (*status & stm32.busyMask) == 0) {
            return {};
        }
        if (m_clock.now() >= deadline) {
            return std::unexpected(stm32UnlockFailed(
                status ? std::format("FLASH_SR BSY still set (0x{:08X})", *status)
                       : std::format("FLASH_SR read returned {}", status.error())));
        }
        (void)m_clock.sleepFor(m_config.timing.optionWritePoll, std::stop_token{});
    }
}

Status JLinkProgrammer::require(OptionalFunction function) {
    if (m_api.has(function)) {
        return {};
    }
    return std::unexpected(makeError(ErrorCode::DependencyJLinkFunctionMissing,
                                     std::string(optionalFunctionName(function))));
}

Error JLinkProgrammer::memoryAccessFailed(std::string_view function, std::uint32_t address,
                                          std::size_t size, int code) {
    return makeError(
        ErrorCode::TargetMemoryAccessFailed,
        libraryDetail(std::format("{}(0x{:08X}, {}) returned {}", function, address, size, code)));
}

namespace {

// usage.invalid_argument unless 1 <= size <= limit, size a multiple of the unit, the address
// aligned to it and the range not running past 0xFFFFFFFF (ADR-0160).
Status checkAccess(std::uint32_t address, std::size_t size, AccessWidth width, std::size_t limit) {
    const std::size_t unit = unitBytes(width);
    constexpr std::uint64_t addressSpaceEnd = std::uint64_t{1} << 32U;
    if (size == 0 || size > limit || size % unit != 0 || address % unit != 0 ||
        std::uint64_t{address} + size > addressSpaceEnd) {
        return std::unexpected(makeError(
            ErrorCode::UsageInvalidArgument,
            std::format("address 0x{:08X}, size {} (1..{} bytes, a multiple of {}, aligned, "
                        "not past 0xFFFFFFFF)",
                        address, size, limit, unit)));
    }
    return {};
}

// Little-endian unit `index` of `bytes` (unit = sizeof(T)).
template <class T> T unitAt(std::span<const std::byte> bytes, std::size_t index) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<T>(
            value | (static_cast<T>(std::to_integer<unsigned>(bytes[(index * sizeof(T)) + i]))
                     << (8U * i)));
    }
    return value;
}

void storeUnit(std::span<std::byte> bytes, std::size_t index, std::uint32_t value,
               std::size_t unit) {
    for (std::size_t i = 0; i < unit; ++i) {
        bytes[(index * unit) + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
    }
}

} // namespace

Result<std::vector<std::byte>> JLinkProgrammer::readMemory(std::uint32_t address, std::size_t size,
                                                           AccessWidth width) {
    if (Status valid = checkAccess(address, size, width, maxReadMemoryBytes); !valid) {
        return std::unexpected(std::move(valid).error());
    }
    std::vector<std::byte> buffer(size);
    switch (width) {
    case AccessWidth::Bits8: {
        const int count = m_api.readMemory(address, buffer);
        if (count < 0 || std::cmp_less(count, size)) {
            return std::unexpected(memoryAccessFailed("JLINKARM_ReadMemEx", address, size, count));
        }
        break;
    }
    case AccessWidth::Bits16: {
        // ReadMemEx with access width 2 (pylink memory_read(nbits=16)). Whether the library counts
        // bytes or units here is to be confirmed on hardware (plan 0013 E3): both mean "all read".
        const int count = m_api.readMemory16(address, buffer);
        if (count < 0 || (std::cmp_not_equal(count, size) && std::cmp_not_equal(count, size / 2))) {
            return std::unexpected(
                memoryAccessFailed("JLINKARM_ReadMemEx (16-bit)", address, size, count));
        }
        break;
    }
    case AccessWidth::Bits32: {
        std::vector<std::uint32_t> units(size / 4);
        const int count = m_api.readU32s(address, units);
        if (count < 0 || std::cmp_less(count, units.size())) {
            return std::unexpected(memoryAccessFailed("JLINKARM_ReadMemU32", address, size, count));
        }
        for (std::size_t i = 0; i < units.size(); ++i) {
            storeUnit(buffer, i, units[i], 4);
        }
        break;
    }
    }
    return buffer;
}

Status JLinkProgrammer::writeMemory(std::uint32_t address, std::span<const std::byte> data,
                                    AccessWidth width) {
    if (Status valid = checkAccess(address, data.size(), width, maxWriteMemoryBytes); !valid) {
        return valid;
    }
    switch (width) {
    case AccessWidth::Bits8: {
        // JLINKARM_WriteMem outside a download: a direct write of target memory.
        const int written = m_api.writeMemory(address, data);
        if (written < 0 || std::cmp_less(written, data.size())) {
            return std::unexpected(
                memoryAccessFailed("JLINKARM_WriteMem", address, data.size(), written));
        }
        break;
    }
    case AccessWidth::Bits16:
        if (Status present = require(OptionalFunction::WriteU16); !present) {
            return present;
        }
        for (std::size_t i = 0; i < data.size() / 2; ++i) {
            const auto at = static_cast<std::uint32_t>(address + (i * 2));
            if (const int result = m_api.writeU16(at, unitAt<std::uint16_t>(data, i));
                result != 0) {
                return std::unexpected(memoryAccessFailed("JLINKARM_WriteU16", at, 2, result));
            }
        }
        break;
    case AccessWidth::Bits32:
        if (Status present = require(OptionalFunction::WriteU32); !present) {
            return present;
        }
        for (std::size_t i = 0; i < data.size() / 4; ++i) {
            const auto at = static_cast<std::uint32_t>(address + (i * 4));
            if (const int result = m_api.writeU32(at, unitAt<std::uint32_t>(data, i));
                result != 0) {
                return std::unexpected(memoryAccessFailed("JLINKARM_WriteU32", at, 4, result));
            }
        }
        break;
    }
    m_api.flushMessages();
    return {};
}

Status JLinkProgrammer::halt() {
    Status halted = m_probe.halt();
    m_api.flushMessages();
    return halted;
}

Status JLinkProgrammer::go() {
    m_probe.go();
    m_api.flushMessages();
    return {};
}

Result<bool> JLinkProgrammer::isHalted() {
    if (Status present = require(OptionalFunction::IsHalted); !present) {
        return std::unexpected(std::move(present).error());
    }
    const int result = m_api.isHalted();
    if (result < 0) {
        return std::unexpected(
            makeError(ErrorCode::TargetConnectFailed,
                      libraryDetail(std::format("JLINKARM_IsHalted returned {}", result))));
    }
    return result > 0;
}

Result<std::uint32_t> JLinkProgrammer::coreId() {
    if (Status present = require(OptionalFunction::GetId); !present) {
        return std::unexpected(std::move(present).error());
    }
    return m_api.coreId();
}

Result<std::vector<RegisterValue>> JLinkProgrammer::readRegisters() {
    for (const OptionalFunction function :
         {OptionalFunction::IsHalted, OptionalFunction::GetRegisterList,
          OptionalFunction::GetRegisterName, OptionalFunction::ReadRegs}) {
        if (Status present = require(function); !present) {
            return std::unexpected(std::move(present).error());
        }
    }
    auto halted = isHalted();
    if (!halted) {
        return std::unexpected(std::move(halted).error());
    }
    if (!*halted) {
        return std::unexpected(makeError(ErrorCode::TargetNotHalted));
    }
    auto indexes = m_api.registerList();
    if (!indexes) {
        return std::unexpected(makeError(
            ErrorCode::TargetConnectFailed,
            libraryDetail(std::format("JLINKARM_GetRegisterList returned {}", indexes.error()))));
    }
    if (indexes->size() > maxRegisters) {
        qCInfo(log::lcJLink) << "Register list truncated from" << indexes->size() << "to"
                             << maxRegisters;
        indexes->resize(maxRegisters);
    }
    std::vector<std::uint32_t> values(indexes->size(), 0);
    std::vector<std::uint8_t> confirmed(indexes->size(), 0);
    if (!indexes->empty()) {
        const int result = m_api.readRegisters(*indexes, values, confirmed);
        if (result < 0) {
            return std::unexpected(
                makeError(ErrorCode::TargetConnectFailed,
                          libraryDetail(std::format("JLINKARM_ReadRegs returned {}", result))));
        }
        if (std::ranges::none_of(confirmed, [](std::uint8_t item) { return item != 0; })) {
            return std::unexpected(
                makeError(ErrorCode::TargetConnectFailed,
                          libraryDetail("JLINKARM_ReadRegs confirmed no register")));
        }
    }
    // ADR-0371: a register the library did not confirm is left out and logged (at most
    // maxUnconfirmedNames names in the log line).
    constexpr std::size_t maxUnconfirmedNames = 8;
    std::size_t unconfirmed = 0;
    std::string unconfirmedNames;
    std::vector<RegisterValue> registers;
    registers.reserve(indexes->size());
    for (std::size_t i = 0; i < indexes->size(); ++i) {
        std::string name = m_api.registerName((*indexes)[i]);
        if (name.empty()) {
            name = std::format("#{}", (*indexes)[i]);
        }
        if (confirmed[i] == 0) {
            if (unconfirmed < maxUnconfirmedNames) {
                unconfirmedNames += unconfirmedNames.empty() ? name : ", " + name;
            }
            ++unconfirmed;
            continue;
        }
        registers.push_back(RegisterValue{.name = std::move(name), .value = values[i]});
    }
    if (unconfirmed > 0) {
        qCInfo(log::lcJLink).noquote() << QString::fromStdString(
            std::format("Registers not confirmed by the library: {} ({}{})", unconfirmed,
                        unconfirmedNames, unconfirmed > maxUnconfirmedNames ? ", ..." : ""));
    }
    m_api.flushMessages();
    return registers;
}

Result<std::vector<MemoryZone>> JLinkProgrammer::memoryZones() {
    if (Status present = require(OptionalFunction::GetMemZones); !present) {
        return std::unexpected(std::move(present).error());
    }
    auto zones = m_api.memoryZones(maxMemoryZones);
    m_api.flushMessages();
    if (!zones) {
        return std::unexpected(
            makeError(ErrorCode::TargetConnectFailed,
                      libraryDetail(std::format("JLINK_GetMemZones returned {}", zones.error()))));
    }
    return std::move(*zones);
}

Status JLinkProgrammer::reset(ResetMode mode) { return m_probe.reset(mode == ResetMode::Halt); }

} // namespace hwio::jlink
