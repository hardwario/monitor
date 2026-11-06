#include "mcp/mcp_controller.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/http_server.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/server.h"
#include "hwio/mcp/token.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/register_tools.h"
#include "hwio/mcp/tools/tools_context.h"
#include "mcp/activity_model.h"
#include "mcp/client_snippets.h"
#include "mcp/consent_view_model.h"
#include "mcp/gui_consent_broker.h"
#include "mcp/gui_device_provider.h"
#include "pages/firmware_services.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/probe_firmware_view_model.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QMetaType>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

using hwio::mcp::ConsentCategory;

QString categoryTitle(ConsentCategory category) {
    switch (category) {
    case ConsentCategory::Flash:
        return McpController::tr("Flash firmware (flash, LTE, raw flash)");
    case ConsentCategory::Erase:
        return McpController::tr("Erase and recover");
    case ConsentCategory::MemoryWrite:
        return McpController::tr("Write memory");
    case ConsentCategory::Uicr:
        return McpController::tr("Write UICR / factory data");
    case ConsentCategory::Connect:
        return McpController::tr("Connect devices (opens a tab)");
    }
    return {};
}

std::optional<ConsentCategory> categoryOf(const QString& id) {
    return hwio::mcp::consentCategoryFromId(toStdString(id));
}

} // namespace

void McpController::registerKeys(SettingsStore& settings) {
    const std::array keys{
        SettingKey{
            .path = enabledKey, .type = QMetaType::Bool, .defaultValue = false, .validator = {}},
        SettingKey{.path = portKey,
                   .type = QMetaType::Int,
                   .defaultValue = defaultPort,
                   .validator = validators::intRange(minPortValue, maxPortValue)},
    };
    settings.registerKeys(keys);
    GuiConsentBroker::registerKeys(settings);
}

McpController::McpController(AppViewModel& app, Options options)
    : QObject(&app), m_app(&app), m_settings(app.settingsStore()), m_options(std::move(options)),
      // Qt parent ownership: the activity list is a child (QML reads it as a property).
      m_activity(new ActivityModel(this)), // NOLINT(cppcoreguidelines-owning-memory)
      m_provider(std::make_unique<GuiDeviceProvider>(app)),
      m_consent(std::make_unique<GuiConsentBroker>(*app.settingsStore(), m_options.consent)) {
    // The CHESTER catalog of the Flash page's services for `flash catalog_id` (none -> the tool
    // answers usage.invalid_argument, as in 0013).
    firmware::FirmwareCatalog* catalog = nullptr;
    if (auto* services = app.findChild<FirmwareServices*>(QString(), Qt::FindDirectChildrenOnly);
        services != nullptr) {
        if (const device::DeviceProfile* chester = device::ProfileRegistry::find("chester");
            chester != nullptr) {
            catalog = services->catalog(*chester);
        }
    }
    m_tools = std::make_unique<hwio::mcp::tools::ToolsContext>(*m_provider, catalog);
    // Plan 0036 (ADR-0105, ADR-0132): the J-Link firmware update is blocked ("Used by an AI
    // assistant") while an AI operation runs on a tab of the probe. Removed by the destructor.
    if (ProbeFirmwareViewModel* firmware = app.probeFirmware(); firmware != nullptr) {
        firmware->setAiHoldsProbe(
            [this](std::uint32_t serial) { return m_provider->aiOperationRuns(serial); });
    }

    // Qt parent ownership: the prompt's view model is a child.
    m_consentView = new ConsentViewModel( // NOLINT(cppcoreguidelines-owning-memory)
        *m_consent,
        [this](std::string_view id) {
            int tab = -1;
            QString text = describeDevice(std::string(id), tab);
            return ConsentViewModel::DeviceDescription{.text = std::move(text), .tabIndex = tab};
        },
        [this](int tabIndex) {
            if (!m_app.isNull()) {
                m_app->setCurrentTab(tabIndex);
                m_app->activateWindow();
            }
        },
        this);

    m_activeTimer.setInterval(activeTick);
    connect(&m_activeTimer, &QTimer::timeout, this, &McpController::updateActiveClients);
    m_drainTimer.setSingleShot(true);
    m_drainTimer.setInterval(drainTimeout);
    connect(&m_drainTimer, &QTimer::timeout, this, &McpController::finishStop);

    connect(m_activity, &ActivityModel::entryRecorded, this, &McpController::clientSeen);
    connect(m_consent.get(), &GuiConsentBroker::clientSeen, this, &McpController::clientSeen);
    connect(m_consent.get(), &GuiConsentBroker::grantsChanged, this, &McpController::grantsChanged);
    connect(m_consent.get(), &GuiConsentBroker::rulesChanged, this, &McpController::rulesChanged);
    connect(m_consent.get(), &GuiConsentBroker::withdrawn, this, [this] {
        if (!m_app.isNull()) {
            m_app->showToast(tr("The AI assistant cancelled the request"));
        }
    });
    connect(m_settings, &SettingsStore::valueChanged, this, [this](const QString& key) {
        if (key == enabledKey || key == portKey) {
            settingsChanged(); // signal
            if (key == portKey) {
                tokenChanged(); // signal: the snippets contain the port
            }
            apply();
        }
    });
    // ADR-0243: the server stops before the application's objects go away.
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
            &McpController::shutdown);
    // The token for the snippets (read only; created when the server starts).
    if (auto stored = hwio::mcp::TokenStore(tokenFile()).load(); stored) {
        m_token = std::move(*stored);
    }
    apply();
}

