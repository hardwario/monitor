#include "mcp/gui_device_provider.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_session.h"
#include "hwio/device/probe_lock.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_registry.h"
#include "hwio/hw/ble/ble_scanner.h"
#include "hwio/hw/ble/types.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tools/device_provider.h"
#include "mcp/client_name.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "workers/ble_service.h"
#include "workers/probe_scanner.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QModelIndex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

using hwio::mcp::tools::DeviceEntry;
using hwio::mcp::tools::DeviceRef;

constexpr std::string_view jlinkPrefix = "jlink:";
constexpr std::string_view blePrefix = "ble:";
constexpr std::string_view defaultProfileId = "chester";

Error deviceNotOpen(std::string_view id) {
    return makeError(ErrorCode::McpDeviceNotOpen, std::string(id));
}

QString busyPage() {
    return QString::fromLatin1(GuiDeviceProvider::busyPageId.data(),
                               static_cast<qsizetype>(GuiDeviceProvider::busyPageId.size()));
}

// The error a tab shows (ConnectionViewModel), as an hwio::Error with the same texts.
Error errorOf(const ErrorInfo& info) {
    Error error =
        makeError(errorCodeFromId(toStdString(info.code())).value_or(ErrorCode::InternalUnexpected),
                  toStdString(info.detail()));
    error.message = toStdString(info.message());
    error.cause = toStdString(info.cause());
    error.suggestion = toStdString(info.suggestion());
    return error;
}

bool ownProcess(const std::optional<device::ProbeLockOwner>& owner) {
    return owner && owner->pid == QCoreApplication::applicationPid();
}

} // namespace

GuiDeviceProvider::GuiDeviceProvider(AppViewModel& app) : m_app(&app) {
    DeviceTabListModel* list = app.tabs();
    for (int i = 0; i < list->count(); ++i) {
        watchTab(list->at(i));
    }
    QObject::connect(list, &QAbstractItemModel::rowsInserted, &m_notifier,
                     [this](const QModelIndex&, int first, int last) {
                         if (DeviceTabListModel* model = tabs()) {
                             for (int row = first; row <= last; ++row) {
                                 watchTab(model->at(row)); // nullptr for the Settings row
                             }
                         }
                         emit m_notifier.devicesChanged();
                     });
    QObject::connect(list, &QAbstractItemModel::rowsRemoved, &m_notifier,
                     [this] { emit m_notifier.devicesChanged(); });
    QObject::connect(&app.probeScanner(), &ProbeScanner::scanned, &m_notifier,
                     [this](const ProbeScan& scan) { onScanned(scan); });
    if (BleService* ble = app.bleService()) { // step D1
        QObject::connect(ble, &BleService::devicesChanged, &m_notifier,
                         [this] { cacheMaybeChanged(); });
    }
    m_cacheSignature = cacheSignature();
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(refreshTimeout);
    QObject::connect(&m_refreshTimer, &QTimer::timeout, &m_notifier, [this] {
        finishRefresh(std::unexpected(makeError(ErrorCode::Timeout, "no probe scan arrived")));
    });
}

GuiDeviceProvider::~GuiDeviceProvider() {
    // Waiters of a refresh that never came (shutdown): they belong to calls that were cancelled.
    m_refreshWaiters.clear();
}

DeviceTabListModel* GuiDeviceProvider::tabs() const {
    return m_app.isNull() ? nullptr : m_app->tabs();
}

void GuiDeviceProvider::watchTab(DeviceTabViewModel* tab) {
    if (tab == nullptr) {
        return;
    }
    // The tab's signals end with the tab; the notifier is the context (ends with the provider).
    QObject::connect(tab, &DeviceTabViewModel::sessionChanged, &m_notifier,
                     [this] { emit m_notifier.devicesChanged(); });
    QObject::connect(tab, &DeviceTabViewModel::configurationChanged, &m_notifier,
                     [this] { emit m_notifier.devicesChanged(); });
}

