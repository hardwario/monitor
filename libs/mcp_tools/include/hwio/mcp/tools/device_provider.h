#pragma once

// IDeviceProvider - how the MCP tools reach devices (plan/features/0013 "Device and consent seams",
// ADR-0098, ADR-0105).
//
// The tools never own sessions. The CLI's CliDeviceProvider owns the sessions it opened (at most
// SessionRegistry::maxSessions); the GUI's provider (plan 0014) maps devices to its tabs. Device
// ids are the session keys as strings: "jlink:<SN>" or "ble:<BleDeviceId>" (ADR-0098).
//
// DeviceRef holds QPointers: a GUI tab can close at any time, so every tool handler checks them on
// each step and ends with mcp.device_not_open when they are gone. DeviceRef::jlink is null for BLE
// sessions (the J-Link tools then return mcp.capability_missing).
//
// Change notifications (not in the plan's sketch, added for resources, ADR-0107): the provider
// emits DeviceProviderNotifier::devicesChanged() after a device was opened or closed (also when
// the user opened or closed a GUI tab). ToolsContext turns it into resources/list_changed and
// (re)connects to the sessions' linesReceived / connectionChanged for resources/updated.
//
// Plan 0062 (ADR-0433..0438): a joined remote support tab is a device of kind "remote" with the
// id "remote:<n>" (a counter of this run, never the session code). Its DeviceRef holds the tab's
// console session (the log the tools read) and an IRemoteDevice (below), through which the tools
// send commands, request operations and see every line in the hidden view; DeviceRef::jlink is
// null. A provider without remote support (the CLI) never lists one.
//
// Thread safety: owner thread only (ADR-0097).

#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_redaction.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_session.h"
#include "hwio/device/probe_lock.h"
#include "hwio/device/session_events.h"
#include "hwio/mcp/request_context.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::mcp::tools {

// ---- remote support tabs (plan 0062)
// -------------------------------------------------------------

// Support's state in the customer's session (the joiner's RemoteConsoleSession::JoinState).
enum class RemoteJoin : std::uint8_t { Connecting, Waiting, Allowed, Ended };
// "connecting", "waiting", "allowed", "ended"
[[nodiscard]] std::string_view remoteJoinId(RemoteJoin join) noexcept;

// Who hosts the session: a HARDWARIO Monitor (it sent a hello), or a host without a hello (the web
// Terminal or the mobile app).
enum class RemoteHost : std::uint8_t { Monitor, Other };
// "monitor", "other"
[[nodiscard]] std::string_view remoteHostId(RemoteHost host) noexcept;

// The operations an AI client may request on a customer's device (D8, ADR-0434).
enum class RemoteOperationKind : std::uint8_t { Flash, Reset, LteUpdate };
// "flash", "reset", "lteUpdate" (the tunnel's kind ids)
[[nodiscard]] std::string_view remoteOperationKindId(RemoteOperationKind kind) noexcept;

// NOLINTBEGIN(readability-redundant-member-init): the {} initializers keep fields optional in
// designated initializers (-Wmissing-designated-field-initializers), as in remote/tunnel.h.
// ADR-0418: a firmware the host prepared (texts from the host's hello, already bounded there).
struct RemotePreparedFirmware {
    std::string id;     // a name ("release")
    std::string name{}; // "STICKER"
    std::string version{};
};

// What the host offers on one target, reduced to RemoteOperationKind. The LTE update has no
// target (`target` empty).
struct RemoteTargetOffer {
    std::string target;                     // "app"; "" for the LTE update
    std::vector<RemoteOperationKind> kinds; // no duplicates
    bool catalog = false;                   // flash with a catalog firmware ID
    std::vector<RemotePreparedFirmware> prepared{};
};

// Bounds of RemoteInfo (the tunnel's: 8 targets plus the LTE update, 4 prepared per target).
inline constexpr std::size_t maxRemoteOffers = 9;
inline constexpr std::size_t maxRemotePrepared = 4;

struct RemoteInfo {
    RemoteHost host = RemoteHost::Other;
    RemoteJoin join = RemoteJoin::Connecting;
    bool commands = true; // the host shares a console (false: an operations-only session)
    std::vector<RemoteTargetOffer> operations{}; // at most maxRemoteOffers
};

// A request of RemoteOperationKind (the joiner's op-request: catalog flash, prepared flash, reset,
// LTE update with the host's recommended firmware).
struct RemoteOperationRequest {
    RemoteOperationKind kind = RemoteOperationKind::Reset;
    std::string target{};                    // flash / reset; "" for the LTE update
    std::optional<std::string> firmwareId{}; // flash: a catalog firmware ID
    std::optional<std::string> prepared{};   // flash: a prepared firmware id of the offer
};

using RemoteOperationId = std::uint64_t;