McpController::~McpController() {
    if (!m_app.isNull()) {
        if (ProbeFirmwareViewModel* firmware = m_app->probeFirmware(); firmware != nullptr) {
            firmware->setAiHoldsProbe({});
        }
    }
    shutdown();
}

McpController* McpController::create(QQmlEngine* /*qmlEngine*/, QJSEngine* /*jsEngine*/) {
    auto* app = QCoreApplication::instance()->findChild<AppViewModel*>(QString(),
                                                                       Qt::FindDirectChildrenOnly);
    auto* controller = app != nullptr
                           ? app->findChild<McpController*>(QString(), Qt::FindDirectChildrenOnly)
                           : nullptr;
    if (controller == nullptr) {
        qCWarning(lcGui) << "McpController: not registered (registerMcp)";
        return nullptr;
    }
    QJSEngine::setObjectOwnership(controller, QJSEngine::CppOwnership);
    return controller;
}

std::filesystem::path McpController::tokenFile() const {
    return m_options.tokenFile.empty() ? hwio::mcp::TokenStore::defaultPath() : m_options.tokenFile;
}

// ---- settings ---------------------------------------------------------------------------------

bool McpController::enabled() const {
    return !m_settings.isNull() && m_settings->value(enabledKey).toBool();
}

void McpController::setEnabled(bool enabled) {
    if (m_settings.isNull() || enabled == this->enabled()) {
        return;
    }
    if (const Status stored = m_settings->setValue(enabledKey, enabled); !stored) {
        qCWarning(lcGui) << "MCP: mcp/enabled not stored:" << toQString(stored.error().detail);
        settingsChanged(); // signal: QML shows the stored value again
    }
}

int McpController::port() const {
    return m_settings.isNull() ? defaultPort : m_settings->value(portKey).toInt();
}

void McpController::setPort(int port) {
    if (m_settings.isNull() || port == this->port()) {
        return;
    }
    if (const Status stored = m_settings->setValue(portKey, port); !stored) {
        qCInfo(lcGui) << "MCP: port" << port << "not stored:" << toQString(stored.error().detail);
        settingsChanged(); // signal
    }
}

QString McpController::consentRule(const QString& category) const {
    const std::optional<ConsentCategory> parsed = categoryOf(category);
    return parsed && m_consent->alwaysAllowed(*parsed) ? QStringLiteral("allow")
                                                       : QStringLiteral("ask");
}

void McpController::setConsentRule(const QString& category, bool allow) {
    const std::optional<ConsentCategory> parsed = categoryOf(category);
    if (!parsed) {
        return;
    }
    if (const Status stored = m_consent->setAlwaysAllowed(*parsed, allow); !stored) {
        qCWarning(lcGui) << "MCP: consent rule" << category
                         << "not stored:" << toQString(stored.error().detail);
        rulesChanged(); // signal
    }
}