std::optional<std::uint32_t> GuiDeviceProvider::probeSerialOf(std::string_view id) {
    if (!id.starts_with(jlinkPrefix)) {
        return std::nullopt;
    }
    const std::string_view digits = id.substr(jlinkPrefix.size());
    constexpr std::size_t maxDigits = 10; // 4294967295
    if (digits.empty() || digits.front() == '0' || digits.size() > maxDigits) {
        return std::nullopt;
    }
    std::uint64_t serial = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        serial = (serial * 10) + static_cast<std::uint64_t>(c - '0');
    }
    if (serial > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(serial);
}

std::string GuiDeviceProvider::bleIdOf(std::string_view id) {
    if (!id.starts_with(blePrefix) || id.size() == blePrefix.size()) {
        return {};
    }
    return ble::canonicalDeviceId(id.substr(blePrefix.size()));
}

std::string GuiDeviceProvider::deviceId(const DeviceTabViewModel& tab) {
    if (tab.choosing()) {
        return {};
    }
    if (tab.isBle()) {
        return tab.bleDeviceId().isEmpty()
                   ? std::string()
                   : std::format("{}{}", blePrefix, toStdString(tab.bleDeviceId()));
    }
    const std::optional<std::uint32_t> serial = tab.probeSerial();
    return serial ? std::format("{}{}", jlinkPrefix, *serial) : std::string();
}

bool GuiDeviceProvider::aiOperationRuns(std::uint32_t serial) const {
    DeviceTabListModel* list = tabs();
    if (list == nullptr) {
        return false;
    }
    for (int i = 0; i < list->count(); ++i) {
        const DeviceTabViewModel* tab = list->at(i);
        if (tab != nullptr && !tab->choosing() && !tab->isBle() && tab->probeSerial() == serial &&
            tab->busyPage() == busyPage()) {
            return true;
        }
    }
    return false;
}

DeviceTabViewModel* GuiDeviceProvider::tabFor(std::string_view id) const {
    DeviceTabListModel* list = tabs();
    if (list == nullptr) {
        return nullptr;
    }
    const std::optional<std::uint32_t> serial = probeSerialOf(id);
    const std::string bleId = bleIdOf(id);
    for (int i = 0; i < list->count(); ++i) {
        DeviceTabViewModel* tab = list->at(i);
        if (tab == nullptr || tab->choosing()) {
            continue;
        }
        if (serial && !tab->isBle() && tab->probeSerial() == serial) {
            return tab;
        }
        if (!bleId.empty() && tab->isBle() &&
            ble::sameDeviceId(toStdString(tab->bleDeviceId()), bleId)) {
            return tab;
        }
    }
    return nullptr;
}

namespace {

DeviceEntry tabEntry(const DeviceTabViewModel& tab, std::string id) {
    const device::ConsoleSession* session = tab.consoleSession();
    return DeviceEntry{
        .id = std::move(id),
        .kind = tab.isBle() ? "ble" : "jlink",
        .displayName = toStdString(tab.title()),
        .profileId = toStdString(tab.profileId()),
        .state = session != nullptr ? DeviceEntry::State::Open : DeviceEntry::State::Available,
        .owner = std::nullopt,
        .connection = session != nullptr ? std::optional(session->connection()) : std::nullopt,
        .openedByMcp = !tab.openedBy().isEmpty()};
}

DeviceEntry probeEntry(const ScannedProbe& probe, std::string id) {
    const bool other = probe.lockOwner && !ownProcess(probe.lockOwner);
    return DeviceEntry{
        .id = std::move(id),
        .kind = "jlink",
        .displayName =
            probe.probe.product.empty()
                ? std::format("J-Link {}", probe.probe.serialNumber)
                : std::format("J-Link {} ({})", probe.probe.serialNumber, probe.probe.product),
        .profileId = {},
        .state = other ? DeviceEntry::State::InUseByOtherProcess : DeviceEntry::State::Available,
        .owner = other ? probe.lockOwner : std::nullopt,
        .connection = std::nullopt,
        .openedByMcp = false};
}

DeviceEntry bleEntry(const ble::BleDeviceInfo& device, const device::BleUse& use, std::string id) {
    const bool other = use.session == nullptr && use.otherProcess.has_value();
    return DeviceEntry{.id = std::move(id),
                       .kind = "ble",
                       .displayName = device.name.empty() ? device.id : device.name,
                       .profileId = device.profileId,
                       .state = other ? DeviceEntry::State::InUseByOtherProcess
                                      : DeviceEntry::State::Available,
                       .owner = other ? use.otherProcess : std::nullopt,
                       .connection = std::nullopt,
                       .openedByMcp = false};
}

} // namespace

