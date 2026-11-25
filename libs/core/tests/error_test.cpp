#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/localized_text.h"
#include "hwio/core/result.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>
#include <QTranslator>
#include <QtTranslation>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace {

using hwio::ErrorCategory;
using hwio::ErrorCode;

TEST(ErrorTable, IdsAreUniqueAndRoundTrip) {
    std::set<std::string_view> ids;
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        EXPECT_TRUE(ids.insert(info.id).second) << info.id;
        EXPECT_EQ(hwio::idOf(info.code), info.id);
        EXPECT_EQ(hwio::categoryOf(info.code), info.category);
        const auto parsed = hwio::errorCodeFromId(info.id);
        EXPECT_EQ(parsed, std::optional<hwio::ErrorCode>(info.code)) << info.id;
    }
}

TEST(ErrorTable, KnownIdsAndCategories) {
    EXPECT_EQ(hwio::idOf(ErrorCode::IoFileNotFound), "io.file_not_found");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::IoFileNotFound), ErrorCategory::Io);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::SettingsNewerSchema), ErrorCategory::Io);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::Cancelled), ErrorCategory::Cancelled);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::UsageInvalidArgument), ErrorCategory::Usage);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::InternalQueueFull), ErrorCategory::Internal);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::Timeout), ErrorCategory::Timeout);
}

TEST(ErrorTable, UnknownIdIsRejected) {
    EXPECT_FALSE(hwio::errorCodeFromId("").has_value());
    EXPECT_FALSE(hwio::errorCodeFromId("io").has_value());
    EXPECT_FALSE(hwio::errorCodeFromId("IO.FILE_NOT_FOUND").has_value());
    EXPECT_FALSE(hwio::errorCodeFromId("probe.no_such_error").has_value());
    EXPECT_EQ(hwio::errorCodeFromId("probe.not_found"), ErrorCode::ProbeNotFound);
}

TEST(ErrorCategoryId, AllCategoriesHaveStableIds) {
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Usage), "usage");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Io), "io");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Dependency), "dependency");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Probe), "probe");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Target), "target");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Firmware), "firmware");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Network), "network");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Timeout), "timeout");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Cancelled), "cancelled");
    EXPECT_EQ(hwio::categoryId(ErrorCategory::Internal), "internal");
}

TEST(MakeError, EveryCodeHasDefaultTexts) {
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        const hwio::Error error = hwio::makeError(info.code, "detail");
        EXPECT_EQ(error.code, info.code);
        EXPECT_FALSE(error.message.empty()) << info.id;
        EXPECT_FALSE(error.cause.empty()) << info.id;
        EXPECT_FALSE(error.suggestion.empty()) << info.id;
        EXPECT_EQ(error.detail, "detail");
    }
    EXPECT_FALSE(hwio::makeError(ErrorCode::IoReadFailed).wizard.has_value());
    EXPECT_EQ(hwio::makeError(ErrorCode::Cancelled).message, "Cancelled by user.");
    EXPECT_EQ(hwio::makeError(ErrorCode::InternalUnexpected).suggestion,
              "Please report it with the log file (see Details).");
}

// M1 codes (plan/features/0002-m1-jlink-rtt.md): categories and wizards.
TEST(MakeError, JLinkCodesHaveCategoriesAndWizards) {
    EXPECT_EQ(hwio::categoryOf(ErrorCode::DependencyJLinkNotFound), ErrorCategory::Dependency);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::ProbeInUseByOtherProcess), ErrorCategory::Probe);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::TargetNoPower), ErrorCategory::Target);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::InternalSessionLimit), ErrorCategory::Internal);
    EXPECT_EQ(hwio::categoryOf(ErrorCode::UsageConfirmationRequired), ErrorCategory::Usage);
    EXPECT_EQ(hwio::idOf(ErrorCode::TargetRttBufferNotFound), "target.rtt_buffer_not_found");
    EXPECT_EQ(hwio::makeError(ErrorCode::DependencyJLinkTooOld).wizard,
              hwio::WizardId::FirstRunSetup);
    EXPECT_EQ(hwio::makeError(ErrorCode::TargetNoPower).wizard, hwio::WizardId::ConnectDevice);
    EXPECT_EQ(hwio::makeError(ErrorCode::TargetRttReadFailed).wizard,
              hwio::WizardId::Troubleshooting);
    // M4 (plan/features/0007): the troubleshooting topic explains how to close the other program.
    EXPECT_EQ(hwio::makeError(ErrorCode::ProbeInUseByOtherProcess).wizard,
              hwio::WizardId::Troubleshooting);
}

