#pragma once

// McpController - MCP in the GUI (plan/features/0014, ADR-0008, ADR-0101, ADR-0102, ADR-0105,
// ADR-0108, ADR-0241, ADR-0243, ADR-0244): the QML singleton behind Settings › AI assistants (MCP),
// the rail indicator, the consent prompt window and the device bar's Cancel of AI operations.
//
// Lifecycle: mcp/enabled on -> the token file (TokenStore::defaultPath(), loadOrCreate) -> a new
// Registry with the tools (hwio_mcp_tools) -> a new McpServer (activity sink = ActivityModel,
// broker = GuiConsentBroker, the CLI's server options) -> ToolsContext::attachServer -> HttpServer
// on 127.0.0.1:<mcp/port> -> Listening, and one probe refresh. Off, a port change and quit: the
// HTTP server closes, McpServer::stop() cancels the calls; when they have answered (callsDrained,
// at most drainTimeout) the server is detached and destroyed, then a pending start follows. Every
// start builds a new server (McpServer::stop() is final); the ToolsContext, the provider and the
// broker live as long as the controller. Remembered grants are forgotten whenever the server stops
// and when the token is regenerated (ADR-0241).
//
// Active clients: the client keys seen (activity entries, consent requests, at most
// maxTrackedClients) within activeWindow, at least 1 while a call runs; re-evaluated every second
// while listening.
//
// The token is never logged and never put into the activity list; QML gets it only through
// token() (Show) and copyToken().
//
// Shutdown: QCoreApplication::aboutToQuit and the destructor (shutdown(): pending prompts are
// denied, the server stops at once). Ownership: a Qt child of AppViewModel (registerMcp); held by
// QPointer, the application is not touched once its destruction has begun. GUI thread only.

#include "hwio/core/error.h"
#include "mcp/activity_model.h"
#include "mcp/consent_view_model.h"
#include "mcp/gui_consent_broker.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

class QJSEngine;
class QQmlEngine;

namespace hwio::mcp {
class HttpServer;
class McpServer;
} // namespace hwio::mcp

namespace hwio::mcp::tools {
class ToolsContext;
} // namespace hwio::mcp::tools

namespace hwio::gui {

class AppViewModel;
class GuiDeviceProvider;
class SettingsStore;

class McpController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY settingsChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    // The rail indicator's tooltip ("AI assistants: listening on port 47800, 1 client").
    Q_PROPERTY(QString summary READ summary NOTIFY stateChanged)
    Q_PROPERTY(hwio::gui::ErrorInfo error READ error NOTIFY stateChanged)
    // The error is mcp.token_file (the section offers Regenerate token).
    Q_PROPERTY(bool tokenError READ tokenError NOTIFY stateChanged)
    Q_PROPERTY(int activeClients READ activeClients NOTIFY stateChanged)
    Q_PROPERTY(QString address READ address NOTIFY settingsChanged)
    Q_PROPERTY(QString maskedToken READ maskedToken NOTIFY tokenChanged)
    // [{title, note, text (as shown: the token masked)}]
    Q_PROPERTY(QVariantList snippets READ snippets NOTIFY tokenChanged)
    // [{client (with the version), categories, text ("Allowed until disconnect: …")}]
    Q_PROPERTY(QVariantList grants READ grants NOTIFY grantsChanged)
    // [{id, title, hint, allow}] in the settings order (ADR-0101)
    Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged)
    Q_PROPERTY(hwio::gui::ActivityModel* activity READ activity CONSTANT)
    Q_PROPERTY(hwio::gui::ConsentViewModel* consent READ consent CONSTANT)
    Q_PROPERTY(int minPort READ minPort CONSTANT)
    Q_PROPERTY(int maxPort READ maxPort CONSTANT)

