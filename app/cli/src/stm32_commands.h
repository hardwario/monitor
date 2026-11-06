#pragma once

// STM32 commands (plan/features/0039, ADR-0277):
//   hwio stm32 chips [--json]
//   hwio stm32 info   --chip ID
//   hwio stm32 flash  --chip ID FILE [--address ADDR] [--erase-all [--yes]] [--allow-eeprom]
//                     [--halt]
//   hwio stm32 erase  --chip ID [--yes]
//   hwio stm32 unlock --chip ID [--yes]
//   hwio stm32 reset  --chip ID [--halt]
// Thin over the same backend as the GUI (CLAUDE.md): one flash-style session per command
// (operation_runner.h). The operations are also used by `hwio chester lrw` (plan 0040) with the
// CHESTER profile and its STM32 target.

#include "cli_app.h"
#include "hwio/core/result.h"
#include "hwio/device/profile.h"
#include "probe_options.h"

#include <CLI/App.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::cli {

void registerStm32Commands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

// The STM32 target an operation runs on: a profile (`stm32` or `chester`) and its target id;
// `commandHint` is the command prefix shown in hints ("hwio stm32 unlock --chip X").
struct Stm32CommandTarget {
    const device::DeviceProfile* profile = nullptr;
    std::string targetId;
};

[[nodiscard]] Result<CommandResult> stm32Info(const Context& context, const GlobalOptions& globals,
                                              const ProbeOptions& group, const ProbeOptions& own,
                                              const Stm32CommandTarget& target);
struct Stm32FlashOptions {
    std::string file;                     // "" only with a prepared firmware (chester lrw)
    std::optional<std::uint32_t> address; // .bin start (default: the target's flash start)
    bool eraseAll = false;
    bool allowEeprom = false;
    bool halt = false;
    bool yes = false; // --erase-all without asking (ADR-0038)
};
[[nodiscard]] Result<CommandResult>
stm32FlashFile(const Context& context, const GlobalOptions& globals, const ProbeOptions& group,
               const ProbeOptions& own, const Stm32CommandTarget& target,
               const Stm32FlashOptions& options);
[[nodiscard]] Result<CommandResult> stm32Erase(const Context& context, const GlobalOptions& globals,
                                               const ProbeOptions& group, const ProbeOptions& own,
                                               const Stm32CommandTarget& target, bool yes);
[[nodiscard]] Result<CommandResult> stm32Unlock(const Context& context,
                                                const GlobalOptions& globals,
                                                const ProbeOptions& group, const ProbeOptions& own,
                                                const Stm32CommandTarget& target, bool yes);
[[nodiscard]] Result<CommandResult> stm32Reset(const Context& context, const GlobalOptions& globals,
                                               const ProbeOptions& group, const ProbeOptions& own,
                                               const Stm32CommandTarget& target, bool halt);

} // namespace hwio::cli
