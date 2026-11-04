#pragma once

// Fixture of the MCP GUI tests (plan/features/0014): an AppViewModel with the Console and Flash
// pages and MCP registered with test options (the token file in the test directory, a short
// consent timeout when asked), a free port for mcp/port, and a minimal in-process HTTP/1.1 client
// for the controller's server (QTcpSocket on the test's event loop: no extra threads, so it also
// runs under ThreadSanitizer).

#include "gui_test_support.h"
#include "hwio/core/result.h"
#include "hwio/hw/ble/ble_api.h"
#include "mcp/mcp_controller.h"
#include "mcp/mcp_registration.h"
#include "pages/console_page.h"
#include "pages/flash_settings_pages.h"
#include "viewmodels/app_view_model.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace hwio::gui::test {

// A port that was free a moment ago (the server binds it right after).
inline int freePort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress(QHostAddress::LocalHost), 0)) {
        return 0;
    }
    const int port = probe.serverPort();
    probe.close();
    return port;
}

struct HttpResponse {
    int status = 0;
    std::map<QByteArray, QByteArray> headers; // lower-case names
    QByteArray body;                          // de-chunked
    bool complete = false;

    [[nodiscard]] QByteArray header(const char* name) const {
        const auto it = headers.find(QByteArray(name));
        return it == headers.end() ? QByteArray() : it->second;
    }
    // The JSON-RPC message: the body, or the last "data:" event of a stream.
    [[nodiscard]] QJsonObject json() const {
        if (header("content-type").startsWith("text/event-stream")) {
            QJsonObject last;
            for (const QByteArray& line : body.split('\n')) {
                if (line.startsWith("data: ")) {
                    last = QJsonDocument::fromJson(line.mid(6)).object();
                }
            }
            return last;
        }
        return QJsonDocument::fromJson(body).object();
    }
    [[nodiscard]] QJsonObject result() const {
        return json().value(QStringLiteral("result")).toObject();
    }
    [[nodiscard]] bool isError() const {
        return result().value(QStringLiteral("isError")).toBool();
    }
    [[nodiscard]] QJsonObject structured() const {
        return result().value(QStringLiteral("structuredContent")).toObject();
    }
    // The hwio error id of a tool error ("" when none).
    [[nodiscard]] QString errorCode() const {
        return structured()
            .value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("code"))
            .toString();
    }
};

// Parses one response at the start of `data`; complete = the whole body is there.
inline HttpResponse parseHttp(const QByteArray& data) {
    HttpResponse response;
    const qsizetype headEnd = data.indexOf("\r\n\r\n");
    if (headEnd < 0) {
        return response;
    }
    const QList<QByteArray> lines = data.left(headEnd).split('\n');
    response.status = lines.front().split(' ').value(1).toInt();
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        response.headers[line.left(colon).toLower()] = line.mid(colon + 1).trimmed();
    }
    qsizetype pos = headEnd + 4;
    if (response.header("transfer-encoding") == "chunked") {
        while (true) {
            const qsizetype lineEnd = data.indexOf("\r\n", pos);
            if (lineEnd < 0) {
                break;
            }
            bool ok = false;
            const qsizetype size = data.mid(pos, lineEnd - pos).toLongLong(&ok, 16);
            if (!ok || data.size() < lineEnd + 2 + size + 2) {
                break;
            }
            pos = lineEnd + 2 + size + 2;
            if (size == 0) {
                response.complete = true;
                break;
            }
            response.body += data.mid(lineEnd + 2, size);
        }
    } else {
        const qsizetype length = response.header("content-length").toLongLong();
        response.body = data.mid(pos, length);
        response.complete = response.body.size() == length;
    }
    return response;
}

// One request on its own connection; the response is read on the event loop.
class HttpCall {
public:
    HttpCall(int port, const QByteArray& request) {
        QObject::connect(&m_socket, &QTcpSocket::readyRead,
                         [this] { m_received += m_socket.readAll(); });
        m_socket.connectToHost(QHostAddress(QHostAddress::LocalHost), static_cast<quint16>(port));
        m_socket.write(request);
    }

    [[nodiscard]] bool done() const {
        return parseHttp(m_received).complete ||
               (m_socket.state() == QAbstractSocket::UnconnectedState && !m_received.isEmpty());
    }
    [[nodiscard]] HttpResponse response() const { return parseHttp(m_received); }
    // Waits for the response (status 0 when none arrived in time).
    HttpResponse wait(std::chrono::milliseconds timeout = std::chrono::seconds(30)) {
        (void)waitFor([this] { return done(); }, timeout);
        return response();
    }
    void abort() { m_socket.abort(); }

private:
    QTcpSocket m_socket;
    QByteArray m_received;
};

