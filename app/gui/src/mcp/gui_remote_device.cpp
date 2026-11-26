#include "mcp/gui_remote_device.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_redaction.h"
#include "hwio/device/profile.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/remote_hidden_view.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/tunnel.h"
#include "mcp/client_name.h"
#include "viewmodels/remote_join_view_model.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtCore/qttranslation.h>

#include <algorithm>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

namespace tools = hwio::mcp::tools;
namespace tunnel = remote::tunnel;

// A made-up catalog firmware ID: hostOffers() only checks that one is given.
constexpr std::string_view anyFirmwareId = "00000000000000000000000000000000";
// The target of the console and of the redaction (ADR-0437).
constexpr std::string_view consoleTarget = "app";

// ADR-0434: an error of the remote session without anything of the session (its detail or a text
// with arguments may name the session code or the relay URL): the error id's own texts.
Error freshError(const Error& error, std::string_view what) {
    return makeError(error.code, std::string(what));
}

bool offered(const tunnel::HostHello& hello, const tunnel::OpRequest& request) {
    if (!remote::RemoteConsoleSession::hostOffers(hello, request)) {
        return false;
    }
    // A host without `targets` (an older Monitor) runs the kinds of its opKinds only.
    return hello.targets.has_value() || tunnel::supports(hello, request.kind);
}

tools::RemoteJoin joinOf(remote::JoinState state) {
    switch (state) {
    case remote::JoinState::Connecting:
        return tools::RemoteJoin::Connecting;
    case remote::JoinState::Waiting:
        return tools::RemoteJoin::Waiting;
    case remote::JoinState::Allowed:
        return tools::RemoteJoin::Allowed;
    case remote::JoinState::Ended:
        return tools::RemoteJoin::Ended;
    }
    return tools::RemoteJoin::Ended;
}

// D8 (ADR-0434): flash (catalog, prepared), reset; the LTE update separately.
tools::RemoteTargetOffer targetOffer(const tunnel::HostHello& hello, const tunnel::OpTarget& target,
                                     const std::vector<tunnel::PreparedFirmware>& prepared) {
    tools::RemoteTargetOffer offer{.target = std::string(target.id()), .kinds = {}};
    offer.catalog = offered(hello, tunnel::OpRequest{.id = "x",
                                                     .kind = tunnel::OpKind::Flash,
                                                     .target = target,
                                                     .firmwareId = std::string(anyFirmwareId)});
    for (const tunnel::PreparedFirmware& firmware : prepared) {
        if (offer.prepared.size() >= tools::maxRemotePrepared) {
            break;
        }
        if (offered(hello, tunnel::OpRequest{.id = "x",
                                             .kind = tunnel::OpKind::Flash,
                                             .target = target,
                                             .firmwareId = std::nullopt,
                                             .prepared = firmware.id})) {
            offer.prepared.push_back(tools::RemotePreparedFirmware{
                .id = firmware.id, .name = firmware.name, .version = firmware.version});
        }
    }
    if (offer.catalog || !offer.prepared.empty()) {
        offer.kinds.push_back(tools::RemoteOperationKind::Flash);
    }
    if (offered(hello,
                tunnel::OpRequest{.id = "x", .kind = tunnel::OpKind::Reset, .target = target})) {
        offer.kinds.push_back(tools::RemoteOperationKind::Reset);
    }
    return offer;
}

