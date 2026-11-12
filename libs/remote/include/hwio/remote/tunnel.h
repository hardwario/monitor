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

#include "hwio/core/result.h"
#include "hwio/device/command_tree.h"

#include <QJsonObject>
#include <QJsonValue>

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

enum class OpKind : std::uint8_t { Flash, Erase, Recover, Reset, LteUpdate };
inline constexpr std::size_t maxOpKinds = 16; // entries of a hello's opKinds
enum class OpTarget : std::uint8_t { App, Lte };

[[nodiscard]] std::string_view kindId(OpKind kind) noexcept; // "flash", "erase", ..., "lteUpdate"
[[nodiscard]] std::string_view targetId(OpTarget target) noexcept; // "app", "lte"
[[nodiscard]] std::optional<OpKind> kindFromId(std::string_view id) noexcept;
[[nodiscard]] std::optional<OpTarget> targetFromId(std::string_view id) noexcept;

struct DeviceInfo {
    std::string name;      // "CHESTER-M"
    std::string serial;    // "2159018247"
    std::string transport; // "jlink", "ble"
    std::string firmware;  // "CHESTER-Clime v3.2.1"
};

// ---- host → joiners ----
struct HostHello {
    std::string app = std::string(appId);
    std::string version; // Monitor version
    DeviceInfo device;
    bool ops = false; // the host accepts operation requests
    // ADR-0363: the kinds it runs (empty when !ops). Decoded: unknown kinds are skipped; a hello
    // without the field gets the four kinds of ADR-0322 when ops is true.
    std::vector<OpKind> opKinds;
};
// The host runs `kind` (ops and opKinds).
[[nodiscard]] bool supports(const HostHello& hello, OpKind kind) noexcept;
struct OpAccepted {
    std::string id;
};
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
// ADR-0364: the host's command tree (never null when decoded).
struct HostCommands {
    std::shared_ptr<const device::CommandTree> tree;
};
using HostMessage =
    std::variant<HostHello, OpAccepted, OpDenied, OpProgress, OpResult, HostEnd, HostCommands>;

// ---- joiner → host ----
struct JoinerHello {
    std::string app = std::string(appId);
    std::string version;
};
struct OpRequest {
    std::string id;
    OpKind kind = OpKind::Reset;
    OpTarget target = OpTarget::App;       // not sent for lteUpdate (decoded as Lte)
    std::optional<std::string> firmwareId; // flash only: a catalog firmware ID (32 hex digits)
};
// ADR-0364: learn the subcommands of the command at `path` (1..maxCommandDepth names).
struct Learn {
    std::vector<std::string> path;
};
using JoinerMessage = std::variant<JoinerHello, OpRequest, Learn>;

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
