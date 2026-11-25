#pragma once

// GuiRemoteDevice - a joined remote support tab as the MCP tools see it (plan/features/0062,
// ADR-0433..0438): the IRemoteDevice over the tab's RemoteConsoleSession and the tab's ONE
// RemoteOperationClient (owned by RemoteJoinViewModel, so that operationPending() stays the single
// source of "an operation is running" and plan 0061's Leave-button rule holds).
//
//   info()             join state and the host's hello: host kind, `console`, and what may be
//                      requested - flash (catalog / prepared), reset, lteUpdate only (D8), each
//                      only when RemoteConsoleSession::hostOffers() accepts it (and, for a host
//                      without targets, its opKinds: tunnel::supports())
//   sendLine()         RemoteConsoleSession::sendLine: a plain line, exactly as a person's
//                      (ADR-0435)
//   requestOperation() RemoteJoinViewModel::requestAiOperation (the tab's client, `questions` for
//                      the LTE update as for a person's request, ADR-0368 / D13)
//   detachOperation()  stop waiting (D12): the tab keeps following the operation (no abandon())
//   redaction()        device::ConsoleRedaction of RemoteConsoleSession::profileFor(hostHello()),
//                      target "app" (ADR-0437)
//   hiddenText()       RemoteHiddenView of that redaction and the session code
//
// Errors returned by sendLine() / requestOperation() are built fresh from the error id: never a
// detail or a text of the session (it may name the session code or the relay URL).
//
// Support's grant (ADR-0436): the clients support allowed for this tab ("Allow for this
// session"), at most maxAiClients (the oldest is revoked when another one is allowed). It ends with
// revokeAi(): Stop AI in the banner, the consent broker's clientGone / forgetAll, the setting
// turned off (GuiDeviceProvider), the session ended (here), and the tab leaving the session or
// joining again (RemoteJoinViewModel revokes before it destroys this object).
//
// Ownership: RemoteJoinViewModel (std::unique_ptr, one object per session of the tab; its Qt parent
// is the RemoteJoinViewModel only for the object tree, the unique_ptr deletes it first). GUI thread
// only.

#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_redaction.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/remote_hidden_view.h"

#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::remote {
class RemoteConsoleSession;
} // namespace hwio::remote

namespace hwio::gui {

class RemoteJoinViewModel;

class GuiRemoteDevice final : public hwio::mcp::tools::IRemoteDevice {
    Q_OBJECT

public:
    // The clients one tab keeps a grant for (a client key per AI client, ADR-0241).
    static constexpr std::size_t maxAiClients = 64;

    GuiRemoteDevice(remote::RemoteConsoleSession& session, RemoteJoinViewModel& join);
    ~GuiRemoteDevice() override;
    GuiRemoteDevice(const GuiRemoteDevice&) = delete;
    GuiRemoteDevice& operator=(const GuiRemoteDevice&) = delete;
    GuiRemoteDevice(GuiRemoteDevice&&) = delete;
    GuiRemoteDevice& operator=(GuiRemoteDevice&&) = delete;

    // hwio::mcp::tools::IRemoteDevice
    [[nodiscard]] hwio::mcp::tools::RemoteInfo info() const override;
    [[nodiscard]] hwio::Status sendLine(std::string line,
                                        const hwio::mcp::RequestContext& who) override;
    [[nodiscard]] hwio::Result<hwio::mcp::tools::RemoteOperationId>
    requestOperation(const hwio::mcp::tools::RemoteOperationRequest& request,
                     const hwio::mcp::RequestContext& who,
                     hwio::mcp::tools::RemoteOperationCallbacks callbacks) override;
    void detachOperation(hwio::mcp::tools::RemoteOperationId id) override;
    [[nodiscard]] hwio::device::ConsoleRedaction redaction() const override;
    [[nodiscard]] std::string hiddenText(const hwio::device::ConsoleLine& line) const override;
    [[nodiscard]] bool aiAllowed(std::string_view clientKey) const override;
    void allowAi(const hwio::mcp::RequestContext& who) override;

    // The display names of the allowed clients, in the order they were allowed.
    [[nodiscard]] QStringList aiClients() const;
    // Ends the grant of `clientKey` ("" = of every client): aiRevoked() (running calls of the
    // client end `cancelled`), aiClientsChanged().
    void revokeAi(std::string_view clientKey);

    // The name an AI client and support's prompt see (never the session code, ADR-0434): the
    // device of the host's hello ("CHESTER-M 2159018247 — remote"; its profile's product name when
    // the hello names no device), else "Remote session (web Terminal or mobile app)". `english`:
    // for MCP (ADR-0374), else in the GUI language.
    [[nodiscard]] QString displayName(bool english) const;
    // The profile id of the host's hello (CHESTER without one).
    [[nodiscard]] std::string profileId() const;

signals:
    void aiClientsChanged();

private:
    void rebuildView();

    struct Grant {
        std::string clientKey;
        QString name; // sanitised (client_name.h)
    };

    QPointer<remote::RemoteConsoleSession> m_session;
    RemoteJoinViewModel* m_join; // non-owning: the owner
    std::unique_ptr<hwio::mcp::tools::RemoteHiddenView> m_view;
    std::vector<Grant> m_grants; // <= maxAiClients
};

} // namespace hwio::gui
