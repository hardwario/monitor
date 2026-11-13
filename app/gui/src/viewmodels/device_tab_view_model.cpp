#include "viewmodels/device_tab_view_model.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/core/version.h"
#include "hwio/device/ble_session.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_info.h"
#include "hwio/device/device_info_watcher.h"
#include "hwio/device/device_session.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/device/session_registry.h"
#include "hwio/device/shell_query.h"
#include "hwio/hw/ble/types.h"
#include "hwio/hw/jlink/library_locator.h"
#include "hwio/hw/nrfutil/nrfutil.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/session_code.h"
#include "settings_store.h"
#include "viewmodels/ble_picker_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/page_registry.h"
#include "viewmodels/probe_picker_view_model.h"
#include "viewmodels/remote_join_view_model.h"
#include "viewmodels/remote_share_view_model.h"
#include "workers/ble_service.h"

#include <QDesktopServices>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::gui {

namespace {

// Plan 0036: the probe is reserved for a J-Link firmware update.
bool probeReserved(const device::SessionRegistry* registry, std::uint32_t serial) {
    return registry != nullptr && registry->reserved(serial);
}

const QString lastTransportKey = QStringLiteral("ui/lastTransport");

} // namespace

DeviceTabViewModel::DeviceTabViewModel(TabContext context, QObject* parent)
    : QObject(parent), m_context(std::move(context)),
      m_consoleLog(std::make_shared<device::ConsoleLog>()) {
    // 0021 (ADR-0175): silent shell queries of this tab; none while a page is busy. Qt parent
    // ownership: this tab (created first, so the pages can use it).
    m_queries = new device::ShellQueryQueue( // NOLINT(cppcoreguidelines-owning-memory)
        [this] { return isBusy(); }, this);
    const auto profiles = device::ProfileRegistry::all();
    if (!profiles.empty()) {
        m_profile = profiles.front();
        if (!m_profile->targets.empty()) {
            m_targetId = toQString(m_profile->targets.front().id);
        }
    }

    // Qt parent ownership: the picker and the connection view model are children of this tab.
    m_picker = new ProbePickerViewModel( // NOLINT(cppcoreguidelines-owning-memory)
        m_context.scanner,
        [this](std::uint32_t serial) {
            return m_context.tabUsingProbe ? m_context.tabUsingProbe(serial, this) : -1;
        },
        this);
    // Plan 0036 (ADR-0257): a probe reserved for a firmware update shows "Updating firmware…".
    m_picker->setReservedCheck(
        [this](std::uint32_t serial) { return probeReserved(m_context.registry, serial); });
    connect(m_picker, &ProbePickerViewModel::tabActivationRequested, this,
            &DeviceTabViewModel::tabActivationRequested);
    connect(m_picker, &ProbePickerViewModel::connectRequested, this,
            [this](std::uint32_t serial, const QString& product, const QString& profileId,
                   const QString& targetId) {
                if (configure(profileId, targetId, serial, product)) {
                    connectDevice();
                }
            });
    createBlePicker();
    // ADR-0081: a new tab opens in the last transport.
    m_transport = jlinkTransport();
    if (m_context.settings != nullptr && m_context.settings->isRegistered(lastTransportKey) &&
        m_context.settings->value(lastTransportKey).toString() == bleTransport() &&
        m_context.ble != nullptr) {
        m_transport = bleTransport();
    }
    m_connection = new ConnectionViewModel( // NOLINT(cppcoreguidelines-owning-memory)
        this, m_context.settings);
    // A dismissed error clears the tab's red dot too (S10).
    connect(m_connection, &ConnectionViewModel::errorChanged, this,
            &DeviceTabViewModel::updateState);
    // Plan 0044: sharing this console / the joiner state (children, Qt parent ownership).
    m_remoteShare = new RemoteShareViewModel(this); // NOLINT(cppcoreguidelines-owning-memory)
    m_remoteJoin = new RemoteJoinViewModel(this);   // NOLINT(cppcoreguidelines-owning-memory)

    if (m_context.pages != nullptr) {
        for (const PageDescriptor& page : m_context.pages->pages()) {
            if (page.global) {
                continue;
            }
            QObject* viewModel = page.createViewModel ? page.createViewModel(this) : nullptr;
            if (viewModel != nullptr && viewModel->parent() == nullptr) {
                viewModel->setParent(this); // Qt parent ownership (contract: parent = tab)
            }
            m_pageViewModels.append(viewModel);
            m_pageIds.append(page.id);
            if (m_currentPage.isEmpty()) {
                m_currentPage = page.id;
            }
        }
    }
}