// Review fix 5 (2026-10-09): RTT write failures have their own code.
TEST(MakeError, RttWriteFailedCode) {
    EXPECT_EQ(hwio::idOf(ErrorCode::TargetRttWriteFailed), "target.rtt_write_failed");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::TargetRttWriteFailed), ErrorCategory::Target);
    EXPECT_EQ(hwio::errorCodeFromId("target.rtt_write_failed"), ErrorCode::TargetRttWriteFailed);
    const hwio::Error error = hwio::makeError(ErrorCode::TargetRttWriteFailed);
    EXPECT_EQ(error.message, "Writing to the device failed.");
    EXPECT_EQ(error.cause, "A cable was disconnected or the device was reset.");
    EXPECT_EQ(error.suggestion,
              "Check the cables; the console reconnects automatically if enabled.");
    EXPECT_EQ(error.wizard, hwio::WizardId::Troubleshooting);
}

// M1 review R11 / R18 (2026-10-10).
TEST(MakeError, FirmwareUpdateFailedAndNotConnectedCodes) {
    EXPECT_EQ(hwio::idOf(ErrorCode::ProbeFirmwareUpdateFailed), "probe.firmware_update_failed");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::ProbeFirmwareUpdateFailed), ErrorCategory::Probe);
    EXPECT_EQ(hwio::makeError(ErrorCode::ProbeFirmwareUpdateFailed).wizard,
              hwio::WizardId::Troubleshooting);
    EXPECT_EQ(hwio::idOf(ErrorCode::TargetNotConnected), "target.not_connected");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::TargetNotConnected), ErrorCategory::Target);
    const hwio::Error error = hwio::makeError(ErrorCode::TargetNotConnected);
    EXPECT_EQ(error.message, "The device is not connected.");
    EXPECT_EQ(error.cause, "The console is disconnected or still reconnecting.");
    EXPECT_EQ(error.wizard, hwio::WizardId::ConnectDevice);
}

// Review R52 (2026-10-11): a J-Link that does not answer (JLINKARM_GetHWStatus fails) has its own
// Probe code instead of probe.not_found.
TEST(MakeError, ProbeIoFailedCode) {
    EXPECT_EQ(hwio::idOf(ErrorCode::ProbeIoFailed), "probe.io_failed");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::ProbeIoFailed), ErrorCategory::Probe);
    EXPECT_EQ(hwio::errorCodeFromId("probe.io_failed"), ErrorCode::ProbeIoFailed);
    const hwio::Error error = hwio::makeError(ErrorCode::ProbeIoFailed);
    EXPECT_EQ(error.message, "Communication with the J-Link failed.");
    EXPECT_EQ(error.cause,
              "The J-Link did not answer: a USB glitch, or the USB cable was disconnected.");
    EXPECT_EQ(error.suggestion, "Check the USB cable between the J-Link and the computer; the "
                                "console reconnects automatically if enabled.");
    EXPECT_EQ(error.wizard, hwio::WizardId::Troubleshooting);
}