std::vector<DeviceEntry> GuiDeviceProvider::list() {
    std::vector<DeviceEntry> entries;
    DeviceTabListModel* list = tabs();
    if (list == nullptr) {
        return entries;
    }
    for (int i = 0; i < list->count(); ++i) {
        const DeviceTabViewModel* tab = list->at(i);
        if (std::string id = tab != nullptr ? deviceId(*tab) : std::string(); !id.empty()) {
            entries.push_back(tabEntry(*tab, std::move(id)));
        }
    }
    // The probes of the last scan without a tab.
    if (const std::optional<ProbeScan>& scan = m_app->probeScanner().last(); scan) {
        for (const ScannedProbe& probe : scan->probes) {
            if (std::string id = std::format("{}{}", jlinkPrefix, probe.probe.serialNumber);
                tabFor(id) == nullptr) {
                entries.push_back(probeEntry(probe, std::move(id)));
            }
        }
    }
    // Step D1 (ADR-0243): the Bluetooth devices seen recently without a tab.
    if (const BleService* ble = m_app->bleService(); ble != nullptr) {
        for (const ble::BleDeviceInfo& device : ble->devices()) {
            if (std::string id = std::format("{}{}", blePrefix, device.id); tabFor(id) == nullptr) {
                entries.push_back(
                    bleEntry(device, m_app->sessionRegistry().bleInUse(device.id), std::move(id)));
            }
        }
    }
    return entries;
}

std::string GuiDeviceProvider::cacheSignature() const {
    std::string signature;
    if (m_app.isNull()) {
        return signature;
    }
    if (const std::optional<ProbeScan>& scan = m_app->probeScanner().last(); scan) {
        for (const ScannedProbe& probe : scan->probes) {
            const bool other = probe.lockOwner && !ownProcess(probe.lockOwner);
            signature +=
                std::format("j{}:{}:{}:{};", probe.probe.serialNumber, probe.probe.product,
                            other ? probe.lockOwner->app : "", other ? probe.lockOwner->pid : 0);
        }
    }
    if (const BleService* ble = m_app->bleService(); ble != nullptr) {
        for (const ble::BleDeviceInfo& device : ble->devices()) {
            signature += std::format("b{}:{};", device.id, device.name);
        }
    }
    return signature;
}

void GuiDeviceProvider::cacheMaybeChanged() {
    std::string signature = cacheSignature();
    if (signature != m_cacheSignature) {
        m_cacheSignature = std::move(signature);
        emit m_notifier.devicesChanged();
    }
}

void GuiDeviceProvider::onScanned(const ProbeScan& scan) {
    cacheMaybeChanged();
    if (m_refreshWaiters.empty()) {
        return;
    }
    Status outcome;
    if (scan.libraryError) {
        outcome = std::unexpected(*scan.libraryError);
    }
    finishRefresh(outcome);
}

void GuiDeviceProvider::finishRefresh(const Status& outcome) {
    m_refreshTimer.stop();
    for (auto& waiter : std::exchange(m_refreshWaiters, {})) {
        waiter(outcome);
    }
}

void GuiDeviceProvider::refresh(std::function<void(hwio::Status)> done) {
    if (m_app.isNull()) {
        done(std::unexpected(makeError(ErrorCode::Cancelled, "the application is closing")));
        return;
    }
    if (m_refreshWaiters.size() >= maxPendingRefreshes) {
        done(std::unexpected(
            makeError(ErrorCode::McpTooManyRequests, "too many waiting probe enumerations")));
        return;
    }
    const bool first = m_refreshWaiters.empty();
    m_refreshWaiters.push_back(std::move(done));
    if (first) {
        m_refreshTimer.start();
        m_app->probeScanner().scanNow(); // a running request is joined
    }
}

