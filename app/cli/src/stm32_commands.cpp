#include "stm32_commands.h"

#include "cli_app.h"
#include "confirm.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_file.h"
#include "hwio/firmware/firmware_image.h"
#include "operation_runner.h"
#include "output.h"
#include "probe_options.h"
#include "progress_ui.h"
#include "target_commands.h"
#include "terminal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>

#include <CLI/CLI.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hwio::cli {

namespace {

constexpr std::string_view completed = "Completed";

Error usage(std::string message, std::string detail = {}) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = std::move(message);
    return error;
}

const device::DeviceProfile& stm32Profile() { return *device::ProfileRegistry::find("stm32"); }

const device::TargetProfile& targetOf(const Stm32CommandTarget& target) {
    return *device::findTarget(*target.profile, target.targetId);
}

// The `--chip` id of `hwio stm32` (the CLI is English only, ADR-0027).
Result<Stm32CommandTarget> chipTarget(const std::string& chip) {
    if (device::findTarget(stm32Profile(), chip) == nullptr) {
        Error error = usage(chip.empty() ? std::string("--chip is required.")
                                         : std::format("Unknown chip \"{}\".", chip),
                            chip);
        error.suggestion = "Run `hwio stm32 chips` for the chip ids.";
        return std::unexpected(std::move(error));
    }
    return Stm32CommandTarget{.profile = &stm32Profile(), .targetId = chip};
}

std::string hex(std::uint32_t value, int digits) { return std::format("0x{:0{}X}", value, digits); }

QJsonObject resultJson(const Stm32CommandTarget& target, const device::OperationResult& result) {
    QJsonObject data;
    data.insert(QStringLiteral("type"), QStringLiteral("result"));
    data.insert(QStringLiteral("profile"), toQString(target.profile->id));
    data.insert(QStringLiteral("target"), toQString(target.targetId));
    data.insert(QStringLiteral("operation"), toQString(device::operationKindId(result.kind)));
    data.insert(QStringLiteral("part"), result.chip && !result.chip->name.empty()
                                            ? QJsonValue(toQString(result.chip->name))
                                            : QJsonValue(QJsonValue::Null));
    if (result.chip && result.chip->devId != 0) {
        data.insert(QStringLiteral("rdp"), result.chip->rdpLevel);
    }
    data.insert(QStringLiteral("durationMs"), static_cast<double>(result.duration.count()));
    return data;
}

Result<CommandResult> chips() {
    QJsonArray list;
    std::vector<std::vector<std::string>> rows;
    for (const device::TargetProfile& target : stm32Profile().targets) {
        const device::ChipPart& part = target.parts.front();
        QJsonObject chip;
        chip.insert(QStringLiteral("id"), toQString(part.id));
        chip.insert(QStringLiteral("name"), toQString(part.displayName));
        chip.insert(QStringLiteral("family"), toQString(target.stm32->family));
        chip.insert(QStringLiteral("devId"), part.stm32->devId);
        chip.insert(QStringLiteral("flashKib"), part.stm32->flashSizeKib);
        chip.insert(QStringLiteral("dataEepromBytes"),
                    target.dataEeprom ? QJsonValue(static_cast<double>(target.dataEeprom->size()))
                                      : QJsonValue(QJsonValue::Null));
        chip.insert(QStringLiteral("jlinkDevice"), toQString(part.jlinkDevice));
        chip.insert(QStringLiteral("hint"), QString::fromUtf8(part.hintSourceText));
        list.append(chip);
        rows.push_back(
            {std::string(part.id), std::string(part.displayName), hex(part.stm32->devId, 3),
             target.dataEeprom ? std::format("{} B", target.dataEeprom->size()) : std::string("-"),
             std::string(part.hintSourceText)});
    }
    QJsonObject data;
    data.insert(QStringLiteral("chips"), list);
    return CommandResult{
        .data = data,
        .humanText = formatTable({"Chip id", "Name", "DEV_ID", "Data EEPROM", "Covers"}, rows),
        .streamed = false};
}