// Review R44: a closed stdout ends with `cancelled` and its own message; the cause, suggestion
// and the detail of the call site stay.
// M4 codes (plan/features/0007-m4-wizard-framework-diagnostics.md): ids, categories, wizards.
TEST(MakeError, M4Codes) {
    struct Row {
        ErrorCode code;
        std::string_view id;
        hwio::ErrorCategory category;
        std::optional<hwio::WizardId> wizard;
    };
    for (const Row& row : {
             Row{.code = ErrorCode::UsbAccessDenied,
                 .id = "usb.access_denied",
                 .category = hwio::ErrorCategory::Probe,
                 .wizard = hwio::WizardId::FirstRunSetup},
             Row{.code = ErrorCode::UsbWorldAccessible,
                 .id = "usb.world_accessible",
                 .category = hwio::ErrorCategory::Probe,
                 .wizard = hwio::WizardId::FirstRunSetup},
             Row{.code = ErrorCode::ProbeHeldByProgram,
                 .id = "probe.held_by_program",
                 .category = hwio::ErrorCategory::Probe,
                 .wizard = hwio::WizardId::Troubleshooting},
             Row{.code = ErrorCode::DiagReportWriteFailed,
                 .id = "diag.report_write_failed",
                 .category = hwio::ErrorCategory::Io,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::DiagCheckTimeout,
                 .id = "diag.check_timeout",
                 .category = hwio::ErrorCategory::Timeout,
                 .wizard = std::nullopt},
         }) {
        const hwio::Error error = hwio::makeError(row.code);
        EXPECT_EQ(hwio::idOf(row.code), row.id);
        EXPECT_EQ(hwio::categoryOf(row.code), row.category);
        EXPECT_EQ(hwio::errorCodeFromId(row.id), row.code);
        EXPECT_EQ(error.wizard, row.wizard) << row.id;
        EXPECT_FALSE(error.message.empty());
        EXPECT_FALSE(error.cause.empty());
        EXPECT_FALSE(error.suggestion.empty());
    }
}

