// STM32 tools (plan/features/0039 B2, ADR-0280 D9): stm32_info, stm32_flash, stm32_erase,
// stm32_unlock on an open J-Link device whose target is an STM32 chip (the generic `stm32`
// profile, or a CHESTER STM32 target such as `lrw`, plan 0040). stm32_unlock needs `confirm: true`
// AND the user's permission (erase): it removes the read-out protection and the chip erases
// itself (ADR-0276).

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_file.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/limits.h"
#include "hwio/mcp/offload.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/schema.h"
#include "hwio/mcp/tool_call.h"
#include "hwio/mcp/tools/tools_context.h"
#include "tool_support.h"
#include "tools/tools.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::mcp::tools::detail {

namespace {

namespace fs = std::filesystem;
using hwio::device::DeviceSession;
using hwio::device::OperationResult;
using hwio::device::TargetProfile;
using hwio::mcp::Schema;

constexpr const char* infoDescription =
    "Identify the STM32 chip behind the probe (read only, no halt): DEV_ID, REV_ID, flash size, "
    "read-out protection level (rdp 0, 1 = protected, unlock possible; 2 = permanent) and whether "
    "it is the device's selected chip (`matches`; `chip` is the matching curated part id). J-Link "
    "only; for STM32 targets (the STM32 chip device, CHESTER `lrw`).";

constexpr const char* flashDescription =
    "Flash an STM32 chip: `path` is an absolute path to an Intel HEX (.hex) or raw binary (.bin) "
    "file; a .bin starts at `address` (default: the flash start 0x08000000). The connected chip "
    "must be the device's selected chip (else target.wrong_chip) and not read-out protected "
    "(else target.protected: see stm32_unlock). erase_all erases the whole chip (flash and data "
    "EEPROM) first; allow_eeprom lets the file write the data EEPROM. Programmed, verified and "
    "reset. Needs the user's permission (flash; erase with erase_all). J-Link only.";

constexpr const char* eraseDescription =
    "Erase the whole STM32 chip (flash and data EEPROM; the firmware is gone). Needs the user's "
    "permission (erase). J-Link only.";

constexpr const char* unlockDescription =
    "Remove read-out protection level 1 of an STM32 chip. The chip then ERASES its whole flash "
    "and data EEPROM; the firmware on it is lost and cannot be read before. Level 2 cannot be "
    "removed (target.permanently_protected). Requires confirm=true AND the user's permission "
    "(erase); takes up to 60 s. J-Link only.";

constexpr OperationSettings programSettings(std::string_view tool) {
    return OperationSettings{
        .tool = tool, .marker = true, .busy = true, .cursors = true, .progress = true};
}

// The tools work only on STM32 targets.
hwio::Status requireStm32(const TargetProfile& target) {
    if (target.vendor == hwio::device::ChipVendor::Stm32) {
        return {};
    }
    return std::unexpected(
        invalidArgument(QStringLiteral("The target %1 is not an STM32 chip.").arg(qs(target.id)),
                        std::string(target.id)));
}

QJsonObject chipJson(const TargetProfile& target, const OperationResult& result) {
    QJsonObject json;
    json.insert(QStringLiteral("target"), qs(target.id));
    json.insert(QStringLiteral("duration"), seconds(result.duration));
    if (result.chip && result.chip->devId != 0) {
        json.insert(QStringLiteral("rdp"), result.chip->rdpLevel);
    }
    return json;
}

// stm32_flash: reads the file on an offloaded worker (ADR-0162), asks consent, flashes.
class Stm32FlashCall final : public OperationCall {
public:
    Stm32FlashCall(ToolsContext& tools, const hwio::mcp::RequestContext& context,
                   const hwio::mcp::ToolEnv& env, ResolvedDevice device,
                   const TargetProfile& target, const QJsonObject& args,
                   std::optional<std::uint32_t> address)
        : OperationCall(tools, context, env, std::move(device), programSettings("stm32_flash")),
          m_target(&target), m_path(args.value(QStringLiteral("path")).toString()),
          m_eraseAll(args.value(QStringLiteral("erase_all")).toBool()),
          m_allowEeprom(args.value(QStringLiteral("allow_eeprom")).toBool()),
          m_address(address.value_or(static_cast<std::uint32_t>(target.flash.start))) {
        auto file = inputFile(m_path, limits().imageFileMaxBytes);
        if (!file) {
            completeError(std::move(file).error());
            return;
        }
        m_binary = hwio::firmware::isBinaryFile(*file);
        using Read = hwio::Result<hwio::firmware::FirmwareImage>;
        auto job = env.offloader().submit<Read>(
            *this,
            [path = *file, at = m_address](const std::stop_token& stop) -> Read {
                return hwio::firmware::readFirmwareFile(path, at, stop);
            },
            [this](Read image) {
                if (!image) {
                    completeError(std::move(image).error());
                    return;
                }
                proceed(std::make_shared<const hwio::firmware::FirmwareImage>(std::move(*image)));
            });
        if (!job) {
            completeError(std::move(job).error()); // mcp.too_many_requests
            return;
        }
        m_read = std::move(*job);
    }

protected:
    void cancelPending() override { m_read.cancel(); }

private:
    void proceed(std::shared_ptr<const hwio::firmware::FirmwareImage> image) {
        if (isFinished()) {
            return;
        }
        // ADR-0275: checked before the consent (the worker checks again).
        for (const hwio::firmware::Segment& segment : image->segments()) {
            if (hwio::Status allowed = hwio::device::checkFlashWrite(
                    *m_target, segment.address, segment.data.size(), m_allowEeprom);
                !allowed) {
                completeError(std::move(allowed).error());
                return;
            }
        }
        m_image = std::move(image);
        hwio::mcp::ConsentCategories consent = hwio::mcp::ConsentCategory::Flash;
        if (m_eraseAll) {
            consent |= hwio::mcp::ConsentCategory::Erase;
        }
        QJsonObject details;
        details.insert(QStringLiteral("file"), m_path);
        details.insert(QStringLiteral("target"), qs(m_target->id));
        details.insert(QStringLiteral("size"), static_cast<qint64>(m_image->totalBytes()));
        details.insert(QStringLiteral("sha256"), qs(m_image->sha256()));
        details.insert(QStringLiteral("erase_all"), m_eraseAll);
        details.insert(QStringLiteral("eeprom"), m_allowEeprom);
        withConsent(consent, details, [this] { program(); });
    }