bool GuiDeviceProvider::firstScanPending() const {
    return !m_app.isNull() && !m_app->probeScanner().last().has_value();
}

std::optional<DeviceRef> GuiDeviceProvider::find(std::string_view id) {
    DeviceTabViewModel* tab = tabFor(id);
    if (tab == nullptr || tab->consoleSession() == nullptr) {
        return std::nullopt;
    }
    return DeviceRef{.console = tab->consoleSession(), .jlink = tab->session()};
}

void GuiDeviceProvider::open(const hwio::mcp::tools::OpenRequest& request,
                             std::function<void(hwio::Result<DeviceRef>)> done) {
    done(openDevice(request));
}

Result<DeviceRef> GuiDeviceProvider::connectTab(DeviceTabViewModel& tab) {
    if (tab.isBusy()) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "the tab is busy"));
    }
    tab.connectDevice();
    if (tab.consoleSession() == nullptr) {
        const ErrorInfo shown = tab.connection()->error();
        return std::unexpected(shown.valid() ? errorOf(shown)
                                             : makeError(ErrorCode::InternalUnexpected,
                                                         "the tab did not open a session"));
    }
    return DeviceRef{.console = tab.consoleSession(), .jlink = tab.session()};
}

bool GuiDeviceProvider::configureProbeTab(DeviceTabViewModel& tab, const QString& profileId,
                                          const std::string& targetId, std::uint32_t serial) {
    // An empty target = the profile's console target (ADR-0233: a tab always uses it).
    QString target = toQString(targetId);
    if (target.isEmpty()) {
        if (const device::DeviceProfile* profile =
                device::ProfileRegistry::find(toStdString(profileId));
            profile != nullptr) {
            target = toQString(device::consoleTargetId(*profile));
        }
    }
    return tab.configure(profileId, target, serial);
}

QString GuiDeviceProvider::seenBleName(const std::string& bleId) const {
    if (const BleService* ble = m_app.isNull() ? nullptr : m_app->bleService(); ble != nullptr) {
        for (const ble::BleDeviceInfo& device : ble->devices()) {
            if (ble::sameDeviceId(device.id, bleId)) {
                return toQString(device.name);
            }
        }
    }
    return {};
}

Result<DeviceRef> GuiDeviceProvider::openDevice(const hwio::mcp::tools::OpenRequest& request) {
    if (m_app.isNull()) {
        return std::unexpected(deviceNotOpen(request.id));
    }
    const std::optional<std::uint32_t> serial = probeSerialOf(request.id);
    const std::string bleId = bleIdOf(request.id);
    if (!serial && (bleId.empty() || m_app->bleService() == nullptr)) {
        return std::unexpected(deviceNotOpen(request.id));
    }
    if (DeviceTabViewModel* tab = tabFor(request.id); tab != nullptr) {
        if (auto existing = find(request.id)) {
            return *existing;
        }
        // ADR-0242: the user's (or an earlier AI) tab is connected with its own options.
        qCInfo(lcGui).noquote() << "MCP: connecting the tab of" << toQString(request.id);
        return connectTab(*tab);
    }

    const QString profileId = toQString(
        request.profileId.empty() ? defaultProfileId : std::string_view(request.profileId));
    DeviceTabViewModel* tab = m_app->appendTab();
    if (tab == nullptr) {
        return std::unexpected(
            makeError(ErrorCode::InternalSessionLimit, "every device tab is in use"));
    }
    const QPointer<DeviceTabViewModel> created(tab);
    const auto discard = [this, created] {
        // Queued: the call may come from a tab's signal or a QML handler.
        QTimer::singleShot(0, &m_notifier, [this, created] {
            if (!created.isNull() && !m_app.isNull()) {
                m_app->closeTabObject(created.data(), true);
            }
        });
    };
    // Before configure(): ui/tabs is saved on configurationChanged and skips AI tabs (ADR-0244).
    // Review M4: the self-declared name is sanitised; ADR-0260: the key decides who may close it.
    QString client = clientDisplayText(request.context.client.name);
    if (client.isEmpty()) {
        client = QStringLiteral("?");
    }
    tab->setOpenedBy(client, request.context.clientKey);
    const bool configured =
        serial ? configureProbeTab(*tab, profileId, request.targetId, *serial)
               : tab->configureBle(profileId, toQString(bleId), seenBleName(bleId));
    if (!configured) {
        discard();
        Error error = makeError(
            ErrorCode::UsageInvalidArgument,
            std::format("profile {} target {}", toStdString(profileId), request.targetId));
        error.message = std::format(R"(The profile "{}" or its target "{}" cannot be used for {}.)",
                                    toStdString(profileId), request.targetId, request.id);
        return std::unexpected(std::move(error));
    }
    // Only for this tab, never remembered for the probe (plan 0014, clarified 2026-11-04).
    tab->connection()->setSessionOverrides(serial ? request.speedKhz : std::nullopt,
                                           request.autoReconnect);
    qCInfo(lcGui).noquote() << "MCP: opening a tab for" << toQString(request.id) << "for" << client;
    auto opened = connectTab(*tab);
    if (!opened) {
        discard();
    }
    return opened;
}