TEST(ErrorTable, BleCodes) {
    struct Row {
        ErrorCode code;
        std::string_view id;
        hwio::ErrorCategory category;
        std::optional<hwio::WizardId> wizard;
    };
    using hwio::WizardId;
    constexpr auto ble = WizardId::BlePairing;
    for (const Row& row : {
             Row{.code = ErrorCode::BleAdapterNotFound,
                 .id = "ble.adapter_not_found",
                 .category = ErrorCategory::Dependency,
                 .wizard = ble},
             Row{.code = ErrorCode::BleAdapterOff,
                 .id = "ble.adapter_off",
                 .category = ErrorCategory::Dependency,
                 .wizard = ble},
             Row{.code = ErrorCode::BlePermissionDenied,
                 .id = "ble.permission_denied",
                 .category = ErrorCategory::Dependency,
                 .wizard = std::nullopt}, // ADR-0250: no wizard until plan 0032
             Row{.code = ErrorCode::BleStackUnavailable,
                 .id = "ble.stack_unavailable",
                 .category = ErrorCategory::Dependency,
                 .wizard = ble},
             Row{.code = ErrorCode::BleDeviceNotFound,
                 .id = "ble.device_not_found",
                 .category = ErrorCategory::Target,
                 .wizard = ble},
             Row{.code = ErrorCode::BleConnectFailed,
                 .id = "ble.connect_failed",
                 .category = ErrorCategory::Target,
                 .wizard = ble},
             Row{.code = ErrorCode::BleServiceNotFound,
                 .id = "ble.service_not_found",
                 .category = ErrorCategory::Target,
                 .wizard = ble},
             Row{.code = ErrorCode::BleNotPaired,
                 .id = "ble.not_paired",
                 .category = ErrorCategory::Target,
                 .wizard = ble},
             Row{.code = ErrorCode::BlePairingFailed,
                 .id = "ble.pairing_failed",
                 .category = ErrorCategory::Target,
                 .wizard = ble},
             Row{.code = ErrorCode::BlePairingNotSupported,
                 .id = "ble.pairing_not_supported",
                 .category = ErrorCategory::Usage,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::BleWriteFailed,
                 .id = "ble.write_failed",
                 .category = ErrorCategory::Target,
                 .wizard = ble},
             Row{.code = ErrorCode::BleWriteTimeout,
                 .id = "ble.write_timeout",
                 .category = ErrorCategory::Target,
                 .wizard = ble},
             Row{.code = ErrorCode::BleDisconnected,
                 .id = "ble.disconnected",
                 .category = ErrorCategory::Target,
                 .wizard = ble},
             Row{.code = ErrorCode::BleNoPairingAgent,
                 .id = "ble.no_pairing_agent",
                 .category = ErrorCategory::Dependency,
                 .wizard = ble},
             Row{.code = ErrorCode::BleInUseByOtherProcess,
                 .id = "ble.in_use_by_other_process",
                 .category = ErrorCategory::Probe,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpPortInUse,
                 .id = "mcp.port_in_use",
                 .category = ErrorCategory::Io,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpListenFailed,
                 .id = "mcp.listen_failed",
                 .category = ErrorCategory::Io,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpTokenFile,
                 .id = "mcp.token_file",
                 .category = ErrorCategory::Io,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpToolNotAllowed,
                 .id = "mcp.tool_not_allowed",
                 .category = ErrorCategory::Usage,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpConsentDenied,
                 .id = "mcp.consent_denied",
                 .category = ErrorCategory::Cancelled,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpConsentTimeout,
                 .id = "mcp.consent_timeout",
                 .category = ErrorCategory::Timeout,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpDeviceAmbiguous,
                 .id = "mcp.device_ambiguous",
                 .category = ErrorCategory::Usage,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpDeviceNotOpen,
                 .id = "mcp.device_not_open",
                 .category = ErrorCategory::Usage,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpCapabilityMissing,
                 .id = "mcp.capability_missing",
                 .category = ErrorCategory::Usage,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::McpTooManyRequests,
                 .id = "mcp.too_many_requests",
                 .category = ErrorCategory::Internal,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::TargetNotHalted,
                 .id = "target.not_halted",
                 .category = ErrorCategory::Target,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::TargetAddressOutOfRange,
                 .id = "target.address_out_of_range",
                 .category = ErrorCategory::Target,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::TargetMemoryAccessFailed,
                 .id = "target.memory_access_failed",
                 .category = ErrorCategory::Target,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::IoFileExists,
                 .id = "io.file_exists",
                 .category = ErrorCategory::Io,
                 .wizard = std::nullopt},
             Row{.code = ErrorCode::DependencyJLinkFunctionMissing,
                 .id = "dependency.jlink_function_missing",
                 .category = ErrorCategory::Dependency,
                 .wizard = std::nullopt},
         }) {
        const hwio::Error error = hwio::makeError(row.code);
        EXPECT_EQ(hwio::idOf(row.code), row.id);
        EXPECT_EQ(hwio::categoryOf(row.code), row.category) << row.id;
        EXPECT_EQ(error.wizard, row.wizard) << row.id;
        EXPECT_FALSE(error.message.empty());
        EXPECT_FALSE(error.cause.empty());
        EXPECT_FALSE(error.suggestion.empty());
        EXPECT_EQ(error.message.find('%'), std::string::npos) << row.id;
    }
}

// Plan 0021 (ADR-0165, ADR-0166): a downloaded file whose SHA-256 differs from the pinned / listed
// one; mapped to the firmware exit code.
TEST(MakeError, ChecksumMismatchCode) {
    EXPECT_EQ(hwio::idOf(ErrorCode::FirmwareChecksumMismatch), "firmware.checksum_mismatch");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::FirmwareChecksumMismatch), ErrorCategory::Firmware);
    EXPECT_EQ(hwio::errorCodeFromId("firmware.checksum_mismatch"),
              ErrorCode::FirmwareChecksumMismatch);
    const hwio::Error error = hwio::makeError(ErrorCode::FirmwareChecksumMismatch);
    EXPECT_EQ(error.message, "The downloaded firmware is damaged (checksum mismatch).");
    EXPECT_EQ(error.suggestion, "Try again; if it repeats, report it to HARDWARIO.");
    EXPECT_FALSE(error.cause.empty());
    EXPECT_EQ(error.wizard, std::nullopt);
}

