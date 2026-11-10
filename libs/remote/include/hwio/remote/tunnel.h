#pragma once

// TunnelCodec - the Monitor-only messages of a remote support session tunneled in the events the
// relay passes on (plan/features/0044 § Wire protocol, ADR-0336):
//   host → joiners   at    {event: "hwio", data: {v: 1, type: "hello"|"op-accepted"|"op-denied"|
//                                                  "op-progress"|"op-result", …}}
//   joiner → host    write {hwio: {v: 1, type: "hello"|"op-request", …}}   (a JSON object, never a
//                                                                          string)
// The web Terminal ignores an `at` whose event is not state / write / read; a web Terminal host
// would write a `write` object to the device as text, so a Monitor joiner sends tunnel objects only
// after the host's hello (RemoteConsoleSession).
//
// Decoding validates every field (version 1, known type, enum values, id and text lengths, the
// firmware ID format, `firmwareId` only and always for flash) and fails with remote.protocol_error.
// Text fields shown to the user are sanitised (control characters removed, length bounded). Pure
// functions, any thread.

#include "hwio/core/result.h"

#include <QJsonObject>
#include <QJsonValue>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace hwio::remote::tunnel {

inline constexpr int version = 1;
inline constexpr std::string_view appId = "hwio-monitor";
inline constexpr std::size_t maxIdBytes = 32;    // operation ids: [A-Za-z0-9_-]
inline constexpr std::size_t maxTextBytes = 128; // app, version, device fields, phase

enum class OpKind : std::uint8_t { Flash, Erase, Recover, Reset };
enum class OpTarget : std::uint8_t { App, Lte };

[[nodiscard]] std::string_view kindId(OpKind kind) noexcept;       // "flash", "erase", ...
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
};
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
using HostMessage = std::variant<HostHello, OpAccepted, OpDenied, OpProgress, OpResult>;

// ---- joiner → host ----
struct JoinerHello {
    std::string app = std::string(appId);
    std::string version;
};
struct OpRequest {
    std::string id;
    OpKind kind = OpKind::Reset;
    OpTarget target = OpTarget::App;
    std::optional<std::string> firmwareId; // flash only: a catalog firmware ID (32 hex digits)
};
using JoinerMessage = std::variant<JoinerHello, OpRequest>;

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

// True for a valid operation id.
[[nodiscard]] bool validId(std::string_view id) noexcept;

} // namespace hwio::remote::tunnel
