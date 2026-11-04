#pragma once

// Client configuration snippets of Settings › AI assistants (plan/features/0014, ADR-0106,
// ADR-0244): Claude Code over HTTP (the token embedded), Claude Code's .mcp.json (the token from
// $HWIO_MCP_TOKEN) and Claude Desktop over stdio (`hwio mcp`, a separate process). Pure functions;
// the texts of the commands are not translated (they are typed into a terminal / config file).

#include <QString>
#include <QStringList>
#include <QtTypes>

#include <vector>

namespace hwio::gui::mcp_snippets {

// How Claude Desktop starts the CLI: the program and the arguments before "mcp" (AppImage:
// program = the AppImage, arguments = {"hwio"}).
struct CliInvocation {
    QString program;
    QStringList arguments;
};

enum class Kind : quint8 { ClaudeCodeHttp, McpJson, ClaudeDesktop };

struct Snippet {
    Kind kind = Kind::ClaudeCodeHttp;
    QString text;  // the full text (Copy)
    QString shown; // as displayed: the token masked
};

// "http://127.0.0.1:<port>/mcp"
[[nodiscard]] QString address(int port);
// The token shown masked: one "•" per character (43).
[[nodiscard]] QString masked(const QString& token);
// This installation's CLI (install_paths: cliCommand(), the AppImage case split into program +
// "hwio").
[[nodiscard]] CliInvocation installedCli();

// The snippets in display order. `token` may be empty (not created yet): "<token>" is used.
[[nodiscard]] std::vector<Snippet> make(int port, const QString& token, const CliInvocation& cli);

} // namespace hwio::gui::mcp_snippets