struct RemoteOperationProgress {
    enum class Stage : std::uint8_t {
        WaitingForCustomer, // sent; the customer has not accepted yet
        Running,
        WaitingForSupport, // ADR-0368: a question for the support person (D13)
    };
    Stage stage = Stage::Running;
    std::string phase{}; // ProgramPhase id of the host ("download", "erasing", ...), "" = unknown
    int step = 1;
    int steps = 1;
    std::uint64_t done = 0;
    std::uint64_t total = 0; // 0 = indeterminate
};

// NOLINTEND(readability-redundant-member-init)

struct RemoteOperationCallbacks {
    std::function<void(const RemoteOperationProgress&)> progress; // any number, before done
    // Exactly once (owner thread, possibly before requestOperation() returns) unless the
    // operation was detached first: success, or the host's / the client's error
    // (remote.operation_denied, the op-result's error id, timeout, remote.host_ended, ...).
    std::function<void(hwio::Status)> done;
};

// A joined remote support tab as the MCP tools see it (ADR-0434..0438). Implemented by the GUI over
// the tab's RemoteConsoleSession and its RemoteOperationClient (libs/mcp_tools does not depend on
// libs/remote). Owner thread (the GUI thread) only; the object lives as long as the tab is in the
// session (a QPointer in DeviceRef).
//
// The session code: the tools never get it. hiddenText() is the only place that knows it; it
// replaces it (RemoteHiddenView, remote_hidden_view.h, implements the whole rule).
class IRemoteDevice : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Host kind, join state and the operations the host offers (as the tools may request them).
    [[nodiscard]] virtual RemoteInfo info() const = 0;
    // A plain remote line, exactly as a person's (RemoteConsoleSession::sendLine); `who` is for
    // the GUI's activity. Errors: those of RemoteConsoleSession::sendLine.
    [[nodiscard]] virtual hwio::Status sendLine(std::string line,
                                                const hwio::mcp::RequestContext& who) = 0;
    // One at a time per tab (internal.busy for another; also while a person's request runs).
    // Errors before sending: the joiner's hostOffers() errors, internal.busy, remote.host_ended.
    [[nodiscard]] virtual hwio::Result<RemoteOperationId>
    requestOperation(const RemoteOperationRequest& request, const hwio::mcp::RequestContext& who,
                     RemoteOperationCallbacks callbacks) = 0;
    // D12: stop waiting. No further callback; the tab keeps following the operation to its result
    // and stays busy until then. Unknown ids are ignored.
    virtual void detachOperation(RemoteOperationId id) = 0;
    // The redaction of the host's profile (CHESTER without a hello).
    [[nodiscard]] virtual hwio::device::ConsoleRedaction redaction() const = 0;
    // ADR-0437: the text of `line` (englishText) as an AI client may see it: redaction(), then the
    // session code (plain and grouped) replaced with "<session>" (RemoteHiddenView::text()).
    [[nodiscard]] virtual std::string hiddenText(const hwio::device::ConsoleLine& line) const = 0;
    // ADR-0436: support allowed this client (RequestContext::clientKey) for this tab. The grant is
    // kept here: it ends with the tab or the session, with Stop AI and when the client goes away
    // (the GUI calls revoke and emits aiRevoked).
    [[nodiscard]] virtual bool aiAllowed(std::string_view clientKey) const = 0;
    // Records support's "Allow for this session" for `who` (the tools call it after the prompt).
    virtual void allowAi(const hwio::mcp::RequestContext& who) = 0;

signals:
    // The host refused one of support's lines (tunnel line-refused; `code` an hwio::Error id).
    void lineRefused(const QString& code);
    // The grant of `clientKey` ended ("" = of every client): the client's running calls end with
    // `cancelled`.
    void aiRevoked(const QString& clientKey);
    // info() changed (join state, host hello).
    void infoChanged();
};

// ---- devices
// -------------------------------------------------------------------------------------

struct DeviceEntry {
    std::string id;          // "jlink:<SN>" | "ble:<id>" (ADR-0098; SessionKey ProbeKey/BleKey) |
                             // "remote:<n>" (plan 0062)
    std::string kind;        // "jlink" | "ble" | "remote"
    std::string displayName; // "J-Link 801000001", "CHESTER-ABC123"
    std::string profileId;   // "chester"; empty when unknown (a probe that is not open)
    enum class State : std::uint8_t {
        Open,
        Available,
        InUseByOtherProcess
    } state = State::Available;
    // ProbeStatus::usedByOtherProcess / BleUse::otherProcess
    std::optional<hwio::device::ProbeLockOwner> owner;
    std::optional<hwio::device::ConnectionEvent> connection; // open devices only
    bool openedByMcp = false;
};

