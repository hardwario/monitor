#pragma once

// GuiDeviceProvider - the IDeviceProvider of the GUI's MCP server (plan/features/0014, ADR-0098,
// ADR-0105, ADR-0242, ADR-0243): the devices are the open device tabs.
//
// list(): the configured J-Link tabs ("jlink:<SN>", display name = the tab title, Open while the
// tab has a session, else Available; openedByMcp = the tab was opened by an AI client), then the
// probes of ProbeScanner::last() without a tab (Available, or InUseByOtherProcess + owner). Step D1
// (plan 0014): the configured Bluetooth tabs ("ble:<id>", DeviceRef::jlink null) and the Bluetooth
// devices of the BleService scan list without a tab (recently seen; Available or
// InUseByOtherProcess). Never enumerates (ADR-0162).
//
// refresh(): ProbeScanner::scanNow(); `done` is called on the next `scanned` (at most
// maxPendingRefreshes waiters; the scan's library error is the outcome). firstScanPending() is true
// while the scanner has no result yet.
//
// open() completes synchronously (review R17; the connect tool asked for consent before): a tab
// with a session -> that session; a user's tab without one -> it is connected (ADR-0242, its own
// options); otherwise a new tab is appended without switching to it (AppViewModel::appendTab, full
// -> internal.session_limit), openedBy = the sanitised client name (and its key), configured with
// the requested profile (default "chester"), the speed / auto-reconnect of the request applied to
// that tab only (ConnectionViewModel::setSessionOverrides: never remembered for the probe), then
// connected. A failed open closes such a new tab again (queued) and is returned.
//
// close(): a tab the calling client opened is closed (AppViewModel::closeTabObject(tab, true),
// queued); a tab of the user or of another client (RequestContext::clientKey, ADR-0260) ->
// mcp.tool_not_allowed (English call-site text, ADR-0103/ADR-0240).
// beginBusy / endBusy -> DeviceTabViewModel::setBusy("ai", ...) / clearBusy("ai"); a tab a user
// page holds busy -> internal.busy (review M3); while an AI operation runs the J-Link firmware
// update of the probe is blocked ("Used by an AI assistant", aiOperationRuns(), plan 0036); a
// probe reserved for that update refuses connect with the registry's probe.open_failed ("The
// firmware of the J-Link %1 is being updated.", SessionRegistry::checkAvailable); marker() ->
// the tab's ConsoleLog (Shell channel); setAiCommandHint() -> DeviceTabViewModel::aiCommandActive.
//
// devicesChanged: a tab was added or removed, a tab's session or configuration changed, the probe
// list of a scan or the Bluetooth device list changed (ids and states only).
//
// Lifetime: owned by McpController (a child of AppViewModel). The application is held by QPointer:
// when the AppViewModel is being destroyed the provider no longer touches it (list() is empty and
// the other calls are no-ops or return mcp.device_not_open). GUI thread only (ADR-0097).

#include "hwio/core/result.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tools/device_provider.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::gui {

class AppViewModel;
class DeviceTabListModel;
class DeviceTabViewModel;
struct ProbeScan;

class GuiDeviceProvider final : public hwio::mcp::tools::IDeviceProvider {
public:
    static constexpr std::size_t maxPendingRefreshes = 64; // plan 0014 "Limits"
    // A refresh whose scan never arrives (scanner stopped) fails with `timeout` after this; it
    // also drops the waiters of cancelled calls (review, low).
    static constexpr std::chrono::milliseconds refreshTimeout{10000};
    // The busy page id of AI operations (DeviceTabViewModel::setBusy, DeviceBar's Cancel).
    static constexpr std::string_view busyPageId = "ai";

    explicit GuiDeviceProvider(AppViewModel& app);
    ~GuiDeviceProvider() override;
    GuiDeviceProvider(const GuiDeviceProvider&) = delete;
    GuiDeviceProvider& operator=(const GuiDeviceProvider&) = delete;
    GuiDeviceProvider(GuiDeviceProvider&&) = delete;
    GuiDeviceProvider& operator=(GuiDeviceProvider&&) = delete;