std::vector<tools::RemoteTargetOffer> offersOf(const tunnel::HostHello& hello) {
    std::vector<tools::RemoteTargetOffer> offers;
    const auto add = [&offers](tools::RemoteTargetOffer offer) {
        if (!offer.kinds.empty() && offers.size() < tools::maxRemoteOffers) {
            offers.push_back(std::move(offer));
        }
    };
    if (hello.targets) {
        for (const tunnel::HostTarget& target : *hello.targets) {
            if (const auto id = tunnel::OpTarget::fromId(target.id)) {
                add(targetOffer(hello, *id, target.prepared));
            }
        }
    } else {
        // An older host: the CHESTER targets app and lte (catalog firmware on app only).
        tools::RemoteTargetOffer app = targetOffer(hello, tunnel::OpTarget::App, {});
        add(std::move(app));
        tools::RemoteTargetOffer lte = targetOffer(hello, tunnel::OpTarget::Lte, {});
        std::erase(lte.kinds, tools::RemoteOperationKind::Flash);
        lte.catalog = false;
        add(std::move(lte));
    }
    if (offered(hello, tunnel::OpRequest{.id = "x",
                                         .kind = tunnel::OpKind::LteUpdate,
                                         .target = tunnel::OpTarget::Lte})) {
        add(tools::RemoteTargetOffer{.target = {},
                                     .kinds = {tools::RemoteOperationKind::LteUpdate}});
    }
    return offers;
}

} // namespace

GuiRemoteDevice::GuiRemoteDevice(remote::RemoteConsoleSession& session, RemoteJoinViewModel& join)
    : IRemoteDevice(&join), m_session(&session), m_join(&join) {
    rebuildView();
    connect(&session, &remote::RemoteConsoleSession::hostHelloReceived, this, [this] {
        rebuildView();
        emit infoChanged();
    });
    connect(&session, &remote::RemoteConsoleSession::joinStateChanged, this, [this] {
        // ADR-0436: the grant ends with the session.
        if (!m_session.isNull() && m_session->joinState() == remote::JoinState::Ended) {
            revokeAi({});
        }
        emit infoChanged();
    });
    connect(&session, &remote::RemoteConsoleSession::lineRefused, this,
            [this](const QString& code) {
                const auto known = errorCodeFromId(toStdString(code));
                emit lineRefused(known ? code : QStringLiteral("remote.command_refused"));
            });
}

GuiRemoteDevice::~GuiRemoteDevice() = default;

void GuiRemoteDevice::rebuildView() {
    const std::string code = m_session.isNull() ? std::string() : m_session->code().text();
    m_view = std::make_unique<tools::RemoteHiddenView>(redaction(), code);
}

tools::RemoteInfo GuiRemoteDevice::info() const {
    tools::RemoteInfo info;
    if (m_session.isNull()) {
        info.join = tools::RemoteJoin::Ended;
        return info;
    }
    info.join = joinOf(m_session->joinState());
    const std::optional<tunnel::HostHello>& hello = m_session->hostHello();
    if (!hello) {
        return info; // the web Terminal or the mobile app: the console only
    }
    info.host = tools::RemoteHost::Monitor;
    info.commands = hello->console;
    info.operations = offersOf(*hello);
    return info;
}

Status GuiRemoteDevice::sendLine(std::string line, const hwio::mcp::RequestContext& /*who*/) {
    if (m_session.isNull()) {
        return std::unexpected(makeError(ErrorCode::RemoteHostEnded, "remote session"));
    }
    if (Status sent = m_session->sendLine(std::move(line)); !sent) {
        return std::unexpected(freshError(sent.error(), "remote line"));
    }
    return {};
}

Result<tools::RemoteOperationId>
GuiRemoteDevice::requestOperation(const tools::RemoteOperationRequest& request,
                                  const hwio::mcp::RequestContext& /*who*/,
                                  tools::RemoteOperationCallbacks callbacks) {
    if (m_session.isNull()) {
        return std::unexpected(makeError(ErrorCode::RemoteHostEnded, "remote session"));
    }
    auto requested = m_join->requestAiOperation(request, std::move(callbacks));
    if (!requested) {
        return std::unexpected(freshError(requested.error(), "remote operation"));
    }
    return *requested;
}

void GuiRemoteDevice::detachOperation(tools::RemoteOperationId id) {
    m_join->detachAiOperation(id);
}