QVariantList McpController::rules() const {
    QVariantList rows;
    for (const ConsentCategory category : GuiConsentBroker::categories) {
        const std::string_view id = hwio::mcp::consentCategoryId(category);
        rows.append(QVariantMap{{QStringLiteral("id"),
                                 QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()))},
                                {QStringLiteral("title"), categoryTitle(category)},
                                // ADR-0164.
                                {QStringLiteral("hint"),
                                 category == ConsentCategory::MemoryWrite
                                     ? tr("Writing memory can make the device run arbitrary code.")
                                     : QString()},
                                {QStringLiteral("allow"), m_consent->alwaysAllowed(category)}});
    }
    return rows;
}

QVariantList McpController::grants() const {
    QVariantList rows;
    for (const GuiConsentBroker::Grant& grant : m_consent->grants()) {
        QStringList titles;
        for (const ConsentCategory category : GuiConsentBroker::categories) {
            if (grant.categories.testFlag(category)) {
                titles.append(categoryTitle(category));
            }
        }
        // ADR-0241: the name and the version when given.
        QString client = grant.clientName.isEmpty() ? tr("An AI assistant") : grant.clientName;
        if (!grant.clientVersion.isEmpty()) {
            client = QStringLiteral("%1 %2").arg(client, grant.clientVersion);
        }
        const QString categories = titles.join(QStringLiteral(", "));
        rows.append(QVariantMap{{QStringLiteral("client"), client},
                                {QStringLiteral("categories"), categories},
                                {QStringLiteral("text"),
                                 tr("Allowed until disconnect: %1 — %2").arg(client, categories)}});
    }
    return rows;
}

void McpController::revokeGrant(int row) {
    if (row >= 0) {
        m_consent->revoke(static_cast<std::size_t>(row));
    }
}

// ---- token and snippets -----------------------------------------------------------------------

QString McpController::address() const { return mcp_snippets::address(port()); }

QString McpController::maskedToken() const {
    return m_token.empty() ? QString() : mcp_snippets::masked(toQString(m_token));
}

QString McpController::token() {
    if (m_token.empty()) {
        if (auto stored = hwio::mcp::TokenStore(tokenFile()).load(); stored) {
            m_token = std::move(*stored);
            tokenChanged(); // signal
        }
    }
    return toQString(m_token);
}

void McpController::copyToken() {
    const QString value = token();
    if (!value.isEmpty() && !m_app.isNull()) {
        m_app->copyToClipboard(value, tr("Copied."));
    }
}

void McpController::regenerateToken() {
    auto regenerated = hwio::mcp::TokenStore(tokenFile()).regenerate();
    if (!regenerated) {
        qCWarning(lcGui).noquote() << "MCP: the token could not be regenerated:"
                                   << toQString(idOf(regenerated.error().code));
        setState(m_server ? m_state : Error, std::move(regenerated).error());
        return;
    }
    m_token = std::move(*regenerated);
    if (m_http) {
        m_http->setToken(m_token); // refused at once for new requests (ADR-0102)
    }
    m_consent->forgetAll(); // ADR-0241
    qCInfo(lcGui) << "MCP: token regenerated";
    tokenChanged(); // signal
    if (m_server && m_state == Listening) {
        // Review M2: the sessions, streams and calls of the old token end: pending prompts are
        // denied and the server restarts (its calls are cancelled).
        m_consent->denyAll();
        stopServer(true);
    } else if (m_state == Error && !m_server) {
        apply(); // a token file error is solved
    }
}

QVariantList McpController::snippets() const {
    QVariantList rows;
    const auto made = mcp_snippets::make(port(), toQString(m_token), mcp_snippets::installedCli());
    for (const mcp_snippets::Snippet& snippet : made) {
        QString title;
        QString note;
        switch (snippet.kind) {
        case mcp_snippets::Kind::ClaudeCodeHttp:
            title = tr("Claude Code (HTTP)");
            break;
        case mcp_snippets::Kind::McpJson:
            title = tr("Claude Code (.mcp.json, token from HWIO_MCP_TOKEN)");
            break;
        case mcp_snippets::Kind::ClaudeDesktop:
            title = tr("Claude Desktop (runs the hwio command separately)");
            note = tr("Runs separately from this window and cannot use a probe that is open here.");
            break;
        }
        rows.append(QVariantMap{{QStringLiteral("title"), title},
                                {QStringLiteral("note"), note},
                                {QStringLiteral("text"), snippet.shown}});
    }
    return rows;
}

