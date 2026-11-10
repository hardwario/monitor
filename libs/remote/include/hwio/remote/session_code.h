#pragma once

// Session codes and relay URLs of a remote support session (plan/features/0044, ADR-0334,
// ADR-0330).
//
// A session is the Socket.IO namespace "/<code>" on the HARDWARIO Terminal relay. A Monitor host
// creates a 6-digit code 100000-999999 with the system CSPRNG (QRandomGenerator::system()). A web
// Terminal host creates Math.floor(Math.random() * 1e6), so a joiner accepts 1 to 6 digits; the
// web joiner joins Number(code), so leading zeros are dropped ("012345" is namespace "/12345") and
// 0 is not a session (the web Terminal treats it as no session). The link is
// <relay>/?session=<code>.
//
// The relay URL (setting remote/relayUrl, CLI --relay) must be https (ADR-0329, ADR-0330). Test
// builds (HWIO_TEST_OVERRIDES, plan 0044 implementation note) also accept http for a loopback host,
// for the fake relay of the tests.

#include "hwio/core/result.h"
#include "hwio/core/settings.h"

#include <QString>
#include <QUrl>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::remote {

// The default relay (ADR-0330).
inline constexpr std::string_view defaultRelayUrl = "https://terminal.hardwario.com";
// Settings → Advanced (ADR-0330); empty = defaultRelayUrl.
inline constexpr SettingKey<std::string> relayUrlSetting{.path = "remote/relayUrl",
                                                         .defaultValue = {}};

inline constexpr std::uint32_t minHostCode = 100000;
inline constexpr std::uint32_t maxCode = 999999;

class SessionCode {
public:
    // A fresh host code 100000-999999 from QRandomGenerator::system().
    [[nodiscard]] static SessionCode generate();
    // 1 to 6 digits; spaces between the digits are allowed ("482 913"). Leading zeros are dropped
    // (web Terminal). Errors: remote.code_invalid (also for 0).
    [[nodiscard]] static Result<SessionCode> parse(std::string_view text);
    [[nodiscard]] static Result<SessionCode> fromValue(std::uint32_t value);

    [[nodiscard]] std::uint32_t value() const noexcept { return m_value; }
    // "482913" (the namespace without the slash).
    [[nodiscard]] std::string text() const;
    // "482 913" for reading aloud (6 digits as two groups of three; shorter codes unchanged).
    [[nodiscard]] std::string grouped() const;
    // "/482913".
    [[nodiscard]] QString socketNamespace() const;

    friend bool operator==(const SessionCode&, const SessionCode&) = default;

private:
    explicit SessionCode(std::uint32_t value) noexcept : m_value(value) {}
    std::uint32_t m_value = minHostCode;
};

// The relay URL: https (or, in test builds, http for 127.0.0.1 / ::1 / localhost), a host, no
// user info, query or fragment; a trailing slash is removed. Errors: usage.invalid_argument.
[[nodiscard]] Result<QUrl> parseRelayUrl(std::string_view text);
// The relay from the setting (an invalid setting falls back to the default, logged).
[[nodiscard]] QUrl configuredRelayUrl(const Settings* settings);
// The Engine.IO WebSocket endpoint of a relay: wss://<host>[:port]<path>/socket.io/?EIO=4&
// transport=websocket (ws:// for an http test relay).
[[nodiscard]] QUrl engineIoUrl(const QUrl& relay);
// <relay>/?session=<code>.
[[nodiscard]] QUrl sessionLink(const QUrl& relay, const SessionCode& code);

// What a joiner typed: a code, or a link with ?session=<code>. `relay` is the link's relay (scheme,
// host, port and path before the query) or nullopt for a bare code.
struct JoinTarget {
    SessionCode code;
    std::optional<QUrl> relay;
};
// Errors: remote.code_invalid (no code in the link, invalid code), usage.invalid_argument (a link
// that is not a valid relay URL, e.g. http).
[[nodiscard]] Result<JoinTarget> parseJoinInput(std::string_view text);
// True when two relay URLs name the same server (scheme, host case-insensitive, port, path).
[[nodiscard]] bool sameRelay(const QUrl& a, const QUrl& b);

} // namespace hwio::remote