// Runs one operation on `target` (a flash-style session of its profile).
Result<OperationOutcome> run(const Context& context, const GlobalOptions& globals,
                             const ProbeOptions& group, const ProbeOptions& own,
                             const Stm32CommandTarget& target, ProgressUi& ui,
                             const StartFn& start) {
    return runOperation(context, globals, group, own, target.targetId, ui, start, std::nullopt,
                        target.profile);
}

// The unlock hint of `target` in the CLI's words.
std::string unlockCommand(const Stm32CommandTarget& target) {
    return target.profile->id == "stm32"
               ? std::format("hwio stm32 unlock --chip {}", target.targetId)
               : std::format("hwio {} {} unlock", target.profile->id, target.targetId);
}

} // namespace

Result<CommandResult> stm32Info(const Context& context, const GlobalOptions& globals,
                                const ProbeOptions& group, const ProbeOptions& own,
                                const Stm32CommandTarget& target) {
    ProgressUi ui(context, std::cerr, stderrIsTerminal(), true);
    auto outcome =
        run(context, globals, group, own, target, ui, [&](device::DeviceSession& session) {
            return session.identifyTarget(target.targetId);
        });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    const device::OperationResult& result = outcome->result;
    if (!result.chip || result.chip->devId == 0) {
        Error error = makeError(ErrorCode::TargetConnectFailed, "no STM32 chip answered");
        error.message = "No STM32 chip answered.";
        error.suggestion = "Check the cable, the power of the board and the selected chip.";
        return std::unexpected(std::move(error));
    }
    const jlink::ChipInfo& chip = *result.chip;
    const device::ChipPart* found = device::findStm32PartByChip(chip.devId, chip.flashSizeKib);
    const bool matches =
        device::findStm32Part(targetOf(target), chip.devId, chip.flashSizeKib) != nullptr;
    QJsonObject data;
    data.insert(QStringLiteral("selected"), toQString(target.targetId));
    data.insert(QStringLiteral("matches"), matches);
    data.insert(QStringLiteral("chip"),
                found != nullptr ? QJsonValue(toQString(found->id)) : QJsonValue(QJsonValue::Null));
    data.insert(QStringLiteral("name"), found != nullptr ? QJsonValue(toQString(found->displayName))
                                                         : QJsonValue(QJsonValue::Null));
    data.insert(QStringLiteral("family"), toQString(chip.family));
    data.insert(QStringLiteral("devId"), chip.devId);
    data.insert(QStringLiteral("revId"), chip.revId);
    data.insert(QStringLiteral("flashKib"), chip.flashSizeKib != 0 ? QJsonValue(chip.flashSizeKib)
                                                                   : QJsonValue(QJsonValue::Null));
    data.insert(QStringLiteral("rdp"), chip.rdpLevel);
    std::string text;
    text += std::format("Chip:                {}\n",
                        found != nullptr ? std::format("{} ({})", found->displayName, found->id)
                                         : std::string("unknown"));
    text += std::format("DEV_ID / REV_ID:     {} / {}\n", hex(chip.devId, 3), hex(chip.revId, 4));
    text += std::format("Flash size:          {}\n", chip.flashSizeKib != 0
                                                         ? std::format("{} KiB", chip.flashSizeKib)
                                                         : std::string("not readable"));
    // The unlock hint names the chip that is really connected.
    Stm32CommandTarget unlockTarget = target;
    if (!matches && found != nullptr && target.profile->id == "stm32") {
        unlockTarget.targetId = std::string(found->id);
    }
    text += std::format("Read-out protection: level {}{}\n", chip.rdpLevel,
                        chip.rdpLevel == 1   ? std::format(" (unlock: `{}`, erases the chip)",
                                                           unlockCommand(unlockTarget))
                        : chip.rdpLevel == 2 ? std::string(" (permanent)")
                                             : std::string());
    text += std::format("Selected chip:       {} ({})", target.targetId,
                        matches ? "matches" : "does NOT match the connected chip");
    return CommandResult{.data = data, .humanText = text, .streamed = false};
}

