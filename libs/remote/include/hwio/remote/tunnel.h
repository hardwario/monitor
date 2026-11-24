#pragma once

// TunnelCodec - the Monitor-only messages of a remote support session tunneled in the events the
// relay passes on (plan/features/0044 § Wire protocol, ADR-0336):
//   host → joiners   at    {event: "hwio", data: {v: 1, type: "hello"|"op-accepted"|"op-denied"|
//                                                  "op-progress"|"op-result"|"end", …}}
//   joiner → host    write {hwio: {v: 1, type: "hello"|"op-request", …}}   (a JSON object, never a
//                                                                          string)
// ADR-0363: the operation kind `lteUpdate` (the guided LTE update with the host's recommended
// firmware: no target, no firmware ID); the host's hello lists the kinds it runs in `opKinds`
// (alongside `ops`; a hello without it means the four kinds of ADR-0322 when ops is true). The
// progress of an lteUpdate names the guided update's step in `phase`
// (device::LteUpdateProcedure::stepId: "connect-app", "confirm" (ADR-0366: the customer's
// Continue), "backup", "erase", "connect-lte", "update-lte", "connect-app-again",
// "choose-application", "restore"; "download" before it).
// ADR-0368: support answers the questions of a remote LTE update. The host's hello says
// `opQuestions: true`; a joiner that can answer sends `questions: true` in its op-request. Then
// host → joiners `op-question` {id, qid, question: "continue" | "continueWithoutCopy"} and joiner →
// host `op-answer` {id, qid, yes}. The relay does not tell the host who sent a `write` (every
// participant sees every event), so the host accepts an answer from an allowed participant only,
// only for its open question (operation id and the host's random qid), and only once; the texts are
// the joiner's own (translated there), never sent. Without the feature on either side the customer
// answers (ADR-0366).
// ADR-0364: host → joiners `commands` {tree: <device::CommandTree JSON>} (the host's learned
// command tree for the joiner's autocomplete), joiner → host `learn` {path: [names]} (the host
// learns that command's subcommands with its silent query). The tree is untrusted input: at most
// maxCommandsBytes of JSON, maxCommandNodes nodes, maxCommandDepth levels, command names only
// (CommandTree's charset), help texts sanitised; then CommandTree's own bounds apply.
// The web Terminal ignores an `at` whose event is not state / write / read; a web Terminal host
// would write a `write` object to the device as text, so a Monitor joiner sends tunnel objects only
// after the host's hello (RemoteConsoleSession).
//
// Decoding validates every field (version 1, known type, enum values, id and text lengths, the
// firmware ID format, `firmwareId` only and always for flash, `target` always except for
// lteUpdate, which has none) and fails with remote.protocol_error.
// `end` (ADR-0354): the host ends the session; `code` is the hwio::Error id of the reason.
//
// Text fields shown to the user are sanitised (control characters removed, length bounded). Pure
// functions, any thread.
//
// Plan 0059 (ADR-0414): `v` stays 1; additions are optional fields that older peers ignore.
//   host hello    `caps` [names], `profile` (device profile id), `console` (false: operations
//                 only, ADR-0419), `targets` [{id, kinds, catalog, prepared: [{id, name,
//                 version}]}]; absent = an older host (CHESTER, `app` / `lte`, catalog on app)
//   joiner hello  `caps`
//   op-request    `target` is any profile target id ([a-z0-9-], at most maxNameBytes); flash
//                 names either `firmwareId` (catalog) or `prepared` (the id of a prepared
//                 firmware of the host's hello, ADR-0418). A joiner sends a target other than
//                 app / lte, or `prepared`, only to a host whose hello lists it
//                 (RemoteConsoleSession::sendTunnel); the host validates every request itself.
//   op-denied, op-result (in `error`), end: `code` is always an id of Monitor 2.0.0-rc.4
//                 (baselineErrorId); when the real id differs it follows as
//                 `detail: {code: <id>}`. Decoded: the detail's id when this build knows it, else
//                 `code`.
//   line-refused  {code} host → joiners: the host refused a remote command line (ADR-0416); sent
//                 only when the session's last joiner hello listed the `refused-lines` cap (an
//                 older joiner shows an unknown type as a warning).
// Names (caps, profile, target and prepared ids) are [a-z0-9-], 1..maxNameBytes bytes; lists are
// bounded (maxCaps, maxTargets, maxPrepared, maxOpKinds). Unknown caps and kinds are kept / skipped
// as data, never an error.

#include "hwio/core/result.h"
#include "hwio/device/command_tree.h"

#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hwio::remote::tunnel {

inline constexpr int version = 1;
inline constexpr std::string_view appId = "hwio-monitor";
inline constexpr std::size_t maxIdBytes = 32;    // operation ids: [A-Za-z0-9_-]
inline constexpr std::size_t maxTextBytes = 128; // app, version, device fields, phase
// ADR-0364: the `commands` tree and a `learn` path.
inline constexpr std::size_t maxCommandsBytes = 256ULL * 1024;
inline constexpr std::size_t maxCommandNodes = 4000;
inline constexpr std::size_t maxCommandDepth = 8;