// Plan 0012: the Bluetooth view of the Choose-device page (a child, Qt parent ownership).
void DeviceTabViewModel::createBlePicker() {
    m_blePicker = new BlePickerViewModel( // NOLINT(cppcoreguidelines-owning-memory)
        m_context.ble,
        BlePickerViewModel::Hooks{
            .tabUsingDevice =
                [this](const QString& id) {
                    return m_context.tabUsingBleDevice ? m_context.tabUsingBleDevice(id, this) : -1;
                },
            .otherProgram =
                [this](const QString& id) {
                    return m_context.bleOtherProgram ? m_context.bleOtherProgram(id) : QString();
                },
            .now = {},
            .openUrl = [this](const QUrl& url) { openExternalUrl(url); },
            .devices = {}},
        this);
    connect(m_blePicker, &BlePickerViewModel::tabActivationRequested, this,
            &DeviceTabViewModel::tabActivationRequested);
    connect(m_blePicker, &BlePickerViewModel::connectRequested, this,
            [this](const QString& id, const QString& name, const QString& profileId) {
                if (configureBle(profileId, id, name)) {
                    connectDevice();
                }
            });
}

DeviceTabViewModel::~DeviceTabViewModel() { shutdown(); }

DeviceTabViewModel::State DeviceTabViewModel::state() const { return m_state; }

bool DeviceTabViewModel::hasError() const { return m_hasError; }

QString DeviceTabViewModel::title() const {
    if (!m_choosing && isRemote()) {
        // "CHESTER-M 1234567890 — remote", "Session 482 913 — web Terminal".
        if (m_remoteSession) {
            return m_remoteSession->title();
        }
        const auto code = remote::SessionCode::parse(toStdString(m_remoteCode));
        return tr("Session %1").arg(code ? toQString(code->grouped()) : m_remoteCode);
    }
    const QString name = m_profile != nullptr ? toQString(m_profile->displayName) : QString();
    if (!m_choosing && isBle() && !m_bleId.isEmpty()) {
        // ADR-0081: "CHESTER · CHESTER-ABC123".
        return QStringLiteral("%1 · %2").arg(name, m_bleName.isEmpty() ? m_bleId : m_bleName);
    }
    if (m_choosing || !m_serial) {
        return tr("New device");
    }
    return QStringLiteral("%1 %2 · %3").arg(name, m_targetId).arg(*m_serial);
}

void DeviceTabViewModel::setTransport(const QString& transport) {
    if (transport == m_transport || !m_choosing ||
        (transport != jlinkTransport() && transport != bleTransport()) ||
        (transport == bleTransport() && m_context.ble == nullptr)) {
        return;
    }
    m_transport = transport;
    if (m_context.settings != nullptr && !m_context.settings->readOnly() &&
        m_context.settings->isRegistered(lastTransportKey)) {
        if (const Status stored = m_context.settings->setValue(lastTransportKey, transport);
            !stored) {
            qCWarning(lcGui) << "Could not store" << lastTransportKey;
        }
    }
    transportChanged(); // signal
}

QString DeviceTabViewModel::pageDisabledReason(const QString& pageId) const {
    // Plan 0044: a remote tab's Flash page offers the remote operations (RemoteJoinViewModel).
    if (!isBle() || m_context.pages == nullptr) {
        return {};
    }
    const PageDescriptor* page = m_context.pages->find(pageId);
    if (page == nullptr || !page->needsOperations) {
        return {};
    }
    // ADR-0229.
    return tr("Flashing needs a J-Link. Open a J-Link tab to flash this device.");
}

void DeviceTabViewModel::setCurrentPage(const QString& pageId) {
    if (pageId == m_currentPage || !m_pageIds.contains(pageId) ||
        !pageDisabledReason(pageId).isEmpty()) {
        return;
    }
    m_currentPage = pageId;
    currentPageChanged(); // signal
}

QString DeviceTabViewModel::busyPage() const { return m_busy ? m_busy->pageId : QString(); }

QString DeviceTabViewModel::busyText() const { return m_busy ? m_busy->text : QString(); }

QString DeviceTabViewModel::busyConfirmText() const {
    return m_busy ? m_busy->confirmText : QString();
}

QObject* DeviceTabViewModel::pageViewModel(const QString& pageId) const {
    const auto index = m_pageIds.indexOf(pageId);
    return index >= 0 ? m_pageViewModels.at(index) : nullptr;
}

device::DeviceSession* DeviceTabViewModel::session() const { return m_session.get(); }

device::BleSession* DeviceTabViewModel::bleSession() const { return m_bleSession.get(); }