Result<CommandResult> stm32FlashFile(const Context& context, const GlobalOptions& globals,
                                     const ProbeOptions& group, const ProbeOptions& own,
                                     const Stm32CommandTarget& target,
                                     const Stm32FlashOptions& options) {
    const device::TargetProfile& profileTarget = targetOf(target);
    const std::filesystem::path path(toQString(options.file).toStdU16String());
    const bool binary = firmware::isBinaryFile(path);
    // ADR-0280 D5: a .bin starts at the part's flash start unless --address says otherwise.
    const std::uint32_t address =
        options.address.value_or(static_cast<std::uint32_t>(profileTarget.flash.start));
    auto image = firmware::readFirmwareFile(path, address);
    if (!image) {
        return std::unexpected(std::move(image).error());
    }
    // Checked before the session opens (ADR-0275; the worker checks again).
    for (const firmware::Segment& segment : image->segments()) {
        if (Status allowed = device::checkFlashWrite(profileTarget, segment.address,
                                                     segment.data.size(), options.allowEeprom);
            !allowed) {
            Error error = std::move(allowed).error();
            if (error.message.find("data EEPROM of") != std::string::npos) {
                error.suggestion = "Add --allow-eeprom if the file is meant to change it.";
            }
            return std::unexpected(std::move(error));
        }
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    ui.info(std::format("Firmware {} ({} bytes{})", toUtf8(path), image->totalBytes(),
                        binary ? std::format(" at {}", hex(address, 8)) : std::string()));
    if (options.eraseAll) {
        const std::string question = std::format(
            "This erases the whole {} (flash and data EEPROM) before flashing.\nContinue? [y/N] ",
            profileTarget.parts.front().displayName);
        if (Status confirmed = confirmDestructive(context, options.yes, question); !confirmed) {
            return std::unexpected(std::move(confirmed).error());
        }
    }
    auto shared = std::make_shared<const firmware::FirmwareImage>(std::move(*image));
    const device::FlashOptions flash{
        .halt = options.halt, .allowUicr = options.allowEeprom, .eraseAll = options.eraseAll};
    auto outcome =
        run(context, globals, group, own, target, ui, [&](device::DeviceSession& session) {
            return session.flash(target.targetId, shared, flash);
        });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    QJsonObject data = resultJson(target, outcome->result);
    QJsonObject imageJson;
    imageJson.insert(QStringLiteral("source"), QStringLiteral("file"));
    imageJson.insert(QStringLiteral("path"), toQString(toUtf8(path)));
    imageJson.insert(QStringLiteral("bytes"), static_cast<double>(shared->totalBytes()));
    imageJson.insert(QStringLiteral("sha256"), toQString(shared->sha256()));
    if (binary) {
        imageJson.insert(QStringLiteral("address"), toQString(hex(address, 8)));
    }
    data.insert(QStringLiteral("image"), imageJson);
    data.insert(QStringLiteral("erasedAll"), options.eraseAll);
    data.insert(QStringLiteral("halted"), options.halt);
    return CommandResult{.data = data, .humanText = std::string(completed), .streamed = false};
}

Result<CommandResult> stm32Erase(const Context& context, const GlobalOptions& globals,
                                 const ProbeOptions& group, const ProbeOptions& own,
                                 const Stm32CommandTarget& target, bool yes) {
    const std::string question =
        std::format("This erases the whole {} (flash and data EEPROM).\nContinue? [y/N] ",
                    targetOf(target).parts.front().displayName);
    if (Status confirmed = confirmDestructive(context, yes, question); !confirmed) {
        return std::unexpected(std::move(confirmed).error());
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    auto outcome =
        run(context, globals, group, own, target, ui, [&](device::DeviceSession& session) {
            return session.erase(target.targetId, device::EraseScope::All);
        });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    QJsonObject data = resultJson(target, outcome->result);
    data.insert(QStringLiteral("scope"), QStringLiteral("all"));
    return CommandResult{.data = data, .humanText = std::string(completed), .streamed = false};
}

Result<CommandResult> stm32Unlock(const Context& context, const GlobalOptions& globals,
                                  const ProbeOptions& group, const ProbeOptions& own,
                                  const Stm32CommandTarget& target, bool yes) {
    const std::string question = std::format(
        "Unlocking removes the read-out protection of the {}. The chip then erases ALL its flash "
        "and data EEPROM; the firmware on it is lost.\nContinue? [y/N] ",
        targetOf(target).parts.front().displayName);
    if (Status confirmed = confirmDestructive(context, yes, question); !confirmed) {
        return std::unexpected(std::move(confirmed).error());
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    ui.info("Unlocking (can take up to 60 s)...");
    auto outcome =
        run(context, globals, group, own, target, ui,
            [&](device::DeviceSession& session) { return session.unlock(target.targetId); });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    return CommandResult{.data = resultJson(target, outcome->result),
                         .humanText = std::string(completed),
                         .streamed = false};
}

Result<CommandResult> stm32Reset(const Context& context, const GlobalOptions& globals,
                                 const ProbeOptions& group, const ProbeOptions& own,
                                 const Stm32CommandTarget& target, bool halt) {
    ProgressUi ui(context, std::cerr, stderrIsTerminal(), true);
    const device::ResetMode mode = halt ? device::ResetMode::Halt : device::ResetMode::Go;
    auto outcome =
        run(context, globals, group, own, target, ui,
            [&](device::DeviceSession& session) { return session.reset(target.targetId, mode); });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    QJsonObject data = resultJson(target, outcome->result);
    data.insert(QStringLiteral("halted"), halt);
    return CommandResult{.data = data, .humanText = std::string(completed), .streamed = false};
}

void registerStm32Commands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals) {
    CLI::App* stm32 = app.add_subcommand(
        "stm32", "STM32 chips over J-Link (curated STM32L0 parts; see `hwio stm32 chips`)");
    stm32->require_subcommand(1);
    auto group = std::make_shared<ProbeOptions>();
    addProbeOptions(*stm32, *group);

    CLI::App* chipsCommand = stm32->add_subcommand("chips", "List the chips that can be chosen");
    chipsCommand->callback([&slot] { slot.command = [](const Context&) { return chips(); }; });

    struct ChipOptions {
        ProbeOptions probe;
        std::string chip;
        bool yes = false;
        bool halt = false;
    };
    const auto addChip = [](CLI::App& command, ChipOptions& options) {
        command.add_option("--chip", options.chip, "Chip id (`hwio stm32 chips`)")
            ->required()
            ->option_text("ID");
        addProbeOptions(command, options.probe);
    };
    // Resolves --chip and runs `body` on the chip's target.
    const auto withChip = [](const ChipOptions& options,
                             const auto& body) -> Result<CommandResult> {
        auto target = chipTarget(options.chip);
        if (!target) {
            return std::unexpected(std::move(target).error());
        }
        return body(*target);
    };

    auto infoOptions = std::make_shared<ChipOptions>();
    CLI::App* info = stm32->add_subcommand(
        "info", "Show DEV_ID, flash size and read-out protection of the connected chip (read "
                "only)");
    addChip(*info, *infoOptions);
    info->callback([&slot, &globals, group, infoOptions, withChip] {
        slot.command = [&globals, group, infoOptions, withChip](const Context& context) {
            return withChip(*infoOptions, [&](const Stm32CommandTarget& target) {
                return stm32Info(context, globals, *group, infoOptions->probe, target);
            });
        };
    });

    struct FlashCommandOptions {
        ChipOptions chip;
        std::string file;
        std::string address;
        bool eraseAll = false;
        bool allowEeprom = false;
    };
    auto flashOptions = std::make_shared<FlashCommandOptions>();
    CLI::App* flash = stm32->add_subcommand("flash", "Flash a .hex or .bin file and verify it");
    addChip(*flash, flashOptions->chip);
    flash->add_option("file", flashOptions->file, "Firmware (.hex, or .bin with --address)")
        ->required()
        ->option_text("FILE");
    flash
        ->add_option("--address", flashOptions->address,
                     "Start address of a .bin file (default: the flash start, 0x08000000)")
        ->option_text("ADDR");
    flash->add_flag("--erase-all", flashOptions->eraseAll,
                    "Erase the whole chip (flash and data EEPROM) first");
    flash->add_flag("--allow-eeprom", flashOptions->allowEeprom,
                    "Also program the file's data EEPROM records");
    flash->add_flag("--halt", flashOptions->chip.halt, "Leave the CPU halted after flashing");
    flash->add_flag("--yes", flashOptions->chip.yes, "Do not ask before --erase-all");
    flash->callback([&slot, &globals, group, flashOptions, withChip] {
        slot.command = [&globals, group, flashOptions, withChip](const Context& context) {
            return withChip(flashOptions->chip,
                            [&](const Stm32CommandTarget& target) -> Result<CommandResult> {
                                std::optional<std::uint32_t> address;
                                if (!flashOptions->address.empty()) {
                                    auto parsed = parseAddress(flashOptions->address);
                                    if (!parsed) {
                                        return std::unexpected(std::move(parsed).error());
                                    }
                                    address = *parsed;
                                }
                                return stm32FlashFile(
                                    context, globals, *group, flashOptions->chip.probe, target,
                                    Stm32FlashOptions{.file = flashOptions->file,
                                                      .address = address,
                                                      .eraseAll = flashOptions->eraseAll,
                                                      .allowEeprom = flashOptions->allowEeprom,
                                                      .halt = flashOptions->chip.halt,
                                                      .yes = flashOptions->chip.yes});
                            });
        };
    });

    auto eraseOptions = std::make_shared<ChipOptions>();
    CLI::App* eraseCommand =
        stm32->add_subcommand("erase", "Erase the whole chip (flash and data EEPROM)");
    addChip(*eraseCommand, *eraseOptions);
    eraseCommand->add_flag("--yes", eraseOptions->yes, "Do not ask for confirmation");
    eraseCommand->callback([&slot, &globals, group, eraseOptions, withChip] {
        slot.command = [&globals, group, eraseOptions, withChip](const Context& context) {
            return withChip(*eraseOptions, [&](const Stm32CommandTarget& target) {
                return stm32Erase(context, globals, *group, eraseOptions->probe, target,
                                  eraseOptions->yes);
            });
        };
    });

    auto unlockOptions = std::make_shared<ChipOptions>();
    CLI::App* unlockCommand = stm32->add_subcommand(
        "unlock", "Remove read-out protection level 1 (the chip erases flash and data EEPROM)");
    addChip(*unlockCommand, *unlockOptions);
    unlockCommand->add_flag("--yes", unlockOptions->yes, "Do not ask for confirmation");
    unlockCommand->callback([&slot, &globals, group, unlockOptions, withChip] {
        slot.command = [&globals, group, unlockOptions, withChip](const Context& context) {
            return withChip(*unlockOptions, [&](const Stm32CommandTarget& target) {
                return stm32Unlock(context, globals, *group, unlockOptions->probe, target,
                                   unlockOptions->yes);
            });
        };
    });

    auto resetOptions = std::make_shared<ChipOptions>();
    CLI::App* resetCommand = stm32->add_subcommand("reset", "Reset the chip");
    addChip(*resetCommand, *resetOptions);
    resetCommand->add_flag("--halt", resetOptions->halt, "Keep the CPU halted after the reset");
    resetCommand->callback([&slot, &globals, group, resetOptions, withChip] {
        slot.command = [&globals, group, resetOptions, withChip](const Context& context) {
            return withChip(*resetOptions, [&](const Stm32CommandTarget& target) {
                return stm32Reset(context, globals, *group, resetOptions->probe, target,
                                  resetOptions->halt);
            });
        };
    });
}

} // namespace hwio::cli