struct DeviceRef {
    QPointer<hwio::device::ConsoleSession> console;
    QPointer<hwio::device::DeviceSession> jlink; // null for BLE sessions and remote tabs
    // Plan 0062: a joined remote tab (console = its RemoteConsoleSession). `isRemote` stays set
    // when `remote` is gone, so that a remote device never passes for a local one.
    // NOLINTNEXTLINE(readability-redundant-member-init): optional in designated initializers
    QPointer<IRemoteDevice> remote{};
    bool isRemote = false;

    // The console session (and the remote device of a remote tab) still exists.
    [[nodiscard]] bool alive() const {
        return !console.isNull() && (!isRemote || !remote.isNull());
    }
};

// What `connect` asks the provider to open (not sketched by the plan; fields follow the connect
// tool's arguments, ADR-0098).
struct OpenRequest {
    std::string id;                    // "jlink:<SN>" | "ble:<id>"
    std::string profileId;             // empty = the provider's default ("chester")
    std::string targetId;              // empty = the profile's first target ("app")
    std::optional<int> speedKhz;       // J-Link only; nullopt = the provider's default
    std::optional<bool> autoReconnect; // nullopt = the provider's default
    hwio::mcp::RequestContext context; // the client (GUI consent / tab title, activity)
};

// The provider's change signal (a separate QObject so that a provider can be a QObject of its own,
// e.g. a GUI model, without multiple QObject inheritance).
class DeviceProviderNotifier : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

signals:
    // A device was opened or closed (by MCP or, in the GUI, by the user).
    void devicesChanged();
};

class IDeviceProvider {
public:
    IDeviceProvider() = default;
    IDeviceProvider(const IDeviceProvider&) = delete;
    IDeviceProvider& operator=(const IDeviceProvider&) = delete;
    IDeviceProvider(IDeviceProvider&&) = delete;
    IDeviceProvider& operator=(IDeviceProvider&&) = delete;
    virtual ~IDeviceProvider() = default;

    // Cheap: the open sessions plus the cached probe / BLE scan results. Never enumerates
    // (ADR-0162): before the first refresh() has finished the cache is empty.
    [[nodiscard]] virtual std::vector<DeviceEntry> list() = 0;
    // One probe enumeration, asynchronous (ADR-0162: the blocking enumeration runs on a worker
    // thread; the owner thread is never blocked). `done` is called exactly once on the owner
    // thread with the outcome, after the cache that list() returns was updated; it may be called
    // before refresh() returns (e.g. an error found at once). A refresh requested while one is
    // running joins it (its `done` is called with the same outcome). When the running
    // enumeration is dropped (the provider's offloader stopped or destroyed, server shutdown),
    // `done` is called with `cancelled` from a later event-loop pass (review F3). The probe
    // enumeration itself cannot be interrupted; it is bounded (about 2 s, review F2).
    virtual void refresh(std::function<void(hwio::Status)> done) = 0;
    // Review V1: true while the first probe enumeration (the one the server starts at start-up)
    // runs. list_devices without `refresh` then waits for it by calling refresh(), which joins
    // the running enumeration instead of starting a second one; afterwards it lists the cache at
    // once. A provider without a start-up enumeration keeps the default (false).
    [[nodiscard]] virtual bool firstScanPending() const { return false; }
    // Opens the session, or returns the open one. Async because the GUI asks for consent first.
    // `done` is called exactly once (possibly before open() returns). The returned session may
    // still be Connecting (DeviceSession::open is non-blocking, ADR-0053).
    virtual void open(const OpenRequest& request,
                      std::function<void(hwio::Result<DeviceRef>)> done) = 0;
    // Errors: mcp.device_not_open (unknown id), mcp.tool_not_allowed (GUI: a tab the user opened).
    [[nodiscard]] virtual hwio::Status close(std::string_view id,
                                             const hwio::mcp::RequestContext& context) = 0;
    // The open session, or nullopt.
    [[nodiscard]] virtual std::optional<DeviceRef> find(std::string_view id) = 0;
    // ADR-0105: an AI operation runs on the device ("AI: Flashing…" + a Cancel button in the GUI);
    // `cancel` is called (owner thread) when the user cancels it. endBusy() follows every
    // successful beginBusy(). Errors (plan 0014 review M3): internal.busy when the device is busy
    // with another (user) operation; the tool then ends with that error.
    [[nodiscard]] virtual hwio::Status beginBusy(std::string_view id, QString text,
                                                 std::function<void()> cancel) = 0;
    virtual void endBusy(std::string_view id) = 0;
    // ADR-0105: a console marker line in the device's ConsoleLog (ConsoleLog::appendMarker); the
    // text is already translated.
    virtual void marker(std::string_view id, QString text) = 0;
    // ADR-0105: "AI is running a command" in the console input while active.
    virtual void setAiCommandHint(std::string_view id, bool active) = 0;
    // The change notifier (owned by the provider, lives as long as it).
    [[nodiscard]] virtual DeviceProviderNotifier& notifier() = 0;
};

} // namespace hwio::mcp::tools