    [[nodiscard]] std::vector<hwio::mcp::tools::DeviceEntry> list() override;
    void refresh(std::function<void(hwio::Status)> done) override;
    [[nodiscard]] bool firstScanPending() const override;
    void open(const hwio::mcp::tools::OpenRequest& request,
              std::function<void(hwio::Result<hwio::mcp::tools::DeviceRef>)> done) override;
    [[nodiscard]] hwio::Status close(std::string_view id,
                                     const hwio::mcp::RequestContext& context) override;
    [[nodiscard]] std::optional<hwio::mcp::tools::DeviceRef> find(std::string_view id) override;
    [[nodiscard]] hwio::Status beginBusy(std::string_view id, QString text,
                                         std::function<void()> cancel) override;
    void endBusy(std::string_view id) override;
    void marker(std::string_view id, QString text) override;
    void setAiCommandHint(std::string_view id, bool active) override;
    [[nodiscard]] hwio::mcp::tools::DeviceProviderNotifier& notifier() override {
        return m_notifier;
    }

    // The device id of a tab ("jlink:<SN>", "ble:<id>"; "" while choosing or without a device).
    [[nodiscard]] static std::string deviceId(const DeviceTabViewModel& tab);
    // Plan 0036 hook (ProbeFirmwareViewModel::setAiHoldsProbe, ADR-0105, ADR-0132): an AI
    // operation runs (busy page "ai") on a J-Link tab of the probe `serial`.
    [[nodiscard]] bool aiOperationRuns(std::uint32_t serial) const;
    // The configured tab of `id` (with or without a session), or nullptr.
    [[nodiscard]] DeviceTabViewModel* tabFor(std::string_view id) const;
    // "jlink:<serial>" -> the serial (decimal without leading zeros, 1..4294967295).
    [[nodiscard]] static std::optional<std::uint32_t> probeSerialOf(std::string_view id);
    // "ble:<id>" -> the canonical Bluetooth device id ("" otherwise).
    [[nodiscard]] static std::string bleIdOf(std::string_view id);

private:
    [[nodiscard]] DeviceTabListModel* tabs() const;
    [[nodiscard]] hwio::Result<hwio::mcp::tools::DeviceRef>
    openDevice(const hwio::mcp::tools::OpenRequest& request);
    // Connects `tab` (connectDevice) and returns its session or the error it shows.
    [[nodiscard]] static hwio::Result<hwio::mcp::tools::DeviceRef>
    connectTab(DeviceTabViewModel& tab);
    // configure() of a new J-Link tab; an empty target = the profile's console target.
    [[nodiscard]] static bool configureProbeTab(DeviceTabViewModel& tab, const QString& profileId,
                                                const std::string& targetId, std::uint32_t serial);
    // The advertised name of a Bluetooth device of the scan list ("" when not seen).
    [[nodiscard]] QString seenBleName(const std::string& bleId) const;
    void watchTab(DeviceTabViewModel* tab);
    void onScanned(const ProbeScan& scan);
    void finishRefresh(const hwio::Status& outcome);
    // The number of waiting refreshes (tests).
public:
    [[nodiscard]] std::size_t pendingRefreshes() const noexcept { return m_refreshWaiters.size(); }

private:
    // Emits devicesChanged when the cached part of list() (probes, Bluetooth devices) changed.
    void cacheMaybeChanged();
    [[nodiscard]] std::string cacheSignature() const;

    QPointer<AppViewModel> m_app;
    hwio::mcp::tools::DeviceProviderNotifier m_notifier; // also the context of all connections
    std::vector<std::function<void(hwio::Status)>> m_refreshWaiters; // <= maxPendingRefreshes
    QTimer m_refreshTimer; // refreshTimeout while waiters exist
    std::string m_cacheSignature;
};

} // namespace hwio::gui
