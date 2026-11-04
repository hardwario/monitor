#pragma once

// ApplicationBackup - the copy of the application MCU's code flash that the guided LTE firmware
// update keeps before it erases the application (plan/features/0033-rc2-feedback.md, D3,
// ADR-0234).
//
// The range is the target's code flash from the profile (CHESTER app: 1 MiB); a target whose flash
// is larger than maxBytes is refused. LteUpdateProcedure reads it with DeviceSession::readMemory in
// chunks of maxReadMemoryBytes; ApplicationBackup only decides the range, trims the trailing erased
// (0xFF) pages, writes the Intel HEX file and keeps the newest keepFiles files of its directory:
//   <directory>/<serial>-<UTC yyyyMMdd-HHmmss>.hex   (owner-only, ADR-0186; Windows: the per-user
//                                                    AppData ACL)
// Default directory: <AppLocalDataLocation>/backups (requires initApplicationIdentity()).
//
// Thread safety: free functions without shared state; file access on the calling thread.

#include "hwio/core/result.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_image.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::device {

class ApplicationBackup {
public:
    // Plan 0033 (D3): the largest code flash a backup reads, and the files kept per directory.
    static constexpr std::size_t maxBytes = 4ULL * 1024 * 1024;
    static constexpr std::size_t keepFiles = 10;
    // Trailing pages that read all 0xFF are not stored (nRF52 / nRF91 flash page).
    static constexpr std::size_t pageBytes = 4096;

    // The code flash of `target`. Errors: usage.invalid_argument (empty, or above maxBytes).
    [[nodiscard]] static Result<firmware::AddressRange> rangeOf(const TargetProfile& target);

    // `data` without its trailing erased pages (whole pages of 0xFF, counted from the start of
    // the range). Empty when everything is erased.
    [[nodiscard]] static std::vector<std::byte> trimmed(std::vector<std::byte> data);

    // Writes `data` (read from `base`; already trimmed, not empty) as Intel HEX into `directory`
    // (created when missing), then removes the oldest backups beyond keepFiles. `serial` is the
    // device's serial number (characters other than [A-Za-z0-9_-] become '_'; "" = "unknown").
    // Errors: usage.invalid_argument (empty data), io.write_failed.
    [[nodiscard]] static Result<std::filesystem::path>
    save(const std::filesystem::path& directory, std::string_view serial,
         std::chrono::system_clock::time_point now, std::uint32_t base,
         std::span<const std::byte> data);

    [[nodiscard]] static std::filesystem::path defaultDirectory();
};

} // namespace hwio::device