device::ConsoleSession* DeviceTabViewModel::consoleSession() const {
    if (m_session) {
        return m_session.get();
    }
    if (m_remoteSession) {
        return m_remoteSession.get();
    }
    return m_bleSession.get();
}

remote::RemoteConsoleSession* DeviceTabViewModel::remoteSession() const {
    return m_remoteSession.get();
}

const device::DeviceProfile& DeviceTabViewModel::profile() const { return *m_profile; }

QString DeviceTabViewModel::profileId() const {
    return m_profile != nullptr ? toQString(m_profile->id) : QString();
}

QString DeviceTabViewModel::consoleTargetId() const { return m_targetId; }

const std::shared_ptr<device::ConsoleLog>& DeviceTabViewModel::consoleLog() const {
    return m_consoleLog;
}

bool DeviceTabViewModel::configure(const QString& profileId, const QString& requestedTarget,
                                   std::uint32_t serial, const QString& product) {
    const device::DeviceProfile* profile = device::ProfileRegistry::find(toStdString(profileId));
    if (profile == nullptr ||
        device::findTarget(*profile, toStdString(requestedTarget)) == nullptr || serial == 0) {
        qCWarning(lcGui) << "Tab: unknown configuration" << profileId << requestedTarget << serial;
        return false;
    }
    // ADR-0233 (plan 0033): a J-Link tab always uses the profile's console target; a tab saved
    // with another target (ui/tabs of rc.2) opens on it.
    const QString targetId = toQString(device::consoleTargetId(*profile));
    if (targetId != requestedTarget) {
        qCInfo(lcGui) << "Tab: target" << requestedTarget << "->" << targetId << "(ADR-0233)";
    }
    if ((m_session && m_serial != serial) || m_bleSession || m_remoteSession) {
        releaseSession();
    }
    m_profile = profile;
    m_targetId = targetId;
    m_serial = serial;
    m_product = product;
    m_bleId.clear();
    m_bleName.clear();
    if (m_transport != jlinkTransport()) {
        m_transport = jlinkTransport();
        transportChanged(); // signal
    }
    m_choosing = false;
    m_connection->setProduct(product);
    m_connection->setOpenError(std::nullopt);
    m_connection->loadOptions(m_serial, *device::findTarget(*profile, toStdString(targetId)));
    m_picker->setActive(false);
    m_blePicker->setActive(false);
    titleChanged();         // signal
    configurationChanged(); // signal
    updateState();
    return true;
}

bool DeviceTabViewModel::configureBle(const QString& profileId, const QString& deviceId,
                                      const QString& name) {
    const device::DeviceProfile* profile = device::ProfileRegistry::find(toStdString(profileId));
    const QString id = toQString(ble::canonicalDeviceId(toStdString(deviceId)));
    if (profile == nullptr || !profile->ble || id.isEmpty() ||
        device::findTarget(*profile, profile->ble->consoleTargetId) == nullptr) {
        qCWarning(lcGui) << "Tab: unknown Bluetooth configuration" << profileId << deviceId;
        return false;
    }
    if (m_session || m_remoteSession ||
        (m_bleSession && !ble::sameDeviceId(toStdString(m_bleId), toStdString(id)))) {
        releaseSession();
    }
    m_profile = profile;
    m_targetId = toQString(profile->ble->consoleTargetId);
    m_serial.reset();
    m_product.clear();
    m_bleId = id;
    m_bleName = name;
    if (m_transport != bleTransport()) {
        m_transport = bleTransport();
        transportChanged(); // signal
    }
    m_choosing = false;
    if (!pageDisabledReason(m_currentPage).isEmpty() && !m_pageIds.isEmpty()) {
        // Plan 0012: e.g. Flash: the first page this tab can use (Console).
        for (const QString& pageId : m_pageIds) {
            if (pageDisabledReason(pageId).isEmpty()) {
                m_currentPage = pageId;
                currentPageChanged(); // signal
                break;
            }
        }
    }
    m_connection->setOpenError(std::nullopt);
    m_connection->loadBleOptions(m_bleId, m_bleName);
    m_picker->setActive(false);
    m_blePicker->setActive(false);
    titleChanged();         // signal
    configurationChanged(); // signal
    updateState();
    return true;
}