// Plan 0016 (ADR-0116): the update check errors; network exit code. update.too_large uses the
// bad_response texts (an internal bound, the bound in `detail`).
TEST(MakeError, UpdateCodes) {
    EXPECT_EQ(hwio::idOf(ErrorCode::UpdateNetwork), "update.network");
    EXPECT_EQ(hwio::idOf(ErrorCode::UpdateRateLimited), "update.rate_limited");
    EXPECT_EQ(hwio::idOf(ErrorCode::UpdateBadResponse), "update.bad_response");
    EXPECT_EQ(hwio::idOf(ErrorCode::UpdateTooLarge), "update.too_large");
    for (const ErrorCode code : {ErrorCode::UpdateNetwork, ErrorCode::UpdateRateLimited,
                                 ErrorCode::UpdateBadResponse, ErrorCode::UpdateTooLarge}) {
        EXPECT_EQ(hwio::categoryOf(code), ErrorCategory::Network);
        const hwio::Error error = hwio::makeError(code);
        EXPECT_FALSE(error.cause.empty());
        EXPECT_EQ(error.wizard, std::nullopt);
    }
    EXPECT_EQ(hwio::makeError(ErrorCode::UpdateNetwork).message,
              "Could not reach GitHub to check for updates.");
    EXPECT_EQ(hwio::makeError(ErrorCode::UpdateRateLimited).suggestion, "Try again in an hour.");
    const hwio::Error tooLarge = hwio::makeError(ErrorCode::UpdateTooLarge);
    const hwio::Error badResponse = hwio::makeError(ErrorCode::UpdateBadResponse);
    EXPECT_EQ(tooLarge.message, badResponse.message);
    EXPECT_EQ(tooLarge.suggestion, badResponse.suggestion);
}

// Plan 0025: the new codes of the security fixes, with what / why / what to do.
TEST(Error, SecurityFixCodes) {
    EXPECT_EQ(hwio::idOf(ErrorCode::McpToFileNotAllowed), "mcp.to_file_not_allowed");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::McpToFileNotAllowed), ErrorCategory::Usage);
    EXPECT_EQ(hwio::idOf(ErrorCode::CliFileIsSymlink), "cli.file_is_symlink");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::CliFileIsSymlink), ErrorCategory::Io);
    for (const ErrorCode code : {ErrorCode::McpToFileNotAllowed, ErrorCode::CliFileIsSymlink}) {
        const hwio::Error error = hwio::makeError(code);
        EXPECT_FALSE(error.message.empty());
        EXPECT_FALSE(error.cause.empty());
        EXPECT_FALSE(error.suggestion.empty());
    }
    EXPECT_EQ(hwio::makeError(ErrorCode::CliFileIsSymlink).message,
              "The output file is a symbolic link.");
}

TEST(MakeError, OutputClosedIsCancelledWithItsOwnMessage) {
    const hwio::Error error = hwio::outputClosedError("the standard output was closed");
    const hwio::Error cancelled = hwio::makeError(ErrorCode::Cancelled);
    EXPECT_EQ(error.code, ErrorCode::Cancelled);
    EXPECT_EQ(error.message, "The output was closed.");
    EXPECT_EQ(error.cause, cancelled.cause);
    EXPECT_EQ(error.suggestion, cancelled.suggestion);
    EXPECT_EQ(error.detail, "the standard output was closed");
}

