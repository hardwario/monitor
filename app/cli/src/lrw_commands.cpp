#include "lrw_commands.h"

#include "cli_app.h"
#include "cli_log.h"
#include "confirm.h"
#include "flash_commands.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/lrw_update_procedure.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/designated_firmware.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/firmware/firmware_catalog.h"
#include "operation_runner.h"
#include "output.h"
#include "probe_options.h"
#include "progress_ui.h"
#include "stm32_commands.h"
#include "target_commands.h"
#include "terminal.h"

#include <QEventLoop>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtLogging>

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::cli {

namespace {

using Procedure = device::LrwUpdateProcedure;
using Step = Procedure::Step;

constexpr std::string_view completed = "Completed";

// The CLI is English only (ADR-0027).
constexpr std::string_view holdHint =
    "The LoRaWAN module answers only while the application MCU is stopped: connect the J-Link to "
    "the APP connector, run `hwio chester app reset --halt`, then move the cable to the LoRaWAN "
    "connector.";

Stm32CommandTarget lrwTarget() {
    return Stm32CommandTarget{.profile = &chesterProfile(), .targetId = "lrw"};
}

Error usage(std::string message, std::string detail) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = std::move(message);
    return error;
}

// A direct operation that did not reach the module: say how to hold the application MCU.
Result<CommandResult> withHoldHint(Result<CommandResult> result) {
    if (!result && (result.error().code == ErrorCode::TargetConnectFailed ||
                    result.error().code == ErrorCode::TargetNotConnected)) {
        Error error = std::move(result).error();
        error.suggestion = std::string(holdHint);
        return std::unexpected(std::move(error));
    }
    return result;
}

// Guided commands wait for the user; scripts use --direct.
Status checkGuided(const Context& context, std::string_view command) {
    if (context.mode != OutputMode::Json && interactiveTerminal()) {
        return {};
    }
    Error error = usage(
        std::format("`hwio chester lrw {}` guides you through moving the J-Link cable and needs an "
                    "interactive terminal (not --json). Use --direct in scripts.",
                    command),
        "guided LoRaWAN module operation");
    error.suggestion =
        std::format("{} Then run `hwio chester lrw {} --direct`.", holdHint, command);
    return std::unexpected(std::move(error));
}

// ADR-0278: the prepared firmware of the module, downloaded (or taken from the cache) and
// SHA-256 verified. The name and version are printed before the download.
Result<Stm32PreparedImage> preparedFirmware(const Context& context, ProgressUi& ui, bool noCache) {
    const auto designated = device::designatedFirmwareOf(chesterTarget("lrw"));
    if (!designated) {
        return std::unexpected(
            makeError(ErrorCode::InternalUnexpected, "the lrw target has no prepared firmware"));
    }
    ui.info(std::format("Prepared firmware: {} {}", designated->name, designated->version));
    firmware::FirmwareCache cache(firmware::FirmwareCache::defaultDirectory());
    auto catalog = chesterCatalog(&cache);
    auto operation = catalog->downloadDesignated(
        *designated, firmware::DownloadOptions{.noCache = noCache, .expectedSha256 = std::nullopt},
        [&ui](std::uint64_t done, std::uint64_t total) { ui.progress("downloading", done, total); },
        context.stop);
    auto downloaded = firmware::waitForDownload(*operation);
    ui.finish();
    if (!downloaded) {
        return std::unexpected(std::move(downloaded).error());
    }
    for (const Error& warning : downloaded->warnings) {
        ui.warning(warning);
    }
    if (!downloaded->image) {
        return std::unexpected(
            makeError(ErrorCode::FirmwareInvalidImage, "the prepared firmware is not a HEX file"));
    }
    ui.info(std::format("{} {} ({}, {} bytes, SHA-256 verified)", designated->name,
                        designated->version, downloaded->fromCache ? "cached" : "downloaded",
                        downloaded->fileBytes));
    return Stm32PreparedImage{.image = downloaded->image,
                              .name = std::string(designated->name),
                              .version = std::string(designated->version),
                              .cached = downloaded->fromCache};
}

// Drives one LrwUpdateProcedure in the terminal: its prompts and progress on stderr, the y/N
// question of the unlock, Ctrl+C (cancelled where the procedure allows it).
class LrwTerminal {
public:
    LrwTerminal(const Context& context, ProgressUi& ui, Procedure& procedure, bool unlockConfirmed)
        : m_context(context), m_ui(ui), m_procedure(procedure), m_unlockConfirmed(unlockConfirmed) {
        QObject::connect(&procedure, &Procedure::changed, &m_loop, [this] { onChanged(); });
        QObject::connect(&procedure, &Procedure::progressChanged, &m_loop,
                         [this] { m_ui.progress(m_procedure.progress()); });
        QObject::connect(&procedure, &Procedure::finished, &m_loop, &QEventLoop::quit);
        QObject::connect(&m_stopCheck, &QTimer::timeout, &m_loop, [this] { onStopCheck(); });
    }

    // Runs until the procedure ends (it was started by the caller).
    void run() {
        show();
        m_stopCheck.start(std::chrono::milliseconds(100));
        if (!ended()) {
            m_loop.exec();
        }
        m_stopCheck.stop();
        m_ui.finish();
    }

private:
    [[nodiscard]] bool ended() const {
        const Step step = m_procedure.step();
        return step == Step::Done || step == Step::Failed || step == Step::Cancelled;
    }

    void show() {
        const Step step = m_procedure.step();
        const std::string prompt = toStdString(m_procedure.promptText());
        if (step == Step::Failed || step == Step::Cancelled || step == Step::Done ||
            prompt.empty() || prompt == m_lastPrompt) {
            return;
        }
        m_ui.finish();
        m_lastPrompt = prompt;
        m_ui.info(prompt);
    }

    void onChanged() {
        show();
        if (m_procedure.step() == Step::NeedsUnlockConfirmation) {
            // Asked on the next turn of the event loop; nothing runs on the device meanwhile.
            QMetaObject::invokeMethod(
                &m_loop, [this] { askAboutTheUnlock(); }, Qt::QueuedConnection);
        }
    }

    void askAboutTheUnlock() {
        if (m_procedure.step() != Step::NeedsUnlockConfirmation) {
            return;
        }
        if (m_unlockConfirmed || askYesNo("Unlock the LoRaWAN module (it is erased)? [y/N] ")) {
            if (Status confirmed = m_procedure.confirmUnlock(); !confirmed) {
                m_ui.warning(confirmed.error());
            }
        } else {
            (void)m_procedure.cancel();
        }
    }

    void onStopCheck() {
        if (!m_context.stop.stop_requested()) {
            return;
        }
        if (!m_procedure.cancel() && !m_cancelRefusedShown) {
            m_cancelRefusedShown = true;
            m_ui.info("The LoRaWAN module is being written and cannot be interrupted; cancelling "
                      "afterwards.");
        }
    }

    const Context& m_context;
    ProgressUi& m_ui;
    Procedure& m_procedure;
    bool m_unlockConfirmed;
    QEventLoop m_loop;
    QTimer m_stopCheck;
    std::string m_lastPrompt;
    bool m_cancelRefusedShown = false;
};

std::string_view actionId(Procedure::Action action) {
    switch (action) {
    case Procedure::Action::Flash:
        return "flash";
    case Procedure::Action::Erase:
        return "erase";
    case Procedure::Action::Unlock:
        return "unlock";
    }
    return "flash";
}

// The command's result for the procedure's end.
Result<CommandResult> guidedResult(const Procedure& procedure, const QJsonObject& image) {
    const std::string stopped =
        "The application MCU is stopped: reset CHESTER (or run `hwio chester app reset` with the "
        "J-Link on the APP connector) to start it again.";
    if (procedure.step() == Step::Done) {
        QJsonObject data;
        data.insert(QStringLiteral("type"), QStringLiteral("result"));
        data.insert(QStringLiteral("profile"), QStringLiteral("chester"));
        data.insert(QStringLiteral("target"), QStringLiteral("lrw"));
        data.insert(QStringLiteral("operation"), toQString(actionId(procedure.request().action)));
        data.insert(QStringLiteral("guided"), true);
        data.insert(QStringLiteral("unlocked"), procedure.unlocked());
        if (procedure.module()) {
            data.insert(QStringLiteral("rdp"), procedure.module()->rdpLevel);
        }
        if (!image.isEmpty()) {
            data.insert(QStringLiteral("image"), image);
        }
        return CommandResult{
            .data = data,
            .humanText = std::format("{}: {}", completed, toStdString(procedure.promptText())),
            .streamed = false};
    }
    if (procedure.step() == Step::Cancelled) {
        Error error = makeError(ErrorCode::Cancelled, "guided LoRaWAN module operation");
        error.message = toStdString(procedure.promptText());
        if (procedure.applicationHeld()) {
            error.suggestion = stopped;
        }
        return std::unexpected(std::move(error));
    }
    Error error = procedure.error().value_or(makeError(
        ErrorCode::InternalUnexpected, "the guided LoRaWAN module operation ended unexpectedly"));
    if (procedure.applicationHeld()) {
        error.suggestion = error.suggestion.empty() ? stopped : error.suggestion + " " + stopped;
    }
    return std::unexpected(std::move(error));
}

// Runs the guided procedure on a session of the application MCU (ADR-0352).
Result<CommandResult> runGuided(const Context& context, const GlobalOptions& globals,
                                const ProbeOptions& group, const ProbeOptions& own, ProgressUi& ui,
                                Procedure::Request request, const QJsonObject& image,
                                bool unlockConfirmed) {
    auto runner = OperationRunner::open(context, globals, group, own, "app", ui);
    if (!runner) {
        return std::unexpected(std::move(runner).error());
    }
    Procedure procedure((*runner)->session(),
                        Procedure::Options{.pollInterval = std::chrono::seconds(1)});
    LrwTerminal terminal(context, ui, procedure, unlockConfirmed);
    if (Status started = procedure.start(std::move(request)); !started) {
        return std::unexpected(std::move(started).error());
    }
    terminal.run();
    return guidedResult(procedure, image);
}

struct FlashArguments {
    std::string file;
    std::string address;
    bool noCache = false;
    bool direct = false;
    bool halt = false;
};

Result<CommandResult> lrwFlash(const Context& context, const GlobalOptions& globals,
                               const ProbeOptions& group, const ProbeOptions& own,
                               const FlashArguments& arguments) {
    if (arguments.halt && !arguments.direct) {
        return std::unexpected(usage(
            "--halt needs --direct: the guided flash starts the application MCU at the end, which "
            "restarts the LoRaWAN module.",
            "--halt"));
    }
    if (!arguments.direct) {
        if (Status guided = checkGuided(context, "flash"); !guided) {
            return std::unexpected(std::move(guided).error());
        }
    }
    Stm32FlashOptions options{.file = arguments.file,
                              .address = std::nullopt,
                              .eraseAll = false,
                              .allowEeprom = false,
                              .halt = arguments.halt,
                              .yes = false,
                              .prepared = std::nullopt};
    if (!arguments.address.empty()) {
        auto parsed = parseAddress(arguments.address);
        if (!parsed) {
            return std::unexpected(std::move(parsed).error());
        }
        options.address = *parsed;
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    if (arguments.file.empty()) {
        auto prepared = preparedFirmware(context, ui, arguments.noCache);
        if (!prepared) {
            return std::unexpected(std::move(prepared).error());
        }
        options.prepared = std::move(*prepared);
    } else if (arguments.noCache) {
        // Only the prepared firmware is cached; said, not an error (as `chester lte flash`).
        qCWarning(lcCli) << "--no-cache has no effect with a FILE";
        std::cerr << "Warning: --no-cache has no effect with a FILE (it applies to the prepared "
                     "lora-modem firmware only).\n";
    }
    if (arguments.direct) {
        return withHoldHint(stm32FlashFile(context, globals, group, own, lrwTarget(), options));
    }
    auto loaded = loadStm32Image(lrwTarget(), options);
    if (!loaded) {
        return std::unexpected(std::move(loaded).error());
    }
    if (!options.prepared) {
        ui.info(loaded->description);
    }
    const std::string name =
        options.prepared
            ? std::format("{} {}", options.prepared->name, options.prepared->version)
            : toUtf8(std::filesystem::path(toQString(arguments.file).toStdU16String()).filename());
    return runGuided(context, globals, group, own, ui,
                     Procedure::Request{.action = Procedure::Action::Flash,
                                        .image = loaded->image,
                                        .name = name,
                                        .options = {}},
                     loaded->json, false);
}

Result<CommandResult> lrwErase(const Context& context, const GlobalOptions& globals,
                               const ProbeOptions& group, const ProbeOptions& own, bool yes,
                               bool direct) {
    if (direct) {
        return withHoldHint(stm32Erase(context, globals, group, own, lrwTarget(), yes));
    }
    if (Status guided = checkGuided(context, "erase"); !guided) {
        return std::unexpected(std::move(guided).error());
    }
    if (Status confirmed = confirmDestructive(
            context, yes,
            "This erases the whole LoRaWAN module (flash and data EEPROM); its firmware is "
            "lost.\nContinue? [y/N] ");
        !confirmed) {
        return std::unexpected(std::move(confirmed).error());
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    // The unlock of a protected module erases it as well: confirmed with the erase.
    return runGuided(
        context, globals, group, own, ui,
        Procedure::Request{
            .action = Procedure::Action::Erase, .image = nullptr, .name = {}, .options = {}},
        {}, true);
}

Result<CommandResult> lrwUnlock(const Context& context, const GlobalOptions& globals,
                                const ProbeOptions& group, const ProbeOptions& own, bool yes,
                                bool direct) {
    if (direct) {
        return withHoldHint(stm32Unlock(context, globals, group, own, lrwTarget(), yes));
    }
    if (Status guided = checkGuided(context, "unlock"); !guided) {
        return std::unexpected(std::move(guided).error());
    }
    if (Status confirmed = confirmDestructive(
            context, yes,
            "Unlocking removes the read-out protection of the LoRaWAN module. The module then "
            "erases ALL its flash and data EEPROM; the firmware on it (e.g. the original Murata "
            "firmware) is lost.\nContinue? [y/N] ");
        !confirmed) {
        return std::unexpected(std::move(confirmed).error());
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    return runGuided(
        context, globals, group, own, ui,
        Procedure::Request{
            .action = Procedure::Action::Unlock, .image = nullptr, .name = {}, .options = {}},
        {}, true);
}

} // namespace

void registerLrwCommands(CLI::App& chester, CommandSlot& slot, const GlobalOptions& globals) {
    CLI::App* lrw = chester.add_subcommand(
        "lrw", "LoRaWAN module commands (Murata Type ABZ; it answers only while the application "
               "MCU is stopped)");
    lrw->require_subcommand(1);
    auto group = std::make_shared<ProbeOptions>();
    addProbeOptions(*lrw, *group);

    auto infoOptions = std::make_shared<ProbeOptions>();
    CLI::App* info = lrw->add_subcommand(
        "info", "Show DEV_ID, flash size and read-out protection of the module (read only)");
    addProbeOptions(*info, *infoOptions);
    info->callback([&slot, &globals, group, infoOptions] {
        slot.command = [&globals, group, infoOptions](const Context& context) {
            return withHoldHint(stm32Info(context, globals, *group, *infoOptions, lrwTarget()));
        };
    });

    struct FlashOptions {
        ProbeOptions probe;
        FlashArguments arguments;
    };
    auto flashOptions = std::make_shared<FlashOptions>();
    CLI::App* flash = lrw->add_subcommand(
        "flash", "Flash the prepared lora-modem firmware (downloaded and verified when no FILE is "
                 "given) or a .hex / .bin file, guided through the cable moves");
    flash
        ->add_option("file", flashOptions->arguments.file,
                     "Firmware (.hex, or .bin; default: the prepared lora-modem for CHESTER)")
        ->option_text("FILE");
    flash
        ->add_option("--address", flashOptions->arguments.address,
                     "Start address of a .bin file (default: the flash start, 0x08000000)")
        ->option_text("ADDR");
    flash->add_flag("--no-cache", flashOptions->arguments.noCache,
                    "Download the prepared firmware even when it is cached");
    flash->add_flag("--direct", flashOptions->arguments.direct,
                    "No guidance: the application MCU is already stopped and the J-Link is on the "
                    "LoRaWAN connector (scripts)");
    flash->add_flag("--halt", flashOptions->arguments.halt,
                    "Leave the module halted after flashing (with --direct)");
    addProbeOptions(*flash, flashOptions->probe);
    flash->callback([&slot, &globals, group, flashOptions] {
        slot.command = [&globals, group, flashOptions](const Context& context) {
            return lrwFlash(context, globals, *group, flashOptions->probe, flashOptions->arguments);
        };
    });

    struct YesOptions {
        ProbeOptions probe;
        bool yes = false;
        bool direct = false;
    };
    const auto addYes = [](CLI::App& command, YesOptions& options) {
        command.add_flag("--yes", options.yes, "Do not ask for confirmation");
        command.add_flag("--direct", options.direct,
                         "No guidance: the application MCU is already stopped and the J-Link is "
                         "on the LoRaWAN connector (scripts)");
        addProbeOptions(command, options.probe);
    };
    auto eraseOptions = std::make_shared<YesOptions>();
    CLI::App* eraseCommand = lrw->add_subcommand(
        "erase", "Erase the whole module (flash and data EEPROM), guided through the cable moves");
    addYes(*eraseCommand, *eraseOptions);
    eraseCommand->callback([&slot, &globals, group, eraseOptions] {
        slot.command = [&globals, group, eraseOptions](const Context& context) {
            return lrwErase(context, globals, *group, eraseOptions->probe, eraseOptions->yes,
                            eraseOptions->direct);
        };
    });

    auto unlockOptions = std::make_shared<YesOptions>();
    CLI::App* unlockCommand = lrw->add_subcommand(
        "unlock", "Remove read-out protection level 1 (the module erases flash and data EEPROM), "
                  "guided through the cable moves");
    addYes(*unlockCommand, *unlockOptions);
    unlockCommand->callback([&slot, &globals, group, unlockOptions] {
        slot.command = [&globals, group, unlockOptions](const Context& context) {
            return lrwUnlock(context, globals, *group, unlockOptions->probe, unlockOptions->yes,
                             unlockOptions->direct);
        };
    });

    struct ResetOptions {
        ProbeOptions probe;
        bool halt = false;
    };
    auto resetOptions = std::make_shared<ResetOptions>();
    CLI::App* resetCommand = lrw->add_subcommand("reset", "Reset the module");
    resetCommand->add_flag("--halt", resetOptions->halt, "Keep the module halted after the reset");
    addProbeOptions(*resetCommand, resetOptions->probe);
    resetCommand->callback([&slot, &globals, group, resetOptions] {
        slot.command = [&globals, group, resetOptions](const Context& context) {
            return withHoldHint(stm32Reset(context, globals, *group, resetOptions->probe,
                                           lrwTarget(), resetOptions->halt));
        };
    });
}

} // namespace hwio::cli