bool DeviceTabViewModel::configureRemote(const QString& code, const QUrl& relay) {
    const auto parsed = remote::SessionCode::parse(toStdString(code));
    const auto relayUrl = remote::parseRelayUrl(toStdString(relay.toString()));
    const device::DeviceProfile* profile = device::ProfileRegistry::find("chester");
    if (!parsed || !relayUrl || profile == nullptr) {
        qCWarning(lcGui) << "Tab: invalid remote session" << code << relay;
        return false;
    }
    releaseSession();
    m_profile = profile;
    m_targetId = toQString(device::consoleTargetId(*profile));
    m_serial.reset();
    m_product.clear();
    m_bleId.clear();
    m_bleName.clear();
    m_remoteCode = toQString(parsed->text());
    m_remoteRelay = *relayUrl;
    if (m_transport != remoteTransport()) {
        m_transport = remoteTransport();
        transportChanged(); // signal
    }
    m_choosing = false;
    m_lastIdentity.reset();
    m_connection->setOpenError(std::nullopt);
    m_picker->setActive(false);
    m_blePicker->setActive(false);
    titleChanged();         // signal
    configurationChanged(); // signal (a remote tab is never saved in ui/tabs)
    updateState();
    return true;
}

bool DeviceTabViewModel::sessionFailed() const {
    // Stopped with an error = a fatal error (dead). Stopped without one is a deliberate suspend()
    // and still a working session (review S18).
    const device::ConsoleSession* session = consoleSession();
    return session != nullptr && session->connection().state == device::ConnectionState::Stopped &&
           session->connection().error.has_value();
}

Status DeviceTabViewModel::ensureSession(bool connectTarget) {
    if (isRemote()) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         "operations of a remote tab go through the host"));
    }
    if (isBle()) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "operations need a J-Link (Bluetooth tab)"));
    }
    if (m_busy) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "the tab is busy"));
    }
    if (m_session && !sessionFailed()) {
        // M4 (ADR-0069): a probe-only session is promoted when a page needs the target.
        if (connectTarget && m_session->connection().state == device::ConnectionState::ProbeOpen) {
            return promoteSession();
        }
        return {};
    }
    // A stopped session (fatal error) is replaced by a new one (S1).
    releaseSession();
    return openSession(connectTarget);
}

Status DeviceTabViewModel::promoteSession() {
    const device::TargetProfile* target =
        m_profile != nullptr ? device::findTarget(*m_profile, toStdString(m_targetId)) : nullptr;
    const bool attachRtt = target != nullptr && !target->rttTerminal.empty();
    return m_session->startConnection(attachRtt);
}

void DeviceTabViewModel::openWizard(const QString& wizardId) {
    if (m_context.openWizard) {
        m_context.openWizard(wizardId, this);
    }
}

Status DeviceTabViewModel::openSession(bool connectTarget) {
    if (isBle()) {
        return openBleSession();
    }
    if (isRemote()) {
        return openRemoteSession();
    }
    if (m_choosing || !m_serial || m_profile == nullptr) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument, "no probe chosen"));
    }
    const std::string targetId = toStdString(m_targetId);
    const device::TargetProfile* target = device::findTarget(*m_profile, targetId);
    if (target == nullptr) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "unknown target '" + targetId + "'"));
    }
    if (m_context.registry == nullptr) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no session registry"));
    }
    const jlink::LocatorOptions locatorOptions =
        m_context.locatorOptions ? m_context.locatorOptions() : jlink::LocatorOptions{};
    auto location = jlink::locateLibrary(locatorOptions);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }

    const ConnectionOptions& options = m_connection->options();
    device::SessionOptions sessionOptions;
    sessionOptions.profile = m_profile;
    sessionOptions.targetId = target->id;
    sessionOptions.probeSn = m_serial;
    sessionOptions.speedKhz = options.sessionSpeedKhz(); // nullopt = automatic (ADR-0111)
    sessionOptions.latencyMs = options.latencyMs;
    sessionOptions.autoReconnect = options.autoReconnect;
    sessionOptions.resetOnOpen = options.resetOnOpen;
    // A target without an RTT console (CHESTER lte) connects without RTT.
    sessionOptions.attachRtt = !target->rttTerminal.empty();
    sessionOptions.connectTarget = connectTarget; // false: a wizard's probe-only session
    // nullopt = the default console file; an empty path = none.
    if (!options.logToFile) {
        sessionOptions.consoleFile = std::filesystem::path();
    }
    sessionOptions.consoleLog = m_consoleLog;
    // ADR-0114: nRF Util is located with the settings at the time a modem update is requested, so
    // a path changed in Settings (or by the setup wizard) reaches an open session. Called on this
    // (GUI) thread; the SettingsStore outlives the tabs and their sessions.
    sessionOptions.nrfUtilProvider = [settings = m_context.settings] {
        return nrfutil::defaultLocatorOptions(std::nullopt,
                                              settings != nullptr ? settings->settings() : nullptr);
    };

    auto opened =
        device::DeviceSession::open(*m_context.registry, *location, std::move(sessionOptions));
    if (!opened) {
        return std::unexpected(std::move(opened).error());
    }
    m_session = std::move(*opened);
    m_connection->touchRememberedProbe();
    adoptSession(*m_session);
    return {};
}

