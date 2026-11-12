#pragma once

// CommandTreeSource - where a console's autocomplete gets the device command tree (plan 0021,
// ADR-0170): the CommandTreeLearner of a local console, or the host's tree sent through a remote
// support session (plan 0044, ADR-0364). Used by the CLI console (console_ui) on the owner thread.

#include "hwio/device/command_tree.h"

#include <memory>
#include <string>
#include <vector>

namespace hwio::device {

class CommandTreeSource {
public:
    CommandTreeSource() = default;
    CommandTreeSource(const CommandTreeSource&) = delete;
    CommandTreeSource& operator=(const CommandTreeSource&) = delete;
    CommandTreeSource(CommandTreeSource&&) = delete;
    CommandTreeSource& operator=(CommandTreeSource&&) = delete;
    virtual ~CommandTreeSource() = default;

    // An immutable snapshot (nullptr = none known yet).
    [[nodiscard]] virtual std::shared_ptr<const CommandTree> tree() const = 0;
    // The top level is needed (a later tree() has it, if it can be learned).
    virtual void requestTopLevel() = 0;
    // The subcommands of the command at `path` are needed.
    virtual void requestChildren(const std::vector<std::string>& path) = 0;
};

} // namespace hwio::device