    void program() {
        const QString name =
            QString::fromStdU16String(fs::path(m_path.toStdU16String()).filename().u16string());
        if (!begin(QStringLiteral("stm32_flash %1 (%2)").arg(name, qs(m_target->id)))) {
            return;
        }
        const std::string target(m_target->id);
        const hwio::device::FlashOptions options{
            .halt = false, .allowUicr = m_allowEeprom, .eraseAll = m_eraseAll};
        run([this, target,
             options](DeviceSession& session) { return session.flash(target, m_image, options); },
            [this](const OperationResult& result) {
                QJsonObject json = chipJson(*m_target, result);
                json.insert(QStringLiteral("status"), QStringLiteral("ok"));
                json.insert(QStringLiteral("file"), m_path);
                json.insert(QStringLiteral("chip"), result.chip && !result.chip->name.empty()
                                                        ? QJsonValue(qs(result.chip->name))
                                                        : QJsonValue(QJsonValue::Null));
                if (m_binary) {
                    json.insert(QStringLiteral("address"), qs(std::format("0x{:08X}", m_address)));
                }
                json.insert(QStringLiteral("verified"), true);
                succeed(std::move(json), result.duration);
            });
    }

    const TargetProfile* m_target; // non-owning, static profile data
    QString m_path;
    bool m_eraseAll;
    bool m_allowEeprom;
    bool m_binary = false;
    std::uint32_t m_address;
    hwio::mcp::Offloader::Handle m_read;
    std::shared_ptr<const hwio::firmware::FirmwareImage> m_image;
};

// "0x08000000" or a non-negative integer; nullopt for anything else.
std::optional<std::uint32_t> addressOf(const QJsonValue& value) {
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (number >= 0 && number <= 0xFFFFFFFF &&
            number == static_cast<double>(value.toInteger())) {
            return static_cast<std::uint32_t>(value.toInteger());
        }
        return std::nullopt;
    }
    bool ok = false;
    const QString text = value.toString();
    const qulonglong parsed = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                                  ? text.mid(2).toULongLong(&ok, 16)
                                  : text.toULongLong(&ok, 10);
    if (!ok || parsed > 0xFFFFFFFFULL) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
}