// Plan 0051 (ADR-0383): target.low_voltage and the guided suggestion of the LTE update.
TEST(MakeError, LowVoltageAndGuidedSuggestion) {
    EXPECT_EQ(hwio::idOf(ErrorCode::TargetLowVoltage), "target.low_voltage");
    EXPECT_EQ(hwio::categoryOf(ErrorCode::TargetLowVoltage), ErrorCategory::Target);
    const hwio::Error low = hwio::makeError(ErrorCode::TargetLowVoltage, "LOW_VOLTAGE");
    EXPECT_EQ(low.message, "The LTE MCU reports too low supply voltage.");
    EXPECT_EQ(low.cause, "The CHESTER is not powered enough (battery, power supply) or the J-Link "
                         "cable does not connect the supply reference.");
    EXPECT_EQ(low.suggestion, "Check the power supply or batteries of the CHESTER and the J-Link "
                              "cable on the connector, then try again.");
    const std::string guided = "Check the power supply of the CHESTER and the J-Link cable on the "
                               "LTE connector, then click Try again.";
    for (const ErrorCode code :
         {ErrorCode::TargetLowVoltage, ErrorCode::FirmwareModemUpdateFailed}) {
        const hwio::Error plain = hwio::makeError(code, "detail");
        const hwio::Error error = hwio::withGuidedLteSuggestion(plain);
        EXPECT_EQ(error.suggestion, guided);
        EXPECT_EQ(error.message, plain.message);
        EXPECT_EQ(error.cause, plain.cause);
        EXPECT_EQ(error.detail, "detail");
        EXPECT_EQ(error.suggestion.find("guided"), std::string::npos);
    }
    const hwio::Error other = hwio::makeError(ErrorCode::TargetProgramFailed);
    EXPECT_EQ(hwio::withGuidedLteSuggestion(other), other);
}

TEST(Fail, FullFormKeepsAllTexts) {
    const hwio::Status status =
        hwio::fail(ErrorCode::IoReadFailed, "message", "cause", "suggestion", "detail");
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::IoReadFailed);
    EXPECT_EQ(status.error().message, "message");
    EXPECT_EQ(status.error().cause, "cause");
    EXPECT_EQ(status.error().suggestion, "suggestion");
    EXPECT_EQ(status.error().detail, "detail");
}

TEST(Fail, DefaultTextsForm) {
    const hwio::Result<int> result = hwio::fail(ErrorCode::Timeout);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::Timeout);
    EXPECT_EQ(result.error().message, hwio::makeError(ErrorCode::Timeout).message);
    EXPECT_TRUE(result.error().detail.empty());
}

} // namespace

// ---- English texts (plan 0050, ADR-0374)
// ---------------------------------------------------------

namespace {

// A pseudo-translation: "[xx] <source>" for every text.
class PseudoTranslator final : public QTranslator {
public:
    [[nodiscard]] QString translate(const char* /*context*/, const char* sourceText,
                                    const char* /*disambiguation*/ = nullptr,
                                    int /*n*/ = -1) const override {
        return QStringLiteral("[xx] ") + QString::fromUtf8(sourceText);
    }
    [[nodiscard]] bool isEmpty() const override { return false; }
};

class ScopedPseudoTranslator {
public:
    ScopedPseudoTranslator() : m_installed(QCoreApplication::installTranslator(&m_translator)) {}
    ~ScopedPseudoTranslator() {
        if (m_installed) {
            QCoreApplication::removeTranslator(&m_translator);
        }
    }
    ScopedPseudoTranslator(const ScopedPseudoTranslator&) = delete;
    ScopedPseudoTranslator& operator=(const ScopedPseudoTranslator&) = delete;
    ScopedPseudoTranslator(ScopedPseudoTranslator&&) = delete;
    ScopedPseudoTranslator& operator=(ScopedPseudoTranslator&&) = delete;

