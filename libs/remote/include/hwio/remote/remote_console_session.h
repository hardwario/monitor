#pragma once

// RemoteConsoleSession - the joiner side of a remote support session (plan/features/0044,
// ADR-0325): a device::ConsoleSession of Transport::Remote, so that the GUI console page and the
// CLI console use it unchanged. No hardware: the console is the host's, through the relay
// (RelayClient on its worker thread). Owner thread only.
//
//   sendLine(line)        → write <line> (at most maxLineBytes; the host passes it to its device
//                           only after the customer allowed this participant; the echo comes back
//                           as at/write)
//   at {state}            → connection(): connected → Connected, connection → Connecting,
//                           disconnected → Disconnected
//   at {write} / {read}   → In / Out lines (a read line with a Zephyr level tag goes to the Log
//                           channel); the log holds the lines of every participant
//   sync                  → the log is replaced by the snapshot (bounded: TranscriptLimits, oldest
//                           first), transcriptReplaced(); a Monitor host sends it when it allows
//                           this participant (joinState() Allowed)
//   at {hwio: hello}      → hostHello(), title, the joiner's own hello is sent (ADR-0336: tunnel
//                           objects only after the host's hello)
//   at {hwio: op-*}       → hostMessage() (RemoteOperationClient)
//   at {hwio: line-refused} → a marker line (plan 0059, ADR-0416: the host refused a line of
//                           this joiner's; the joiner hello lists the refused-lines cap)
//   at {hwio: commands}   → commandTree(), commandTreeChanged() (ADR-0364: the host's learned
//                           command tree, validated by TunnelCodec); requestLearn(path) asks the
//                           host for a command's subcommands (tunnel `learn`, once per path, again
//                           after learnRetry; at most maxLearnPaths paths per session)
// The relay connection lost → Disconnected (remote.relay_unreachable; it reconnects), failed →
// Stopped. A Monitor host that ends sharing sends nothing (the relay does not announce it), so the
// session ends only when the relay connection fails; idleNotice() after idleNoticeAfter without any
// event from the host (plan 0044 § Joiner, step 5). recordRemoteEvent() adds a marker to the log.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/command_tree.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/remote/relay_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/transcript.h"
#include "hwio/remote/tunnel.h"

#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::remote {

struct JoinOptions {
    QUrl relay;
    SessionCode code = SessionCode::generate();
    std::string appVersion; // the joiner's hello
    RelayLimits relayLimits;
    TranscriptLimits transcript;
    std::chrono::milliseconds idleNoticeAfter{30LL * 60 * 1000};
    // ADR-0063: nullptr = the session creates its own log.
    std::shared_ptr<device::ConsoleLog> consoleLog;
};

enum class JoinState : std::uint8_t {
    Connecting, // to the relay
    Waiting,    // in the session; no sync yet (the customer has not allowed this participant)
    Allowed,    // a sync arrived (a web Terminal host sends one to every newcomer at once)
    Ended,      // the relay connection failed or close()
};

class RemoteConsoleSession final : public device::ConsoleSession {
    Q_OBJECT

public:
    static constexpr std::size_t maxLineBytes = 512;
    static constexpr std::size_t maxLearnPaths = 200;
    static constexpr std::chrono::seconds learnRetry{10};

    // Connects at once (non-blocking).
    explicit RemoteConsoleSession(JoinOptions options);
    ~RemoteConsoleSession() override;
    RemoteConsoleSession(const RemoteConsoleSession&) = delete;
    RemoteConsoleSession& operator=(const RemoteConsoleSession&) = delete;
    RemoteConsoleSession(RemoteConsoleSession&&) = delete;
    RemoteConsoleSession& operator=(RemoteConsoleSession&&) = delete;

    // Errors: usage.invalid_argument (too long, control characters), remote.host_ended (ended),
    // target.not_connected (not in the session yet).
    [[nodiscard]] Status sendLine(std::string line) override;
    // Nothing to reconnect by hand (the relay connection reconnects by itself).
    [[nodiscard]] Status reconnect() override;
    [[nodiscard]] Status recordRemoteEvent(std::string text) override;
    [[nodiscard]] const device::ConsoleLog& log() const override { return *m_log; }
    [[nodiscard]] const device::ConnectionEvent& connection() const override {
        return m_connection;
    }
    // {Remote, "<code>", title()}.
    [[nodiscard]] device::SessionIdentity identity() const override;
    // Operations when the host's hello said ops: true.
    [[nodiscard]] device::Capabilities capabilities() const override;
    // Plan 0059 (ADR-0415): profileFor(hostHello()).
    [[nodiscard]] const device::DeviceProfile& profile() const override;
    // The console rules for a host hello: the named profile when this build has it, else
    // device::genericConsoleProfile(); CHESTER for a hello without a profile (an older host) and
    // without a hello (web Terminal host, or before the hello).
    [[nodiscard]] static const device::DeviceProfile&
    profileFor(const std::optional<tunnel::HostHello>& hello);
    [[nodiscard]] std::string_view consoleTargetId() const override { return "app"; }
    [[nodiscard]] std::filesystem::path consoleFile() const override { return {}; }
    [[nodiscard]] Status setFirmwareIdentity(std::optional<FirmwareIdentity> identity,
                                             std::uint64_t attach) override;

