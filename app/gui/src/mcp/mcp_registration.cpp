#include "mcp/mcp_registration.h"

#include "mcp/mcp_controller.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/page_registry.h"

#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtCore/qttranslation.h>

#include <utility>

namespace hwio::gui {

McpController& registerMcp(AppViewModel& app, McpController::Options options) {
    McpController::registerKeys(*app.settingsStore());
    app.pageRegistry().addSettingsSection(SettingsSectionDescriptor{
        .id = QStringLiteral("mcp"),
        .titleSourceText = QT_TRANSLATE_NOOP("Pages", "AI assistants (MCP)"),
        .qml = QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/settings/McpSection.qml")),
        .order = 50});
    // Qt parent ownership: the AppViewModel (constructor).
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* controller = new McpController(app, std::move(options));
    return *controller;
}

} // namespace hwio::gui