    [[nodiscard]] bool ok() const { return m_installed; }

private:
    PseudoTranslator m_translator;
    bool m_installed = false;
};

// Every default error has its English texts, equal to the translation without a translator and
// different from it with one.
TEST(ErrorEnglish, MakeErrorFillsTheEnglishTexts) {
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        const hwio::Error english = hwio::makeError(info.code);
        EXPECT_FALSE(english.messageEn.empty()) << info.id;
        EXPECT_FALSE(english.causeEn.empty()) << info.id;
        EXPECT_FALSE(english.suggestionEn.empty()) << info.id;
        EXPECT_EQ(english.message, english.messageEn) << info.id;
        EXPECT_EQ(english.cause, english.causeEn) << info.id;
        EXPECT_EQ(english.suggestion, english.suggestionEn) << info.id;
    }
    const ScopedPseudoTranslator translator;
    ASSERT_TRUE(translator.ok());
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        const hwio::Error error = hwio::makeError(info.code);
        EXPECT_EQ(error.message, "[xx] " + error.messageEn) << info.id;
        EXPECT_EQ(error.cause, "[xx] " + error.causeEn) << info.id;
        EXPECT_EQ(error.suggestion, "[xx] " + error.suggestionEn) << info.id;
    }
    const hwio::Error closed = hwio::outputClosedError("stdout");
    EXPECT_EQ(closed.messageEn, "The output was closed.");
    EXPECT_EQ(closed.message, "[xx] The output was closed.");
}

TEST(ErrorEnglish, SetTextKeepsBothLanguagesWithTheSameArguments) {
    const ScopedPseudoTranslator translator;
    ASSERT_TRUE(translator.ok());
    hwio::Error error = hwio::makeError(ErrorCode::TargetRttBufferNotFound);
    hwio::setText(error, hwio::ErrorText::Message, "hwio::test",
                  QT_TRANSLATE_NOOP("hwio::test", "RTT buffer \"%1\" not found (%2 of %3)."),
                  {QStringLiteral("Terminal"), 2, std::uint64_t{3}});
    EXPECT_EQ(error.message, "[xx] RTT buffer \"Terminal\" not found (2 of 3).");
    EXPECT_EQ(error.messageEn, "RTT buffer \"Terminal\" not found (2 of 3).");
    // A translated argument: its English counterpart in the English text.
    hwio::setText(error, hwio::ErrorText::Cause, "hwio::test",
                  QT_TRANSLATE_NOOP("hwio::test", "Reported: %1"),
                  {hwio::textOf(error, hwio::ErrorText::Message)});
    EXPECT_EQ(error.cause, "[xx] Reported: [xx] RTT buffer \"Terminal\" not found (2 of 3).");
    EXPECT_EQ(error.causeEn, "Reported: RTT buffer \"Terminal\" not found (2 of 3).");
    // One pass: a value that contains "%2" is not expanded again (review R13).
    hwio::setText(error, hwio::ErrorText::Suggestion, "hwio::test",
                  QT_TRANSLATE_NOOP("hwio::test", "%1 and %2"),
                  {QStringLiteral("100%2"), QStringLiteral("x")});
    EXPECT_EQ(error.suggestionEn, "100%2 and x");
    hwio::clearText(error, hwio::ErrorText::Suggestion);
    EXPECT_TRUE(error.suggestion.empty());
    EXPECT_TRUE(error.suggestionEn.empty());
    hwio::setUntranslatedText(error, hwio::ErrorText::Suggestion, "Plain English.");
    EXPECT_EQ(error.suggestion, "Plain English.");
    EXPECT_EQ(error.suggestionEn, "Plain English.");
}

TEST(ErrorEnglish, JsonInEnglish) {
    const ScopedPseudoTranslator translator;
    ASSERT_TRUE(translator.ok());
    const hwio::Error error = hwio::makeError(ErrorCode::TargetNotHalted);
    const QJsonObject english = hwio::toJson(error, hwio::ErrorLanguage::English);
    EXPECT_EQ(english.value(QStringLiteral("message")).toString().toStdString(), error.messageEn);
    EXPECT_EQ(hwio::toJson(error).value(QStringLiteral("message")).toString().toStdString(),
              error.message);
    // An error without English texts (aggregate-initialized) falls back to the translated ones.
    hwio::Error bare;
    bare.message = "only";
    EXPECT_EQ(hwio::toJson(bare, hwio::ErrorLanguage::English)
                  .value(QStringLiteral("message"))
                  .toString(),
              QStringLiteral("only"));
}

} // namespace