// Plan 0059 (ADR-0414): caps, profile, target and prepared firmware ids.
inline constexpr std::size_t maxNameBytes = 32;
inline constexpr std::size_t maxCaps = 16;    // entries of a hello's caps
inline constexpr std::size_t maxTargets = 8;  // entries of a host hello's targets
inline constexpr std::size_t maxPrepared = 4; // prepared firmware per target
inline constexpr std::size_t maxErrorIdBytes = 64;

// The capabilities of this build (ADR-0414). Host: detail codes on its errors, line-refused.
// Joiner: understands targets, detail codes and line-refused.
namespace cap {
inline constexpr std::string_view targets = "targets";   // host hello lists `targets`
inline constexpr std::string_view prepared = "prepared"; // host accepts `prepared` flash
inline constexpr std::string_view detailCodes = "detail-codes";
inline constexpr std::string_view refusedLines = "refused-lines";
} // namespace cap

// A name of the protocol: [a-z0-9-], 1..maxNameBytes bytes.
[[nodiscard]] constexpr bool validName(std::string_view name) noexcept {
    return !name.empty() && name.size() <= maxNameBytes &&
           std::ranges::all_of(name, [](const char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
           });
}
[[nodiscard]] bool hasCap(const std::vector<std::string>& caps, std::string_view name) noexcept;

enum class OpKind : std::uint8_t { Flash, Erase, Recover, Reset, LteUpdate };
inline constexpr std::size_t maxOpKinds = 16; // entries of a hello's opKinds

// The target of an operation request: a profile target id (ADR-0414, D-16), a bounded value with
// the two CHESTER ids as constants.
class OpTarget {
public:
    static const OpTarget App; // "app"
    static const OpTarget Lte; // "lte"

    // nullopt unless validName(id).
    [[nodiscard]] static constexpr std::optional<OpTarget> fromId(std::string_view id) noexcept {
        if (!validName(id)) {
            return std::nullopt;
        }
        return OpTarget(id);
    }
    [[nodiscard]] constexpr std::string_view id() const noexcept {
        return {m_chars.data(), m_size};
    }
    // One of the two targets of a 2.0.0-rc.4 peer.
    [[nodiscard]] constexpr bool isChesterTarget() const noexcept {
        return id() == "app" || id() == "lte";
    }
    [[nodiscard]] constexpr bool operator==(const OpTarget& other) const noexcept {
        return id() == other.id();
    }

private:
    constexpr explicit OpTarget(std::string_view id) noexcept : m_size(id.size()) {
        std::ranges::copy(id, m_chars.begin());
    }

    std::array<char, maxNameBytes> m_chars{};
    std::size_t m_size = 0;
};
inline constexpr OpTarget OpTarget::App{"app"};
inline constexpr OpTarget OpTarget::Lte{"lte"};
// ADR-0368: the questions of a remote LTE update that support answers.
enum class OpQuestionKind : std::uint8_t {
    Continue,            // ADR-0366: the application will be erased; go on?
    ContinueWithoutCopy, // the application could not be copied; go on without a copy?
};

[[nodiscard]] std::string_view kindId(OpKind kind) noexcept; // "flash", "erase", ..., "lteUpdate"
[[nodiscard]] std::string_view targetId(const OpTarget& target) noexcept; // "app", "lte", ...
[[nodiscard]] std::optional<OpKind> kindFromId(std::string_view id) noexcept;
// Any valid target id (OpTarget::fromId).
[[nodiscard]] std::optional<OpTarget> targetFromId(std::string_view id) noexcept;
[[nodiscard]] std::string_view questionId(OpQuestionKind kind) noexcept; // "continue", ...
[[nodiscard]] std::optional<OpQuestionKind> questionFromId(std::string_view id) noexcept;

struct DeviceInfo {
    std::string name;      // "CHESTER-M"
    std::string serial;    // "2159018247"
    std::string transport; // "jlink", "ble"
    std::string firmware;  // "CHESTER-Clime v3.2.1"
};