Status DeviceTabViewModel::openBleSession() {
    if (m_choosing || m_bleId.isEmpty() || m_profile == nullptr || !m_profile->ble) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument, "no device chosen"));
    }
    if (m_context.registry == nullptr || m_context.ble == nullptr) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no Bluetooth service"));
    }
    const ConnectionOptions& options = m_connection->options();
    device::BleSessionOptions sessionOptions{.profile = m_profile,
                                             .deviceId = toStdString(m_bleId),
                                             .deviceName = toStdString(m_bleName),
                                             .autoReconnect = options.autoReconnect,
                                             .consoleFile = std::nullopt,
                                             .consoleLog = m_consoleLog};
    if (!options.logToFile) {
        sessionOptions.consoleFile = std::filesystem::path(); // none
    }
    auto opened = device::BleSession::open(*m_context.registry, m_context.ble->host(),
                                           std::move(sessionOptions));
    if (!opened) {
        return std::unexpected(std::move(opened).error());
    }
    m_bleSession = std::move(*opened);
    m_connection->touchRememberedBleDevice();
    connect(m_bleSession.get(), &device::BleSession::pairingPrompt, this,
            [this] { m_connection->setPairingPending(true); });
    connect(m_bleSession.get(), &device::BleSession::pairingFinished, this,
            [this](const Status&) { m_connection->setPairingPending(false); });
    adoptSession(*m_bleSession);
    return {};
}

Status DeviceTabViewModel::openRemoteSession() {
    if (m_choosing || m_remoteCode.isEmpty()) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument, "no session code"));
    }
    auto code = remote::SessionCode::parse(toStdString(m_remoteCode));
    if (!code) {
        return std::unexpected(std::move(code).error());
    }
    m_remoteSession = std::make_unique<remote::RemoteConsoleSession>(
        remote::JoinOptions{.relay = m_remoteRelay,
                            .code = *code,
                            .appVersion = std::string(buildInfo().version),
                            .relayLimits = {},
                            .transcript = {},
                            .idleNoticeAfter = std::chrono::minutes(30),
                            .consoleLog = m_consoleLog});
    connect(m_remoteSession.get(), &remote::RemoteConsoleSession::transcriptReplaced, this,
            &DeviceTabViewModel::consoleReplaced);
    connect(m_remoteSession.get(), &remote::RemoteConsoleSession::hostHelloReceived, this,
            &DeviceTabViewModel::titleChanged);
    connect(m_remoteSession.get(), &remote::RemoteConsoleSession::joinStateChanged, this,
            &DeviceTabViewModel::titleChanged);
    adoptSession(*m_remoteSession);
    return {};
}

void DeviceTabViewModel::adoptSession(device::ConsoleSession& session) {
    // Plan 0044: never a silent shell query to a remote host's device (no `info show`, so the
    // command tree is never learned either).
    if (!isRemote()) {
        m_queries->setSession(&session);
        // Plan 0028 (D7): `info show` on every Connected (0021 ADR-0166), the console file name.
        m_infoWatcher = std::make_unique<device::DeviceInfoWatcher>(session, *m_queries);
        connect(m_infoWatcher.get(), &device::DeviceInfoWatcher::infoChanged, this,
                &DeviceTabViewModel::deviceInfoChanged);
        connect(m_infoWatcher.get(), &device::DeviceInfoWatcher::answered, this,
                &DeviceTabViewModel::deviceInfoQueried);
    }
    // Review round 1 (R6): the identity the session adopted (`info show` or the boot banner).
    connect(&session, &device::ConsoleSession::firmwareIdentityAdopted, this, [this] {
        if (const device::ConsoleSession* current = consoleSession()) {
            m_lastIdentity = current->adoptedIdentity();
        }
    });
    connect(&session, &device::ConsoleSession::connectionChanged, this,
            &DeviceTabViewModel::onConnectionChanged);
    connect(&session, &device::ConsoleSession::warning, this,
            [this](const Error& warning) { m_connection->setWarning(warning); });
    m_connection->setOpenError(std::nullopt);
    m_connection->setConnection(session.connection());
    sessionChanged(); // signal
    updateState();
}

