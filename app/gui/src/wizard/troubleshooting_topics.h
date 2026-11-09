#pragma once

// The troubleshooting topics (plan/features/0009 with the 0035 amendments, § C; ADR-0067: the
// table is compiled in; ADR-0251: the texts are condensed from docs/en/troubleshooting.md
// "Common problems" and reviewed by HARDWARIO). A change of a troubleshooting fact is made in the
// docs page and here together.
//
// Each topic is a list of steps: automatic checks (environment checks of libs/diag run by the
// DiagWorker, or Diagnose steps on the tab's session), manual instructions (optionally with an
// action button) and hand-offs. The TroubleshootingWizard adds the error introduction in front and
// "Still not working?" at the end.
//
// Bounds: at most maxTopics topics, at most maxTopicSteps steps per topic (static_asserts in the
// .cpp).

#include "hwio/core/error.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hwio::gui {

enum class TopicStepKind : std::uint8_t {
    AutoEnvironment, // checkIds: environment check ids (diag::environmentCheckIds)
    AutoDiagnose,    // checkIds: Diagnose step ids shown (empty = all six; ADR-0296 stop point)
    Manual,          // an instruction with [Next]; `action` adds a button
    Commands,        // an instruction with command lines (CommandBlock); `action` adds buttons
    HandOff,         // an instruction whose button hands off (`action`)
};

// Manual / Commands actions.
inline constexpr std::string_view actionReset = "reset";            // session reset(target, Go)
inline constexpr std::string_view actionSpeed1000 = "speed-1000";   // ADR-0298
inline constexpr std::string_view actionProbeFirmware = "probe-fw"; // ADR-0291 pattern (0036)
inline constexpr std::string_view actionConnect = "connect-device"; // AutoEnvironment: when Ok
// HandOff actions.
inline constexpr std::string_view handOffLteUpdate = "lte-update";
inline constexpr std::string_view handOffFlashWizard = "flash-firmware"; // or the Flash page
inline constexpr std::string_view handOffRecover = "open-flash-page:recover";
// Illustrations from the device profile (ADR-0077).
inline constexpr std::string_view guideCable = "guide:cable";
inline constexpr std::string_view guidePower = "guide:power";

struct TopicStep {
    TopicStepKind kind = TopicStepKind::Manual;
    std::span<const std::string_view> checkIds;
    // QT_TRANSLATE_NOOP("Troubleshooting", …).
    const char* titleSourceText = ""; // short, for the step list
    const char* headingSourceText = "";
    const char* bodySourceText = "";   // "" = none; "guide:power" steps use the profile's text
    std::string_view illustrationSlot; // a slot, guideCable / guidePower or ""
    std::string_view action;           // see above; "" = none
    const char* actionSourceText = ""; // the button text
    bool learnMore = false;            // a "Learn more" link to the topic's docs section
};

struct Topic {
    std::string_view id;
    std::span<const ErrorCode> codes; // the codes that open it (empty: the topic list only)
    const char* titleSourceText = ""; // in the topic list
    std::span<const TopicStep> steps;
    std::string_view docsTopic; // hwio::DocsTopic kebab name ("trouble-no-probe") or ""
    bool listed = false;        // shown in "What is the problem?"
};

inline constexpr std::size_t maxTopics = 32;
inline constexpr std::size_t maxTopicSteps = 8;

// All topics; the listed ones in the order of the topic list (0035 § C).
[[nodiscard]] std::span<const Topic> troubleshootingTopics();
// The topic of an error code (nullptr when none).
[[nodiscard]] const Topic* topicFor(ErrorCode code);
[[nodiscard]] const Topic* topicById(std::string_view id);

} // namespace hwio::gui
