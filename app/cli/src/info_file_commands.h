#pragma once

// `hwio chester app info save [PATH]` and `hwio ble info save [PATH]` (plan/features/0063,
// ADR-0440): the shared part of both commands - where the file goes, the collection through
// device::collectDeviceInfo and the output. No logic of its own beyond the PATH rule; the
// collection, the format, the name and the secrets are libs/device (device_info_file.h).
//
// PATH omitted: the generated name (<serial>_<YYYY-MM-DD_HHMMSS>.txt) in the current directory;
// an existing directory: the generated name in it; anything else: exactly that file. An existing
// file is never replaced (io.file_exists, a symlink cli.file_is_symlink, both exit code 3).

#include "cli_app.h"
#include "hwio/core/result.h"

#include <filesystem>
#include <string>

namespace hwio::device {
class ConsoleSession;
class ShellQueryQueue;
} // namespace hwio::device

namespace hwio::cli {

struct InfoSaveOptions {
    std::string path; // "" = the current directory
    bool withSecrets = false;
};

// Checks PATH before the device is touched (an existing file or symlink fails at once).
[[nodiscard]] Status checkInfoSavePath(const InfoSaveOptions& options);

// Reads the device information from the connected `session` (silent queries through `queries`)
// and writes the file. JSON data: {"path", "serial" (null when unknown), "commands": [{"command",
// "answered", "lines"}]}; human text: the path and the commands without an answer.
[[nodiscard]] Result<CommandResult> saveDeviceInfo(const Context& context,
                                                   device::ConsoleSession& session,
                                                   device::ShellQueryQueue& queries,
                                                   const InfoSaveOptions& options);

} // namespace hwio::cli
