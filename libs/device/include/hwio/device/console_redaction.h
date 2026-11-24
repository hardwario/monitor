#pragma once

// ConsoleRedaction - the secrets of a console line as they are written to a file (ADR-0186 and its
// amendment U2 of plan 0028; ADR-0426, plan 0060 A4). One implementation for the console files
// (ConsoleFileSink) and "Save view…" of the GUI; the screen and Copy show the line unchanged.
//
// - A command with secret arguments (TargetProfile::sensitiveCommandPrefixes of the console's
//   profile and target) becomes "<prefix> <redacted>": the sent line (Direction::In) and (review
//   R4) a shell's echo of it in the shell output, after its prompt (the profile's RTT or BLE shell
//   prompt, or a Zephyr "<name>:~$ " prompt).
// - A shell output line "<key>: <value>" whose key is one of the target's sensitiveOutputKeys
//   (CHESTER `info show`: claim token, BLE passkey) becomes "<key>: <redacted>".
// - Plan 0059 (ADR-0416): a shell output line that starts with one of the target's
//   sensitiveOutputPrefixes (a secret printed without a "key:", STICKER `config secret-key <hex>`)
//   becomes "<prefix> <redacted>" (after the prompt, as above).
// An unknown profile or target redacts nothing. Thread safety: immutable after construction.

#include "hwio/device/console_line.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hwio::device {

struct DeviceProfile;

class ConsoleRedaction {
public:
    // The profile is looked up in the ProfileRegistry.
    ConsoleRedaction(std::string_view profileId, std::string_view targetId);
    // A given profile (static profile data, e.g. one that is not registered).
    ConsoleRedaction(const DeviceProfile& profile, std::string_view targetId);

    // The text of `line` with its secret replaced, nullopt when the line has none (write
    // line.text as it is).
    [[nodiscard]] std::optional<std::string> redacted(const ConsoleLine& line) const;

private:
    // nullptr = an unknown profile (redacts nothing).
    ConsoleRedaction(const DeviceProfile* profile, std::string_view targetId);

    // The length of a shell prompt at the start of an echoed line (0 = none).
    [[nodiscard]] std::size_t echoPromptLength(std::string_view text) const noexcept;

    // ADR-0186: input lines matching these are written redacted (the target's profile list).
    std::span<const std::string_view> m_sensitivePrefixes;
    // Plan 0028 (U2): output lines "<key>: <secret>" are written as "<key>: <redacted>".
    std::span<const std::string_view> m_sensitiveOutput;
    // Plan 0059 (ADR-0416): output lines "<prefix> <secret>" are written as "<prefix> <redacted>".
    std::span<const std::string_view> m_sensitiveOutputPrefixes;
    // Review R4: the profile's shell prompts (RTT target, BLE); "" = none.
    std::array<std::string_view, 2> m_prompts{};
};

} // namespace hwio::device