hwio::Status addInfo(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "stm32_info",
            .title = "Identify the STM32 chip",
            .description = infoDescription,
            .inputSchema = Schema::object()
                               .property(QStringLiteral("device"), deviceArgument())
                               .property(QStringLiteral("target"), targetArgument())
                               .json(),
            .outputSchema = objectOf({{"target", typed("string")},
                                      {"chip", nullable("string")},
                                      {"matches", typed("boolean")},
                                      {"dev_id", typed("integer")},
                                      {"rev_id", typed("integer")},
                                      {"flash_kib", nullable("integer")},
                                      {"rdp", typed("integer")}},
                                     {QStringLiteral("target"), QStringLiteral("rdp")}),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start = operationStart(
                context,
                [](const QJsonObject&, const ResolvedDevice&, const TargetProfile& target,
                   const hwio::mcp::ToolLimits&) -> hwio::Result<SimpleOperation> {
                    if (hwio::Status stm32 = requireStm32(target); !stm32) {
                        return std::unexpected(std::move(stm32).error());
                    }
                    const std::string id(target.id);
                    const TargetProfile* chosen = &target;
                    return SimpleOperation{
                        .settings = readSettings("stm32_info"),
                        .consent = {},
                        .details = {},
                        .action = QStringLiteral("stm32_info %1").arg(qs(id)),
                        .start = [id](DeviceSession& s) { return s.identifyTarget(id); },
                        .result = [chosen,
                                   id](const OperationResult& r) -> hwio::Result<QJsonObject> {
                            if (!r.chip || r.chip->devId == 0) {
                                hwio::Error error =
                                    makeError(ErrorCode::TargetConnectFailed, "no STM32 chip");
                                error.message = "No STM32 chip answered.";
                                return std::unexpected(std::move(error));
                            }
                            const auto& chip = *r.chip;
                            const auto* found =
                                hwio::device::findStm32PartByChip(chip.devId, chip.flashSizeKib);
                            QJsonObject json;
                            json.insert(QStringLiteral("target"), qs(id));
                            json.insert(QStringLiteral("chip"), found != nullptr
                                                                    ? QJsonValue(qs(found->id))
                                                                    : QJsonValue(QJsonValue::Null));
                            json.insert(QStringLiteral("matches"),
                                        hwio::device::findStm32Part(*chosen, chip.devId,
                                                                    chip.flashSizeKib) != nullptr);
                            json.insert(QStringLiteral("dev_id"), chip.devId);
                            json.insert(QStringLiteral("rev_id"), chip.revId);
                            json.insert(QStringLiteral("flash_kib"),
                                        chip.flashSizeKib != 0 ? QJsonValue(chip.flashSizeKib)
                                                               : QJsonValue(QJsonValue::Null));
                            json.insert(QStringLiteral("rdp"), chip.rdpLevel);
                            return json;
                        }};
                }),
            .validate = {}});
}

hwio::Status addFlash(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "stm32_flash",
            .title = "Flash an STM32 chip",
            .description = flashDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(QStringLiteral("target"), targetArgument())
                    .property(QStringLiteral("path"),
                              Schema::string(QStringLiteral("Absolute path to a .hex or .bin "
                                                            "file."))
                                  .minLength(1)
                                  .maxLength(1024),
                              hwio::mcp::Required::Yes)
                    .property(QStringLiteral("address"),
                              Schema::string(QStringLiteral("Start address of a .bin file, e.g. "
                                                            "\"0x08000000\" (default: the flash "
                                                            "start)."))
                                  .pattern(QStringLiteral("^(0[xX][0-9a-fA-F]{1,8}|[0-9]{1,10})$")))
                    .property(QStringLiteral("erase_all"),
                              Schema::boolean(QStringLiteral("Erase the whole chip (flash and "
                                                             "data EEPROM) first."))
                                  .defaultValue(false))
                    .property(QStringLiteral("allow_eeprom"),
                              Schema::boolean(QStringLiteral("Let the file write the data "
                                                             "EEPROM."))
                                  .defaultValue(false))
                    .json(),
            .outputSchema = objectOf({{"status", typed("string")},
                                      {"file", typed("string")},
                                      {"target", typed("string")},
                                      {"address", typed("string")},
                                      {"chip", nullable("string")},
                                      {"rdp", typed("integer")},
                                      {"duration", typed("number")},
                                      {"verified", typed("boolean")},
                                      {"log_cursor_end", typed("integer")},
                                      {"terminal_cursor_end", typed("integer")}},
                                     {QStringLiteral("status"), QStringLiteral("target")}),
            .annotations = destructive,
            .consent = hwio::mcp::ConsentCategory::Flash,
            .marker = true,
            .start = [&context](const hwio::mcp::RequestContext& rc, const QJsonObject& args,
                                hwio::mcp::ToolEnv& env) -> std::unique_ptr<hwio::mcp::ToolCall> {
                auto device = resolveJLinkDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                auto target = resolveTarget(*device->ref.console, args);
                if (!target) {
                    return failed(std::move(target).error());
                }
                if (hwio::Status stm32 = requireStm32(**target); !stm32) {
                    return failed(std::move(stm32).error());
                }
                std::optional<std::uint32_t> address;
                if (args.contains(QStringLiteral("address"))) {
                    address = addressOf(args.value(QStringLiteral("address")));
                    if (!address) {
                        return failed(invalidArgument(
                            QStringLiteral("Invalid address."),
                            args.value(QStringLiteral("address")).toString().toStdString()));
                    }
                }
                return std::make_unique<Stm32FlashCall>(context, rc, env, std::move(*device),
                                                        **target, args, address);
            },
            .validate = {}});
}