// A modern (2026-07-28) tools/call request; `overrides` replace or (null) remove headers.
inline QByteArray toolRequest(int port, const std::string& token, const QString& tool,
                              const QJsonObject& arguments,
                              const QString& client = QStringLiteral("Claude Code"),
                              const std::map<QByteArray, QByteArray>& overrides = {}) {
    const QJsonObject meta{
        {QStringLiteral("io.modelcontextprotocol/protocolVersion"), QStringLiteral("2026-07-28")},
        {QStringLiteral("io.modelcontextprotocol/clientCapabilities"), QJsonObject()},
        {QStringLiteral("io.modelcontextprotocol/clientInfo"),
         QJsonObject{{QStringLiteral("name"), client},
                     {QStringLiteral("version"), QStringLiteral("1")}}}};
    const QByteArray body =
        QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                  {QStringLiteral("id"), 1},
                                  {QStringLiteral("method"), QStringLiteral("tools/call")},
                                  {QStringLiteral("params"),
                                   QJsonObject{{QStringLiteral("name"), tool},
                                               {QStringLiteral("arguments"), arguments},
                                               {QStringLiteral("_meta"), meta}}}})
            .toJson(QJsonDocument::Compact);
    std::map<QByteArray, QByteArray> headers{
        {"Host", "127.0.0.1:" + QByteArray::number(port)},
        {"Authorization", "Bearer " + QByteArray::fromStdString(token)},
        {"Content-Type", "application/json"},
        {"Accept", "application/json, text/event-stream"},
        {"Content-Length", QByteArray::number(body.size())},
        {"MCP-Protocol-Version", "2026-07-28"},
        {"Mcp-Method", "tools/call"},
        {"Mcp-Name", tool.toUtf8()},
    };
    for (const auto& [name, value] : overrides) {
        if (value.isNull()) {
            headers.erase(name);
        } else {
            headers[name] = value;
        }
    }
    QByteArray text = "POST /mcp HTTP/1.1\r\n";
    for (const auto& [name, value] : headers) {
        text += name + ": " + value + "\r\n";
    }
    return text + "\r\n" + body;
}

class McpGuiTest : public GuiTest {
protected:
    void TearDown() override {
        destroyApp();
        GuiTest::TearDown();
    }

    // Stops the server and destroys the application (no-op without one).
    void destroyApp() {
        if (m_app) {
            if (McpController* mcp = controller()) {
                mcp->shutdown();
            }
            m_app.reset();
        }
    }
    // Step D1 tests: the application gets a BleService (before app()).
    void setBluetooth(bool on) { m_bluetooth = on; }

    // The application with MCP; `options.tokenFile` defaults to the test directory.
    AppViewModel& app(McpController::Options options = {}) {
        if (!m_app) {
            if (options.tokenFile.empty()) {
                options.tokenFile = root() / "mcp-token";
            }
            m_app = std::make_unique<AppViewModel>(
                AppOptions{.settings = openSettings(),
                           .registry = registryOptions(),
                           .scanInterval = std::chrono::milliseconds(200),
                           .registerExtensions =
                               [options](AppViewModel& application) {
                                   registerConsolePage(application);
                                   registerFlashAndSettingsPages(application);
                                   (void)registerMcp(application, options);
                               },
                           .crashDirectory = {},
                           .bluetooth = m_bluetooth,
                           .bleApiFactory = hwio::ble::createBleApi});
        }
        return *m_app;
    }

    [[nodiscard]] McpController* controller() const {
        return m_app ? m_app->findChild<McpController*>(QString(), Qt::FindDirectChildrenOnly)
                     : nullptr;
    }
    McpController& mcp() { return *controller(); }

    // Turns the server on at a free port and waits until it listens.
    void listen() {
        const int port = freePort();
        ASSERT_NE(port, 0);
        ASSERT_TRUE(app().settingsStore()->setValue(McpController::portKey, port));
        mcp().setEnabled(true);
        ASSERT_TRUE(waitFor([this] { return mcp().state() == McpController::Listening; }))
            << mcp().stateText().toStdString();
    }

    [[nodiscard]] std::string token() { return mcp().token().toStdString(); }

    // The prompt's buttons as a user clicks them: Allow waits until the prompt is armed (700 ms).
    void deny() { mcp().consent()->deny(mcp().consent()->promptId()); }
    void allowOnce() {
        ASSERT_TRUE(waitFor([this] { return mcp().consent()->armed(); }));
        mcp().consent()->allowOnce(mcp().consent()->promptId());
    }
    void allowForClient() {
        ASSERT_TRUE(waitFor([this] { return mcp().consent()->armed(); }));
        mcp().consent()->allowForClient(mcp().consent()->promptId());
    }

    // One tool call over HTTP (synchronous; the event loop runs while waiting).
    HttpResponse call(const QString& tool, const QJsonObject& arguments = {},
                      const QString& client = QStringLiteral("Claude Code")) {
        HttpCall http(mcp().listeningPort(),
                      toolRequest(mcp().listeningPort(), token(), tool, arguments, client));
        return http.wait();
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    bool m_bluetooth = false; // the BleService (step D1 tests)
};

} // namespace hwio::gui::test
