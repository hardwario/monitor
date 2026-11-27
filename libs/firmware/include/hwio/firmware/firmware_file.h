#pragma once

// Plan 0039 (ADR-0280 D5): one entry point for a user's firmware file - Intel HEX (.hex) or raw
// binary (.bin, placed at a start address). Used by the CLI, the GUI and MCP.

#include "hwio/core/result.h"
#include "hwio/firmware/firmware_image.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>

namespace hwio::firmware {

// A .bin file by its extension (case-insensitive); everything else is read as Intel HEX.
[[nodiscard]] bool isBinaryFile(const std::filesystem::path& path);

// Reads `path`: .bin at `binAddress` (firmware.bin_address_required when nullopt), otherwise
// readIntelHexFile(). A .hex ignores `binAddress`. Errors: those of readIntelHexFile(),
// FirmwareImage::fromBinary(), io.file_not_found, io.read_failed.
[[nodiscard]] Result<FirmwareImage> readFirmwareFile(const std::filesystem::path& path,
                                                     std::optional<std::uint32_t> binAddress,
                                                     const std::stop_token& stop = {});

} // namespace hwio::firmware