void DeviceTabViewModel::releaseSession() {
    std::unique_ptr<device::ConsoleSession> session;
    if (m_session) {
        session = std::move(m_session);
    } else if (m_bleSession) {
        session = std::move(m_bleSession);
    } else if (m_remoteSession) {
        session = std::move(m_remoteSession);
    } else {
        return;
    }
    session->disconnect(this);
    m_queries->setSession(nullptr);
    resetInfoWatcher();
    m_connection->setPairingPending(false);
    // J-Link: joins the worker, releases the probe lock and the registry slot. The worker unloads
    // its library copy under the process-wide load lock (ADR-0093): this can wait for a library
    // load of the probe scanner in progress (review S19, see probe_scanner.h). Bluetooth: returns
    // at once, the BLE thread closes the link (BleSession).
    session->close();
    m_retiredSessions.push_back(std::move(session));
    QMetaObject::invokeMethod(this, [this] { m_retiredSessions.clear(); }, Qt::QueuedConnection);
    m_connection->setConnection(std::nullopt);
    sessionChanged(); // signal
    updateState();
}

void DeviceTabViewModel::connectDevice() {
    if (m_choosing || isBusy()) {
        return;
    }
    if (device::ConsoleSession* session = consoleSession()) {
        const device::ConnectionState current = session->connection().state;
        if (current == device::ConnectionState::Connected ||
            current == device::ConnectionState::Connecting) {
            return;
        }
        if (current == device::ConnectionState::ProbeOpen && m_session) {
            // M4 (ADR-0069): promote the probe-only session; the probe lock stays held.
            Status promoted = promoteSession();
            if (promoted) {
                return;
            }
            if (promoted.error().code == ErrorCode::InternalBusy) {
                // An operation (a wizard's Diagnose) runs on the session (review V4): keep it and
                // show why; Connect works again when the operation has finished.
                qCInfo(lcGui) << "Connect refused:" << toQString(promoted.error().detail);
                m_connection->setOpenError(std::move(promoted).error());
                updateState();
                return;
            }
        }
        // A stopped or failed session is replaced by a fresh one (new options apply).
        releaseSession();
    }
    if (Status status = openSession(); !status) {
        qCInfo(lcGui) << "Connect failed:" << toQString(idOf(status.error().code))
                      << toQString(status.error().detail);
        addMarker(tr("— disconnected: %1 —").arg(ConnectionViewModel::shortCause(status.error())));
        m_connection->setOpenError(std::move(status).error());
        updateState();
    }
}

void DeviceTabViewModel::disconnectDevice() {
    if (isBusy()) {
        return;
    }
    const bool hadSession = consoleSession() != nullptr;
    releaseSession();
    m_connection->setOpenError(std::nullopt);
    if (hadSession) {
        addMarker(tr("— disconnected —"));
    }
    updateState();
}

void DeviceTabViewModel::reconnect() {
    if (isBusy() || m_choosing) {
        return;
    }
    device::ConsoleSession* session = consoleSession();
    if (session != nullptr &&
        (session->connection().state == device::ConnectionState::Disconnected ||
         session->connection().state == device::ConnectionState::Connected)) {
        if (const Status status = session->reconnect(); status) {
            return;
        }
    }
    connectDevice();
}

void DeviceTabViewModel::chooseAnotherProbe() {
    if (isBusy()) {
        return;
    }
    handOverToUser(); // ADR-0260: the user changes the device of an AI tab
    releaseSession();
    m_lastIdentity.reset(); // plan 0028 (D14): another device
    m_connection->setOpenError(std::nullopt);
    m_pairing = false;
    m_choosing = true;
    if (isRemote()) { // plan 0044: back to the picker of a local device
        m_transport = jlinkTransport();
        m_remoteCode.clear();
        transportChanged(); // signal
    }
    titleChanged();         // signal
    configurationChanged(); // signal
    updateState();
    m_picker->updateFromScanner();
}