device::ConsoleRedaction GuiRemoteDevice::redaction() const {
    // ADR-0437: the profile of the host's hello; CHESTER when there is no hello (the web Terminal
    // or the mobile app as host, or before the hello). Independent of the console rules the
    // joiner's screen uses.
    const std::optional<tunnel::HostHello> hello =
        m_session.isNull() ? std::nullopt : m_session->hostHello();
    return {remote::RemoteConsoleSession::profileFor(hello), consoleTarget};
}

std::string GuiRemoteDevice::hiddenText(const device::ConsoleLine& line) const {
    return m_view->text(line);
}

bool GuiRemoteDevice::aiAllowed(std::string_view clientKey) const {
    return std::ranges::any_of(m_grants,
                               [clientKey](const Grant& g) { return g.clientKey == clientKey; });
}

void GuiRemoteDevice::allowAi(const hwio::mcp::RequestContext& who) {
    if (aiAllowed(who.clientKey)) {
        return;
    }
    if (m_session.isNull() || m_session->joinState() == remote::JoinState::Ended) {
        return; // the session ended while support answered
    }
    if (m_grants.size() >= maxAiClients) {
        const std::string oldest = m_grants.front().clientKey;
        revokeAi(oldest);
    }
    QString name = clientDisplayText(who.client.name);
    if (name.isEmpty()) {
        name = QCoreApplication::translate("hwio::gui::GuiRemoteDevice", "An AI assistant");
    }
    m_grants.push_back(Grant{.clientKey = who.clientKey, .name = std::move(name)});
    qCInfo(lcGui) << "MCP: support allowed an AI client for a remote session";
    emit aiClientsChanged();
}

QStringList GuiRemoteDevice::aiClients() const {
    QStringList names;
    for (const Grant& grant : m_grants) {
        if (!names.contains(grant.name)) {
            names.append(grant.name);
        }
    }
    return names;
}

void GuiRemoteDevice::revokeAi(std::string_view clientKey) {
    const auto removed = std::erase_if(m_grants, [clientKey](const Grant& g) {
        return clientKey.empty() || g.clientKey == clientKey;
    });
    if (removed == 0) {
        return;
    }
    qCInfo(lcGui) << "MCP: the AI grant of a remote session ended";
    emit aiRevoked(toQString(clientKey));
    emit aiClientsChanged();
}

QString GuiRemoteDevice::displayName(bool english) const {
    const std::optional<tunnel::HostHello> hello =
        m_session.isNull() ? std::nullopt : m_session->hostHello();
    if (hello) {
        // A Monitor host: its device, else its profile's product name ("CHESTER").
        QString device = toQString(hello->device.name);
        if (device.isEmpty()) {
            device = toQString(remote::RemoteConsoleSession::profileFor(hello).displayName);
        }
        if (!hello->device.serial.empty()) {
            device += QLatin1Char(' ') + toQString(hello->device.serial);
        }
        // The same form as the tab title (RemoteConsoleSession::title()).
        const char* const remoteSuffix = QT_TRANSLATE_NOOP("hwio::remote", "%1 — remote");
        const QString text = english ? QString::fromUtf8(remoteSuffix)
                                     : QCoreApplication::translate("hwio::remote", remoteSuffix);
        return QString::fromStdString(m_view->scrub(toStdString(text.arg(device))));
    }
    const char* const fallback = QT_TRANSLATE_NOOP("hwio::gui::GuiRemoteDevice",
                                                   "Remote session (web Terminal or mobile app)");
    return english ? QString::fromUtf8(fallback)
                   : QCoreApplication::translate("hwio::gui::GuiRemoteDevice", fallback);
}

std::string GuiRemoteDevice::profileId() const {
    const std::optional<tunnel::HostHello> hello =
        m_session.isNull() ? std::nullopt : m_session->hostHello();
    return std::string(remote::RemoteConsoleSession::profileFor(hello).id);
}

} // namespace hwio::gui