Status GuiDeviceProvider::close(std::string_view id, const hwio::mcp::RequestContext& context) {
    DeviceTabViewModel* tab = tabFor(id);
    if (tab == nullptr || (tab->openedBy().isEmpty() && tab->consoleSession() == nullptr)) {
        return std::unexpected(deviceNotOpen(id));
    }
    if (tab->openedBy().isEmpty()) {
        Error error = makeError(ErrorCode::McpToolNotAllowed, std::string(id));
        // ADR-0103 / ADR-0240: an MCP call-site text, English.
        error.message =
            std::format("The tab of {} was opened by the user; only the user can close it.", id);
        error.suggestion.clear();
        return std::unexpected(std::move(error));
    }
    if (tab->openedByKey() != context.clientKey) {
        // ADR-0260: only the client that opened the tab may close it.
        Error error = makeError(ErrorCode::McpToolNotAllowed, std::string(id));
        error.message = std::format(
            "The tab of {} was opened by another AI client; only that client or the user can "
            "close it.",
            id);
        error.suggestion.clear();
        return std::unexpected(std::move(error));
    }
    const QPointer<DeviceTabViewModel> target(tab);
    QTimer::singleShot(0, &m_notifier, [this, target] {
        if (!target.isNull() && !m_app.isNull()) {
            m_app->closeTabObject(target.data(), true);
        }
    });
    qCInfo(lcGui).noquote() << "MCP: closing the tab of" << toQString(id);
    return {};
}

Status GuiDeviceProvider::beginBusy(std::string_view id, QString text,
                                    std::function<void()> cancel) {
    DeviceTabViewModel* tab = tabFor(id);
    if (tab == nullptr) {
        return std::unexpected(deviceNotOpen(id));
    }
    if (tab->isBusy() && tab->busyPage() != busyPage()) {
        // Review M3: a user operation (Flash page, wizard) holds the tab.
        return std::unexpected(
            makeError(ErrorCode::InternalBusy,
                      std::format("{} is busy with {}", id, toStdString(tab->busyPage()))));
    }
    tab->setBusy(busyPage(), std::move(text),
                 QCoreApplication::translate("hwio::gui::GuiDeviceProvider",
                                             "An AI assistant is running an operation on this "
                                             "device. Cancel it and close the tab?"),
                 std::move(cancel));
    return {};
}

void GuiDeviceProvider::endBusy(std::string_view id) {
    if (DeviceTabViewModel* tab = tabFor(id); tab != nullptr) {
        tab->clearBusy(busyPage());
    }
}

void GuiDeviceProvider::marker(std::string_view id, QString text) {
    if (DeviceTabViewModel* tab = tabFor(id); tab != nullptr) {
        tab->consoleLog()->appendMarker(device::Channel::Shell, toStdString(text));
    }
}

void GuiDeviceProvider::setAiCommandHint(std::string_view id, bool active) {
    if (DeviceTabViewModel* tab = tabFor(id); tab != nullptr) {
        tab->setAiCommandActive(active);
    }
}

} // namespace hwio::gui