// ---- host → joiners ----
// ADR-0418: a firmware the host has prepared for a target (no file, URL or digest crosses the
// relay; the host flashes its own pinned image).
struct PreparedFirmware {
    std::string id{};      // name: "release", "debug"
    std::string name{};    // "STICKER" (text)
    std::string version{}; // "2.0.0" (text)
};
// ADR-0414: what the host offers on one target.
struct HostTarget {
    std::string id{};                         // the profile target id (a name)
    std::vector<OpKind> kinds{};              // decoded: unknown kinds skipped, no duplicates
    bool catalog = false;                     // a catalog firmware ID is accepted for flash
    std::vector<PreparedFirmware> prepared{}; // at most maxPrepared
};
struct HostHello {
    std::string app = std::string(appId);
    std::string version; // Monitor version
    DeviceInfo device;
    bool ops = false; // the host accepts operation requests
    // ADR-0363: the kinds it runs (empty when !ops). Decoded: unknown kinds are skipped; a hello
    // without the field gets the four kinds of ADR-0322 when ops is true.
    std::vector<OpKind> opKinds;
    // ADR-0368: the host sends op-question to a joiner whose request says `questions`.
    bool opQuestions = false;
    // ---- plan 0059 (ADR-0414); all optional on the wire ----
    std::vector<std::string> caps{}; // names, at most maxCaps (sent when not empty)
    std::string profile{};           // device profile id; "" = not sent (an older host: CHESTER)
    bool console = true;             // false: an operations-only session (sent only when false)
    // nullopt = not sent (an older host: app / lte as in ADR-0322).
    std::optional<std::vector<HostTarget>> targets = std::nullopt;
};
// The hello's target `id`, nullptr when it lists none (or no targets at all).
[[nodiscard]] const HostTarget* findTarget(const HostHello& hello, std::string_view id) noexcept;
// The host runs `kind` (ops and opKinds).
[[nodiscard]] bool supports(const HostHello& hello, OpKind kind) noexcept;
struct OpAccepted {
    std::string id;
};
// ADR-0414: `code` is the real hwio::Error id; the codec sends the rc.4 id plus `detail` when they
// differ, and decodes the detail's id when it knows it.
struct OpDenied {
    std::string id;
    std::string code; // a hwio::Error id: "internal.busy", "remote.operation_denied", ...
};
struct OpProgress {
    std::string id;
    std::string phase; // ProgramPhase id ("download", "erasing", ...), "" = unknown
    int step = 1;
    int steps = 1;
    std::uint64_t done = 0;
    std::uint64_t total = 0; // 0 = indeterminate
};
struct OpResult {
    std::string id;
    bool ok = false;
    std::optional<std::string> code; // a hwio::Error id when !ok
};
// The host ends the session (ADR-0354): sent before it disconnects from the relay.
struct HostEnd {
    std::string code =
        "cancelled"; // a hwio::Error id: "cancelled" (End sharing), "remote.idle", ...
};
// ADR-0368: a question of the running operation for support (the joiner's text for `question`).
struct OpQuestion {
    std::string id;  // the operation
    std::string qid; // the question (random, the host's)
    OpQuestionKind question = OpQuestionKind::Continue;
};
// ADR-0364: the host's command tree (never null when decoded).
struct HostCommands {
    std::shared_ptr<const device::CommandTree> tree;
};
// Plan 0059 (ADR-0416): the host refused a remote command line (only to joiners with the
// refused-lines cap).
struct LineRefused {
    std::string code = "remote.command_refused"; // a hwio::Error id
};
using HostMessage = std::variant<HostHello, OpAccepted, OpDenied, OpProgress, OpResult, HostEnd,
                                 HostCommands, OpQuestion, LineRefused>;

// ---- joiner → host ----
struct JoinerHello {
    std::string app = std::string(appId);
    std::string version;
    std::vector<std::string> caps{}; // plan 0059: names, at most maxCaps (sent when not empty)
};
struct OpRequest {
    std::string id;
    OpKind kind = OpKind::Reset;
    OpTarget target = OpTarget::App;                      // not sent for lteUpdate (decoded as Lte)
    std::optional<std::string> firmwareId = std::nullopt; // flash: a catalog firmware ID, or
    bool questions = false; // ADR-0368: the joiner answers op-question (sent only when true)
    // flash (ADR-0418): the id of a prepared firmware of the host's hello. A flash names exactly
    // one of firmwareId and prepared.
    std::optional<std::string> prepared = std::nullopt;
};
// ADR-0364: learn the subcommands of the command at `path` (1..maxCommandDepth names).
struct Learn {
    std::vector<std::string> path;
};
// ADR-0368: support's answer to an op-question.
struct OpAnswer {
    std::string id;
    std::string qid;
    bool yes = false;
};
using JoinerMessage = std::variant<JoinerHello, OpRequest, Learn, OpAnswer>;

// The `at` payload {event: "hwio", data: …}.
[[nodiscard]] QJsonObject encodeHost(const HostMessage& message);
// The `write` payload {hwio: …}.
[[nodiscard]] QJsonObject encodeJoiner(const JoinerMessage& message);

// True when an `at` payload is a tunnel message (event "hwio").
[[nodiscard]] bool isTunnelAt(const QJsonValue& payload);
// Decodes the `at` payload of a tunnel message. Errors: remote.protocol_error.
[[nodiscard]] Result<HostMessage> decodeHost(const QJsonValue& payload);
// Decodes a `write` object. Errors: remote.protocol_error.
[[nodiscard]] Result<JoinerMessage> decodeJoiner(const QJsonValue& payload);

// The `commands` message of `tree` without help texts when `withHelp` is false (to fit the relay's
// event size, RemoteHost).
[[nodiscard]] QJsonObject encodeCommands(const device::CommandTree& tree, bool withHelp);

// True for a valid operation id.
[[nodiscard]] bool validId(std::string_view id) noexcept;

} // namespace hwio::remote::tunnel