void DeviceTabViewModel::onConnectionChanged(const device::ConnectionEvent& event) {
    m_connection->setConnection(event);
    switch (event.state) {
    case device::ConnectionState::Connected:
        m_connection->setPairingPending(false);
        if (isBle()) {
            addMarker(tr("— connected (Bluetooth) —"));
        } else if (isRemote()) {
            addMarker(tr("— connected (remote) —"));
        } else {
            addMarker(tr("— connected —"));
        }
        consoleAttached(); // signal (`info show`: m_infoWatcher, plan 0028)
        break;
    case device::ConnectionState::Disconnected:
    case device::ConnectionState::Stopped:
        addMarker(
            event.error
                ? tr("— disconnected: %1 —").arg(ConnectionViewModel::shortCause(*event.error))
                : tr("— disconnected —"));
        break;
    case device::ConnectionState::Connecting:
    case device::ConnectionState::ProbeOpen: // a wizard's probe-only session (ADR-0069)
        break;
    }
    updateState();
    // Plan 0044: an ended remote session stays (read-only: its log can still be saved).
    if (event.state == device::ConnectionState::Stopped && event.error && !isRemote()) {
        // A fatally stopped session is released on the next event-loop turn (never inside its own
        // signal): the probe lock, the library copy and the registry slot are freed, so another
        // program (hwio) can use the probe. Its error stays in the ConnectionViewModel (S18).
        const QPointer<device::ConsoleSession> failed(consoleSession());
        QMetaObject::invokeMethod(
            this,
            [this, failed] {
                if (failed && consoleSession() == failed.data() && sessionFailed()) {
                    releaseSession();
                }
            },
            Qt::QueuedConnection);
    }
}

// ---- plan 0012: pairing (ADR-0227) -----------------------------------------------------------

void DeviceTabViewModel::pairDevice() {
    if (!isBle() || m_choosing || m_pairing || isBusy() || m_context.ble == nullptr) {
        return;
    }
    // A session would keep connecting (and fail unpaired) while the system pairs: none meanwhile.
    releaseSession();
    m_connection->setOpenError(std::nullopt);
    const QPointer<DeviceTabViewModel> self(this);
    const Status started = m_context.ble->pair(
        toStdString(m_bleId), this, [this, self](const Status& status, bool alreadyPaired) {
            if (!self) {
                return;
            }
            m_pairing = false;
            m_connection->setPairing(false);
            if (!status) {
                qCInfo(lcGui) << "Pairing failed:" << toQString(idOf(status.error().code));
                m_connection->setOpenError(status.error());
                updateState();
                return;
            }
            qCInfo(lcGui) << (alreadyPaired ? "Already paired" : "Paired");
            connectDevice();
        });
    if (!started) {
        m_connection->setOpenError(started.error());
        updateState();
        return;
    }
    m_pairing = true;
    m_connection->setPairing(true);
    updateState();
}

// ---- 0021: the firmware in the device (ADR-0166)
// --------------------------------------------------

const device::DeviceInfo& DeviceTabViewModel::deviceInfoFields() const noexcept {
    static const device::DeviceInfo none;
    return m_infoWatcher ? m_infoWatcher->info() : none;
}

QVariantMap DeviceTabViewModel::deviceInfo() const {
    const device::DeviceInfo& info = deviceInfoFields();
    const bool valid = info.hasFirmware();
    return {
        {QStringLiteral("valid"), valid},
        {QStringLiteral("fwName"), toQString(info.fwName())},
        {QStringLiteral("fwVersion"), toQString(info.fwVersion())},
        {QStringLiteral("text"),
         valid ? QStringLiteral("%1 %2").arg(toQString(info.fwName()), toQString(info.fwVersion()))
               : QString()}};
}

void DeviceTabViewModel::refreshDeviceInfo() {
    if (m_infoWatcher) {
        m_infoWatcher->refresh();
    }
}

// The session goes away: its watcher too (the device info of the tab is cleared).
void DeviceTabViewModel::resetInfoWatcher() {
    if (!m_infoWatcher) {
        return;
    }
    const bool hadInfo = !m_infoWatcher->info().fields.empty();
    m_infoWatcher.reset();
    if (hadInfo) {
        deviceInfoChanged(); // signal
    }
}

void DeviceTabViewModel::addMarker(const QString& text) {
    m_consoleLog->appendMarker(device::Channel::Shell, toStdString(text));
    // Plan 0012 (ADR-0082): a Bluetooth console has no log channel; its pane stays hidden while
    // the channel is empty, so the markers go to the shell only.
    if (!isBle() && !isRemote()) {
        m_consoleLog->appendMarker(device::Channel::Log, toStdString(text));
    }
}

void DeviceTabViewModel::setBusy(const QString& pageId, QString busyText, QString confirmText,
                                 std::function<void()> cancel) {
    if (m_busy && m_busy->pageId != pageId) {
        qCWarning(lcGui) << "Tab: page" << pageId << "cannot be busy while" << m_busy->pageId
                         << "is busy; ignored";
        return;
    }
    const bool wasBusy = m_busy.has_value();
    m_busy = BusyState{.pageId = pageId,
                       .text = std::move(busyText),
                       .confirmText = std::move(confirmText),
                       .cancel = std::move(cancel),
                       .cancelled = false};
    if (!wasBusy) {
        addMarker(tr("— paused: %1 —").arg(m_busy->text));
    }
    busyChanged(); // signal
    m_connection->refresh();
    updateState();
}

