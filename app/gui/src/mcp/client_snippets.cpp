#include "mcp/client_snippets.h"

#include "hwio/core/version.h"
#include "install_paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtEnvironmentVariables>

#include <vector>

namespace hwio::gui::mcp_snippets {

namespace {

const QString serverName = QStringLiteral("hwio");

QString json(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed();
}

} // namespace

QString address(int port) { return QStringLiteral("http://127.0.0.1:%1/mcp").arg(port); }

QString masked(const QString& token) { return {token.size(), QChar(0x2022)}; }

CliInvocation installedCli() {
    if (buildInfo().packageKind == "appimage") {
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        return {.program = appImage.isEmpty() ? QStringLiteral("<AppImage>") : appImage,
                .arguments = {QStringLiteral("hwio")}};
    }
    return {.program = cliCommand(), .arguments = {}};
}

std::vector<Snippet> make(int port, const QString& token, const CliInvocation& cli) {
    const QString value = token.isEmpty() ? QStringLiteral("<token>") : token;
    const QString url = address(port);
    std::vector<Snippet> snippets;

    // Claude Code, HTTP: the token is embedded (shown masked).
    const QString command =
        QStringLiteral(
            "claude mcp add --transport http %1 %2 --header \"Authorization: Bearer %3\"")
            .arg(serverName, url);
    snippets.push_back(Snippet{.kind = Kind::ClaudeCodeHttp,
                               .text = command.arg(value),
                               .shown = command.arg(token.isEmpty() ? value : masked(token))});

    // .mcp.json: the token stays in the environment.
    const QJsonObject mcpJson{
        {QStringLiteral("mcpServers"),
         QJsonObject{{serverName,
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("http")},
                                  {QStringLiteral("url"), url},
                                  {QStringLiteral("headers"),
                                   QJsonObject{{QStringLiteral("Authorization"),
                                                QStringLiteral("Bearer ${HWIO_MCP_TOKEN}")}}}}}}}};
    const QString mcpJsonText = json(mcpJson);
    snippets.push_back(Snippet{.kind = Kind::McpJson, .text = mcpJsonText, .shown = mcpJsonText});

    // Claude Desktop: stdio, `hwio mcp` as a separate process.
    QJsonArray arguments;
    for (const QString& argument : cli.arguments) {
        arguments.append(argument);
    }
    arguments.append(QStringLiteral("mcp"));
    const QJsonObject desktop{
        {QStringLiteral("mcpServers"),
         QJsonObject{{serverName, QJsonObject{{QStringLiteral("command"), cli.program},
                                              {QStringLiteral("args"), arguments}}}}}};
    const QString desktopText = json(desktop);
    snippets.push_back(
        Snippet{.kind = Kind::ClaudeDesktop, .text = desktopText, .shown = desktopText});
    return snippets;
}

} // namespace hwio::gui::mcp_snippets