hwio::Status addErase(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "stm32_erase",
            .title = "Erase an STM32 chip",
            .description = eraseDescription,
            .inputSchema = Schema::object()
                               .property(QStringLiteral("device"), deviceArgument())
                               .property(QStringLiteral("target"), targetArgument())
                               .json(),
            .outputSchema = objectOf({{"target", typed("string")},
                                      {"duration", typed("number")},
                                      {"rdp", typed("integer")},
                                      {"log_cursor_end", typed("integer")},
                                      {"terminal_cursor_end", typed("integer")}},
                                     {QStringLiteral("target"), QStringLiteral("duration")}),
            .annotations = destructive,
            .consent = hwio::mcp::ConsentCategory::Erase,
            .marker = true,
            .start = operationStart(
                context,
                [](const QJsonObject&, const ResolvedDevice&, const TargetProfile& target,
                   const hwio::mcp::ToolLimits&) -> hwio::Result<SimpleOperation> {
                    if (hwio::Status stm32 = requireStm32(target); !stm32) {
                        return std::unexpected(std::move(stm32).error());
                    }
                    const std::string id(target.id);
                    const TargetProfile* chosen = &target;
                    QJsonObject details;
                    details.insert(QStringLiteral("target"), qs(id));
                    details.insert(QStringLiteral("scope"), QStringLiteral("all"));
                    return SimpleOperation{
                        .settings = programSettings("stm32_erase"),
                        .consent = hwio::mcp::ConsentCategory::Erase,
                        .details = details,
                        .action = QStringLiteral("stm32_erase %1").arg(qs(id)),
                        .start =
                            [id](DeviceSession& s) {
                                return s.erase(id, hwio::device::EraseScope::All);
                            },
                        .result = [chosen](const OperationResult& r) -> hwio::Result<QJsonObject> {
                            return chipJson(*chosen, r);
                        }};
                }),
            .validate = {}});
}

hwio::Status addUnlock(hwio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hwio::mcp::ToolSpec{
            .name = "stm32_unlock",
            .title = "Unlock a read-out protected STM32 chip",
            .description = unlockDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(QStringLiteral("target"), targetArgument())
                    .property(QStringLiteral("confirm"),
                              Schema::boolean(QStringLiteral("Must be true: the chip erases its "
                                                             "flash and data EEPROM.")),
                              hwio::mcp::Required::Yes)
                    .json(),
            .outputSchema = objectOf({{"target", typed("string")},
                                      {"duration", typed("number")},
                                      {"rdp", typed("integer")},
                                      {"log_cursor_end", typed("integer")},
                                      {"terminal_cursor_end", typed("integer")}},
                                     {QStringLiteral("target"), QStringLiteral("duration")}),
            .annotations = destructive,
            .consent = hwio::mcp::ConsentCategory::Erase,
            .marker = true,
            .start = operationStart(
                context,
                [](const QJsonObject&, const ResolvedDevice&, const TargetProfile& target,
                   const hwio::mcp::ToolLimits&) -> hwio::Result<SimpleOperation> {
                    if (hwio::Status stm32 = requireStm32(target); !stm32) {
                        return std::unexpected(std::move(stm32).error());
                    }
                    const std::string id(target.id);
                    const TargetProfile* chosen = &target;
                    QJsonObject details;
                    details.insert(QStringLiteral("target"), qs(id));
                    details.insert(QStringLiteral("erases"),
                                   QStringLiteral("flash and data EEPROM"));
                    return SimpleOperation{
                        .settings = programSettings("stm32_unlock"),
                        .consent = hwio::mcp::ConsentCategory::Erase,
                        .details = details,
                        .action = QStringLiteral("stm32_unlock %1").arg(qs(id)),
                        .start = [id](DeviceSession& s) { return s.unlock(id); },
                        .result = [chosen](const OperationResult& r) -> hwio::Result<QJsonObject> {
                            return chipJson(*chosen, r);
                        }};
                }),
            // ADR-0280 D9: an explicit confirm=true, in addition to the user's permission.
            .validate = [](const QJsonObject& args,
                           const hwio::mcp::Limits&) -> std::optional<hwio::mcp::ValidationError> {
                if (args.value(QStringLiteral("confirm")) != QJsonValue(true)) {
                    return invalid(QStringLiteral("confirm"),
                                   QStringLiteral("must be true: unlocking erases the chip"));
                }
                return std::nullopt;
            }});
}

} // namespace

hwio::Status registerStm32Tools(hwio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add : {&addInfo, &addFlash, &addErase, &addUnlock}) {
        if (auto added = add(registry, context); !added) {
            return added;
        }
    }
    return {};
}

} // namespace hwio::mcp::tools::detail