void McpController::copySnippet(int row) {
    const auto made = mcp_snippets::make(port(), toQString(m_token), mcp_snippets::installedCli());
    if (row < 0 || std::cmp_greater_equal(row, made.size()) || m_app.isNull()) {
        return;
    }
    const mcp_snippets::Snippet& snippet = made.at(static_cast<std::size_t>(row));
    m_app->copyToClipboard(snippet.text, snippet.kind == mcp_snippets::Kind::ClaudeCodeHttp
                                             ? tr("Copied. Run it in a terminal.")
                                             : tr("Copied."));
}

// ---- state ------------------------------------------------------------------------------------

QString McpController::stateText() const {
    switch (m_state) {
    case Off:
        return tr("Off");
    case Starting:
        return tr("Starting…");
    case Listening:
        return m_activeClients == 0 ? tr("Listening")
                                    : tr("Listening · %n client(s)", "", m_activeClients);
    case Stopping:
        return tr("Stopping…");
    case Error:
        if (m_error && m_error->code == ErrorCode::McpPortInUse) {
            return tr("Port %1 is in use by another program — choose another port").arg(port());
        }
        return m_error ? toQString(m_error->message) : QString();
    }
    return {};
}

QString McpController::summary() const {
    switch (m_state) {
    case Listening:
        return m_activeClients == 0
                   ? tr("AI assistants: listening on port %1").arg(listeningPort())
                   : tr("AI assistants: listening on port %1, %n client(s)", "", m_activeClients)
                         .arg(listeningPort());
    case Off:
    case Starting:
    case Stopping:
    case Error:
        break;
    }
    return tr("AI assistants: %1").arg(stateText());
}

ErrorInfo McpController::error() const {
    return m_state == Error && m_error ? ErrorInfo(*m_error) : ErrorInfo();
}

bool McpController::tokenError() const {
    return m_state == Error && m_error && m_error->code == ErrorCode::McpTokenFile;
}

int McpController::listeningPort() const {
    return m_http && m_state == Listening ? static_cast<int>(m_http->port()) : 0;
}

void McpController::setState(State state, std::optional<hwio::Error> error) {
    if (state != Error) {
        error.reset();
    }
    const bool same = state == m_state && !error && !m_error;
    m_state = state;
    m_error = std::move(error);
    if (m_state == Listening) {
        m_activeTimer.start();
    } else {
        m_activeTimer.stop();
        m_seen.clear();
        m_activeClients = 0;
    }
    if (!same) {
        stateChanged(); // signal
    }
}

void McpController::retry() {
    if (m_state == Error && !m_server) {
        apply();
    }
}

// ---- lifecycle --------------------------------------------------------------------------------

void McpController::apply() {
    if (m_shutDown) {
        return;
    }
    const bool want = enabled();
    if (m_state == Stopping) {
        m_startAfterStop = want;
        return;
    }
    if (m_server) {
        if (!want) {
            stopServer(false);
        } else if (m_http && std::cmp_not_equal(m_http->port(), port())) {
            stopServer(true); // a port change restarts the server
        }
        return;
    }
    if (want) {
        start();
    } else if (m_state != Off) {
        setState(Off);
    }
}

void McpController::start() {
    setState(Starting);
    auto token = hwio::mcp::TokenStore(tokenFile()).loadOrCreate();
    if (!token) {
        qCWarning(lcGui).noquote() << "MCP: token file:" << toQString(idOf(token.error().code));
        setState(Error, std::move(token).error());
        return;
    }
    if (*token != m_token) {
        m_token = std::move(*token);
        tokenChanged(); // signal
    }
    hwio::mcp::Registry registry;
    if (const Status registered = hwio::mcp::tools::registerTools(registry, *m_tools);
        !registered) {
        setState(Error, registered.error());
        return;
    }
    m_server = std::make_unique<hwio::mcp::McpServer>(std::move(registry), *m_consent, *m_activity,
                                                      hwio::mcp::ServerOptions{});
    m_tools->attachServer(*m_server);
    connect(m_server.get(), &hwio::mcp::McpServer::clientsChanged, this,
            &McpController::updateActiveClients);
    m_http = std::make_unique<hwio::mcp::HttpServer>(
        *m_server,
        hwio::mcp::HttpServerOptions{.port = static_cast<quint16>(port()), .token = m_token});
    if (Status listening = m_http->listen(); !listening) {
        qCWarning(lcGui).noquote() << "MCP: listen on port" << port()
                                   << "failed:" << toQString(idOf(listening.error().code));
        teardown();
        setState(Error, std::move(listening).error());
        return;
    }
    qCInfo(lcGui) << "MCP: listening on 127.0.0.1 port" << m_http->port();
    setState(Listening);
    m_provider->refresh([](const Status& refreshed) {
        if (!refreshed) {
            qCInfo(lcGui).noquote()
                << "MCP: probe enumeration failed:" << toQString(idOf(refreshed.error().code));
        }
    });
}

