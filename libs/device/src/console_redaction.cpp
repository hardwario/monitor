#include "hwio/device/console_redaction.h"

#include "hwio/device/console_line.h"
#include "hwio/device/profile.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::device {

ConsoleRedaction::ConsoleRedaction(std::string_view profileId, std::string_view targetId)
    : m_sensitivePrefixes(sensitivePrefixesOf(profileId, targetId)),
      m_sensitiveOutput(sensitiveOutputKeysOf(profileId, targetId)) {
    // Review R4: the shell prompts of the profile (RTT target, BLE), stripped from echoed lines.
    if (const DeviceProfile* profile = ProfileRegistry::find(profileId)) {
        if (const TargetProfile* target = findTarget(*profile, targetId)) {
            m_prompts.at(0) = target->shellPrompt;
        }
        if (profile->ble) {
            m_prompts.at(1) = profile->ble->shellPrompt;
        }
    }
}

std::optional<std::string> ConsoleRedaction::redacted(const ConsoleLine& line) const {
    // ADR-0186: a command with secret arguments is written as "<prefix> <redacted>": the input
    // line and (review R4) a shell's echo of it in the output, after its prompt.
    if (line.dir != Direction::In &&
        (line.dir != Direction::Out || line.channel != Channel::Shell)) {
        return std::nullopt;
    }
    const std::size_t promptLength = line.dir == Direction::Out ? echoPromptLength(line.text) : 0;
    const std::string_view command = std::string_view(line.text).substr(promptLength);
    if (const auto prefix = matchSensitiveCommand(command, m_sensitivePrefixes)) {
        return line.text.substr(0, promptLength) + std::string(*prefix) + " <redacted>";
    }
    // Plan 0028 (U2): the value of a secret printed by the device ("claim token: <redacted>").
    if (line.dir == Direction::Out) {
        if (const auto key = matchSensitiveOutput(command, m_sensitiveOutput)) {
            return line.text.substr(0, promptLength) + *key + ": <redacted>";
        }
    }
    return std::nullopt;
}

std::size_t ConsoleRedaction::echoPromptLength(std::string_view text) const noexcept {
    for (const std::string_view prompt : m_prompts) {
        if (!prompt.empty() && text.starts_with(prompt)) {
            return prompt.size();
        }
    }
    // A Zephyr shell prompt "<name>:~$ " (CHESTER's RTT prompt is not in the profile yet).
    constexpr std::string_view zephyrPromptEnd = ":~$ ";
    const std::size_t end = text.find(zephyrPromptEnd);
    if (end != std::string_view::npos && end > 0 &&
        text.substr(0, end).find_first_of(" \t") == std::string_view::npos) {
        return end + zephyrPromptEnd.size();
    }
    return 0;
}

} // namespace hwio::device