void DeviceTabViewModel::clearBusy(const QString& pageId) {
    if (!m_busy || m_busy->pageId != pageId) {
        return;
    }
    m_busy.reset();
    if (!m_closePending) {
        addMarker(tr("— resumed —"));
    }
    busyChanged(); // signal
    m_connection->refresh();
    updateState();
    if (m_closePending) {
        // Queued: the page may call clearBusy() inside one of the session's signals.
        QMetaObject::invokeMethod(this, [this] { closeReady(); }, Qt::QueuedConnection);
    }
}

void DeviceTabViewModel::cancelBusy(bool close) {
    if (!m_busy) {
        return;
    }
    if (close) {
        m_closePending = true;
    }
    if (!m_busy->cancelled) {
        m_busy->cancelled = true;
        if (m_busy->cancel) {
            // A copy: the callback may call clearBusy(), which resets m_busy.
            const std::function<void()> cancel = m_busy->cancel;
            cancel();
        }
    }
}

void DeviceTabViewModel::setOpenedBy(const QString& client, std::string clientKey) {
    m_openedByKey = std::move(clientKey);
    if (client != m_openedBy) {
        m_openedBy = client;
        aiStateChanged(); // signal
    }
}

void DeviceTabViewModel::handOverToUser() {
    if (m_openedBy.isEmpty()) {
        return;
    }
    qCInfo(lcGui) << "Tab: the user took over the tab opened by" << m_openedBy;
    m_openedBy.clear();
    m_openedByKey.clear();
    aiStateChanged();       // signal
    configurationChanged(); // signal: ui/tabs is saved with this tab now
}

void DeviceTabViewModel::setAiCommandActive(bool active) {
    if (active != m_aiCommandActive) {
        m_aiCommandActive = active;
        aiStateChanged(); // signal
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const): page contract (plan 0004)
void DeviceTabViewModel::showToast(QString text) {
    if (m_context.showToast) {
        m_context.showToast(std::move(text));
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const): page contract (plan 0004)
void DeviceTabViewModel::openExternalUrl(const QUrl& url) {
    if (m_context.openUrl) {
        m_context.openUrl(url);
    } else {
        QDesktopServices::openUrl(url);
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const): page contract (plan 0004)
void DeviceTabViewModel::copyToClipboard(const QString& text, const QString& toast) {
    if (m_context.copyToClipboard) {
        m_context.copyToClipboard(text, toast);
    }
}

void DeviceTabViewModel::shutdown() {
    if (m_picker != nullptr) {
        m_picker->setActive(false);
    }
    if (m_blePicker != nullptr) {
        m_blePicker->setActive(false);
    }
    if (m_session || m_bleSession || m_remoteSession) {
        // Pages drop their pointers on sessionChanged (contract), before the session is deleted.
        std::unique_ptr<device::ConsoleSession> session;
        if (m_session) {
            session = std::move(m_session);
        } else if (m_bleSession) {
            session = std::move(m_bleSession);
        } else {
            session = std::move(m_remoteSession);
        }
        session->disconnect(this);
        m_queries->setSession(nullptr);
        resetInfoWatcher();
        session->close();
        sessionChanged(); // signal
    }
    m_retiredSessions.clear();
}

void DeviceTabViewModel::retranslate() {
    titleChanged(); // signal
    m_connection->refresh();
    m_picker->retranslate();
    m_blePicker->retranslate();
}

void DeviceTabViewModel::updateState() {
    State state = Disconnected;
    bool error = false;
    if (m_choosing) {
        state = ChoosingDevice;
    } else if (m_busy) {
        state = Busy;
    } else if (const device::ConsoleSession* session = consoleSession()) {
        switch (session->connection().state) {
        case device::ConnectionState::Connecting:
        case device::ConnectionState::Disconnected: // auto-reconnecting
            state = Connecting;
            break;
        case device::ConnectionState::Connected:
            state = Connected;
            break;
        case device::ConnectionState::ProbeOpen: // the target is not connected (ADR-0069)
        case device::ConnectionState::Stopped:
            state = Disconnected;
            break;
        }
    }
    if (!m_choosing) {
        error = m_connection->error().valid();
    }
    if (state != m_state || error != m_hasError) {
        m_state = state;
        m_hasError = error;
        stateChanged(); // signal
    }
}

} // namespace hwio::gui