void McpController::teardown() {
    m_tools->detachServer();
    m_http.reset();
    if (m_server) {
        m_server->stop();
        m_server.reset();
    }
}

void McpController::stopServer(bool restart) {
    m_startAfterStop = restart;
    setState(Stopping);
    m_http->close();
    m_consent->forgetAll(); // ADR-0241: the server stops, its port changes
    connect(m_server.get(), &hwio::mcp::McpServer::callsDrained, this, &McpController::finishStop,
            Qt::QueuedConnection);
    m_drainTimer.start();
    qCInfo(lcGui) << "MCP: stopping";
    m_server->stop(); // callsDrained follows (at once when no call runs)
}

void McpController::finishStop() {
    if (m_state != Stopping) {
        return;
    }
    m_drainTimer.stop();
    if (m_server && m_server->activeCalls() > 0) {
        qCWarning(lcGui) << "MCP:" << m_server->activeCalls()
                         << "calls did not answer in time; the server is destroyed";
    }
    teardown();
    qCInfo(lcGui) << "MCP: stopped";
    setState(Off);
    const bool restart = std::exchange(m_startAfterStop, false);
    if (restart || enabled()) {
        apply();
    }
}

void McpController::shutdown() {
    if (m_shutDown) {
        return;
    }
    m_shutDown = true;
    m_activeTimer.stop();
    m_drainTimer.stop();
    m_consent->denyAll(); // ADR-0108: closing the window denies pending prompts
    if (m_http) {
        m_http->close();
    }
    teardown();
    m_consent->forgetAll();
    m_state = Off;
    m_error.reset();
}

void McpController::cancelOperation(DeviceTabViewModel* tab) {
    if (tab != nullptr &&
        tab->busyPage() ==
            QLatin1StringView(GuiDeviceProvider::busyPageId.data(),
                              static_cast<qsizetype>(GuiDeviceProvider::busyPageId.size()))) {
        qCInfo(lcGui) << "MCP: the user cancelled the AI operation";
        tab->cancelBusy(false);
    }
}

// ---- active clients ---------------------------------------------------------------------------

void McpController::clientSeen(const QString& clientKey) {
    if (m_state != Listening) {
        return;
    }
    if (!m_seen.contains(clientKey) && m_seen.size() >= maxTrackedClients) {
        const auto oldest = std::ranges::min_element(
            m_seen, [](const auto& a, const auto& b) { return a.second < b.second; });
        m_seen.erase(oldest);
    }
    m_seen.insert_or_assign(clientKey, std::chrono::steady_clock::now());
    updateActiveClients();
}

void McpController::updateActiveClients() {
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(m_seen, [now](const auto& entry) { return now - entry.second > activeWindow; });
    int count = static_cast<int>(m_seen.size());
    if (count == 0 && m_server && m_server->activeCalls() > 0) {
        count = 1; // a call of a client not seen yet (its entry follows when it ends)
    }
    if (count != m_activeClients) {
        m_activeClients = count;
        stateChanged(); // signal
    }
}

// ---- consent prompt ---------------------------------------------------------------------------

QString McpController::describeDevice(const std::string& id, int& tabIndex) const {
    tabIndex = -1;
    if (DeviceTabViewModel* tab = m_provider->tabFor(id); tab != nullptr && !m_app.isNull()) {
        tabIndex = m_app->tabs()->indexOf(tab);
        return tr("%1 (tab %2)").arg(tab->title(), QString::number(tabIndex + 1));
    }
    for (const hwio::mcp::tools::DeviceEntry& entry : m_provider->list()) {
        if (entry.id == id) {
            return toQString(entry.displayName);
        }
    }
    return toQString(id);
}

} // namespace hwio::gui