    [[nodiscard]] JoinState joinState() const noexcept { return m_state; }
    [[nodiscard]] const SessionCode& code() const noexcept { return m_options.code; }
    [[nodiscard]] const QUrl& relay() const noexcept { return m_options.relay; }
    // The host's hello (a Monitor host), nullopt before it / for a web Terminal host.
    [[nodiscard]] const std::optional<tunnel::HostHello>& hostHello() const noexcept {
        return m_hostHello;
    }
    // "CHESTER-M 1234567890 — remote", "Session 482 913 — web Terminal" (no hello after a sync),
    // "Session 482 913" before.
    [[nodiscard]] QString title() const;
    // Plan 0059 (ADR-0414): the one place that keeps a joiner from sending a host what it cannot
    // decode: a host hello without `targets` (an older host) gets only app / lte requests with
    // catalog IDs (and ops: true); a host that lists its targets only listed targets, kinds and
    // prepared firmware. Errors: remote.operation_unsupported_peer, remote.target_unsupported,
    // remote.firmware_not_in_catalog (a catalog ID for a target without a catalog).
    [[nodiscard]] static Status hostOffers(const tunnel::HostHello& hello,
                                           const tunnel::OpRequest& request);
    // Sends a tunnel message (only after the host's hello). Errors:
    // remote.operation_unsupported_peer (no hello), remote.host_ended; for an op-request those of
    // hostOffers().
    [[nodiscard]] Status sendTunnel(const tunnel::JoinerMessage& message);
    // ADR-0364: the host's command tree (nullptr: none, e.g. a web Terminal host).
    [[nodiscard]] std::shared_ptr<const device::CommandTree> commandTree() const {
        return m_commandTree;
    }
    // ADR-0364: asks a Monitor host to learn the subcommands of `path` (allowed, after its hello;
    // otherwise nothing).
    void requestLearn(const std::vector<std::string>& path);
    // The relay error that ended the session (Ended).
    [[nodiscard]] const std::optional<Error>& endError() const noexcept { return m_endError; }

signals:
    void joinStateChanged();
    void hostHelloReceived();
    // The log was replaced by a sync snapshot (the views reload it).
    void transcriptReplaced();
    void hostMessage(const hwio::remote::tunnel::HostMessage& message);
    // No event from the host for idleNoticeAfter.
    void idleNotice();
    // ADR-0364: commandTree() changed.
    void commandTreeChanged();

protected:
    void closeSession() override;

private:
    void onAt(const QJsonValue& payload);
    void onTunnelAt(const QJsonValue& payload); // a hwio tunnel object (ADR-0336)
    void onSync(const QJsonValue& payload);
    void appendLines(QList<device::ConsoleLine> lines);
    void setConnection(device::ConnectionState state, std::optional<Error> error = std::nullopt);
    void setState(JoinState state);
    void hostActivity();
    void end(const std::optional<Error>& error);
    [[nodiscard]] static device::ConsoleLine makeLine(device::Direction dir, const QString& text);

    JoinOptions m_options;
    std::shared_ptr<device::ConsoleLog> m_log;
    std::unique_ptr<RelayClient> m_relay;
    device::ConnectionEvent m_connection;
    JoinState m_state = JoinState::Connecting;
    std::optional<tunnel::HostHello> m_hostHello;
    bool m_syncSeen = false;
    std::optional<Error> m_endError;
    QTimer m_idleTimer{this};
    std::uint64_t m_attach = 0;
    std::shared_ptr<const device::CommandTree> m_commandTree;
    // Paths asked for and when (<= maxLearnPaths).
    std::map<std::vector<std::string>, std::chrono::steady_clock::time_point> m_learnAsked;
};

} // namespace hwio::remote

Q_DECLARE_METATYPE(hwio::remote::tunnel::HostMessage)