public:
    // Unscoped: QML compares McpController.Listening etc. with the int property.
    enum State : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        Off,
        Starting,
        Listening,
        Stopping,
        Error
    };
    Q_ENUM(State)

    static constexpr int defaultPort = 47800; // ADR-0102
    static constexpr int minPortValue = 1024;
    static constexpr int maxPortValue = 65535;
    static constexpr std::chrono::seconds activeWindow{60}; // ADR-0244
    static constexpr std::chrono::seconds drainTimeout{5};  // plan 0014 "Limits"
    static constexpr std::chrono::seconds activeTick{1};    // while listening
    static constexpr std::size_t maxTrackedClients = 64;    // oldest dropped
    static inline const QString enabledKey = QStringLiteral("mcp/enabled");
    static inline const QString portKey = QStringLiteral("mcp/port");

    struct Options {
        GuiConsentBroker::Options consent;
        std::filesystem::path tokenFile; // empty = TokenStore::defaultPath() (tests: their own)
    };

    // mcp/enabled, mcp/port and the consent rules (ADR-0025, ADR-0244).
    static void registerKeys(SettingsStore& settings);

    // Qt parent: `app` (registerMcp). The tools use the CHESTER catalog of the FirmwareServices
    // child of `app` when there is one.
    McpController(AppViewModel& app, Options options);
    ~McpController() override; // shutdown()
    McpController(const McpController&) = delete;
    McpController& operator=(const McpController&) = delete;
    McpController(McpController&&) = delete;
    McpController& operator=(McpController&&) = delete;

    // QML singleton factory: the controller of the AppViewModel created by main() (nullptr and a
    // warning when there is none).
    static McpController* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] bool enabled() const;
    void setEnabled(bool enabled);
    [[nodiscard]] int port() const;
    void setPort(int port);
    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString summary() const;
    [[nodiscard]] ErrorInfo error() const;
    [[nodiscard]] bool tokenError() const;
    [[nodiscard]] int activeClients() const noexcept { return m_activeClients; }
    [[nodiscard]] QString address() const;
    [[nodiscard]] QString maskedToken() const;
    [[nodiscard]] QVariantList snippets() const;
    [[nodiscard]] QVariantList grants() const;
    [[nodiscard]] QVariantList rules() const;
    [[nodiscard]] ActivityModel* activity() const noexcept { return m_activity; }
    [[nodiscard]] ConsentViewModel* consent() const noexcept { return m_consentView; }
    [[nodiscard]] static int minPort() noexcept { return minPortValue; }
    [[nodiscard]] static int maxPort() noexcept { return maxPortValue; }

    // Show: the full token ("" when there is none yet).
    Q_INVOKABLE [[nodiscard]] QString token();
    Q_INVOKABLE void copyToken();
    // A new token; the old one stops working at once, remembered grants are forgotten.
    Q_INVOKABLE void regenerateToken();
    // Starts again after an error (Retry).
    Q_INVOKABLE void retry();
    // "ask" | "allow" for a category id ("flash", "erase", "memory-write", "uicr", "connect").
    Q_INVOKABLE [[nodiscard]] QString consentRule(const QString& category) const;
    Q_INVOKABLE void setConsentRule(const QString& category, bool allow);
    Q_INVOKABLE void revokeGrant(int row);
    Q_INVOKABLE void copySnippet(int row);
    // The device bar's Cancel of a running AI operation (ADR-0244).
    Q_INVOKABLE static void cancelOperation(hwio::gui::DeviceTabViewModel* tab);

    // Stops the server at once, denies pending prompts and forgets grants (quit). Idempotent.
    void shutdown();

    // Tests and integration (C1): the bound port while listening (0 otherwise), the provider.
    [[nodiscard]] int listeningPort() const;
    [[nodiscard]] GuiDeviceProvider& provider() const noexcept { return *m_provider; }
    [[nodiscard]] GuiConsentBroker& broker() const noexcept { return *m_consent; }
    // The running server (nullptr while off; tests hand calls to it directly).
    [[nodiscard]] hwio::mcp::McpServer* serverForTests() const noexcept { return m_server.get(); }
    // The client keys counted as active now (sorted; tests).
    [[nodiscard]] std::size_t trackedClients() const noexcept { return m_seen.size(); }

signals:
    void settingsChanged();
    void stateChanged();
    void tokenChanged();
    void grantsChanged();
    void rulesChanged();

private:
    void apply();
    void start();
    void stopServer(bool restart);
    void finishStop();
    // Destroys a server that never served (a failed listen).
    void teardown();
    void setState(State state, std::optional<hwio::Error> error = std::nullopt);
    void clientSeen(const QString& clientKey);
    void updateActiveClients();
    [[nodiscard]] std::filesystem::path tokenFile() const;
    // The device text of the consent prompt and its tab (ConsentViewModel::DeviceDescriber).
    [[nodiscard]] QString describeDevice(const std::string& id, int& tabIndex) const;

    QPointer<AppViewModel> m_app;
    QPointer<SettingsStore> m_settings; // AppViewModel's child (deleted before this object)
    Options m_options;
    State m_state = Off;
    std::optional<hwio::Error> m_error;
    std::string m_token; // never logged
    bool m_startAfterStop = false;
    bool m_shutDown = false;
    int m_activeClients = 0;
    // Client key -> last seen (at most maxTrackedClients).
    std::map<QString, std::chrono::steady_clock::time_point> m_seen;
    QTimer m_activeTimer;
    QTimer m_drainTimer;
    ActivityModel* m_activity = nullptr;       // child
    ConsentViewModel* m_consentView = nullptr; // child
    // Destroyed in reverse order: the HTTP server, the server, the tools, the broker, the provider.
    std::unique_ptr<GuiDeviceProvider> m_provider;
    std::unique_ptr<GuiConsentBroker> m_consent;
    std::unique_ptr<hwio::mcp::tools::ToolsContext> m_tools;
    std::unique_ptr<hwio::mcp::McpServer> m_server; // only while on
    std::unique_ptr<hwio::mcp::HttpServer> m_http;  // only while on
};

} // namespace hwio::gui
