#include "hwio/remote/session_code.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/core/settings.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QtCore/qnamespace.h>
#include <QtLogging>

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::remote {

namespace {

Error invalidCode(std::string detail) {
    return makeError(ErrorCode::RemoteCodeInvalid, std::move(detail));
}

Error invalidRelay(std::string detail) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message =
        QCoreApplication::translate("hwio::remote", "The support server address is not valid.")
            .toStdString();
    error.cause =
        QCoreApplication::translate(
            "hwio::remote", "The address must start with https:// and name a server, for example "
                            "https://terminal.hardwario.com.")
            .toStdString();
    error.suggestion =
        QCoreApplication::translate("hwio::remote",
                                    "Correct the support server address, or clear it to use the "
                                    "default server.")
            .toStdString();
    return error;
}

bool isLoopbackHost(const QString& host) {
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

} // namespace

SessionCode SessionCode::generate() {
    // bounded(low, high): low inclusive, high exclusive.
    return SessionCode(QRandomGenerator::system()->bounded(minHostCode, maxCode + 1));
}

Result<SessionCode> SessionCode::parse(std::string_view text) {
    std::uint32_t value = 0;
    int digits = 0;
    for (const char c : text) {
        if (c == ' ') {
            continue; // "482 913"
        }
        if (c < '0' || c > '9') {
            return std::unexpected(invalidCode(std::format("not a digit in the code: {:?}", text)));
        }
        if (++digits > 6) {
            return std::unexpected(invalidCode("more than 6 digits"));
        }
        value = (value * 10U) + static_cast<std::uint32_t>(c - '0');
    }
    if (digits == 0) {
        return std::unexpected(invalidCode("no digits"));
    }
    return fromValue(value);
}

Result<SessionCode> SessionCode::fromValue(std::uint32_t value) {
    if (value == 0 || value > maxCode) {
        return std::unexpected(invalidCode(std::format("code {} out of range", value)));
    }
    return SessionCode(value);
}

std::string SessionCode::text() const { return std::to_string(m_value); }

std::string SessionCode::grouped() const {
    std::string digits = text();
    if (digits.size() == 6) {
        digits.insert(3, 1, ' ');
    }
    return digits;
}

QString SessionCode::socketNamespace() const { return QStringLiteral("/%1").arg(m_value); }

Result<QUrl> parseRelayUrl(std::string_view text) {
    QUrl url(toQString(text), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() ||
        url.hasFragment()) {
        return std::unexpected(invalidRelay(std::format("not a valid relay URL: {:?}", text)));
    }
    const QString scheme = url.scheme().toLower();
    bool allowed = scheme == QStringLiteral("https");
#ifdef HWIO_TEST_OVERRIDES
    // Test builds only: the fake relay of the tests (plain WebSocket on a loopback port).
    allowed = allowed || (scheme == QStringLiteral("http") && isLoopbackHost(url.host()));
#else
    static_cast<void>(&isLoopbackHost);
#endif
    if (!allowed) {
        return std::unexpected(invalidRelay(std::format("relay URL must be https: {:?}", text)));
    }
    url.setScheme(scheme);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    url.setPath(path);
    return url;
}

QUrl configuredRelayUrl(const Settings* settings) {
    const std::string configured = settings != nullptr ? settings->get(relayUrlSetting) : "";
    if (!configured.empty()) {
        if (auto url = parseRelayUrl(configured)) {
            return *url;
        }
        qCWarning(log::lcRemote).noquote()
            << "Invalid setting" << toQString(relayUrlSetting.path) << "- using the default relay";
    }
    return *parseRelayUrl(defaultRelayUrl);
}

QUrl engineIoUrl(const QUrl& relay) {
    QUrl url = relay;
    url.setScheme(relay.scheme() == QStringLiteral("http") ? QStringLiteral("ws")
                                                           : QStringLiteral("wss"));
    url.setPath(relay.path() + QStringLiteral("/socket.io/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("EIO"), QStringLiteral("4"));
    query.addQueryItem(QStringLiteral("transport"), QStringLiteral("websocket"));
    url.setQuery(query);
    return url;
}

QUrl sessionLink(const QUrl& relay, const SessionCode& code) {
    QUrl url = relay;
    url.setPath(relay.path() + QStringLiteral("/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("session"), toQString(code.text()));
    url.setQuery(query);
    return url;
}

Result<JoinTarget> parseJoinInput(std::string_view text) {
    std::string_view trimmed = text;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
        trimmed.remove_prefix(1);
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' ||
                                trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.remove_suffix(1);
    }
    if (!trimmed.contains("://")) {
        auto code = SessionCode::parse(trimmed);
        if (!code) {
            return std::unexpected(std::move(code).error());
        }
        return JoinTarget{.code = *code, .relay = std::nullopt};
    }
    const QUrl link(toQString(trimmed), QUrl::StrictMode);
    const QUrlQuery query(link);
    if (!link.isValid() || !query.hasQueryItem(QStringLiteral("session"))) {
        return std::unexpected(invalidCode("the link has no ?session= code"));
    }
    auto code = SessionCode::parse(toStdString(query.queryItemValue(QStringLiteral("session"))));
    if (!code) {
        return std::unexpected(std::move(code).error());
    }
    QUrl relayPart = link;
    relayPart.setQuery(QString());
    relayPart.setFragment(QString());
    auto relay = parseRelayUrl(toStdString(relayPart.toString(QUrl::FullyEncoded)));
    if (!relay) {
        return std::unexpected(std::move(relay).error());
    }
    return JoinTarget{.code = *code, .relay = *relay};
}

bool sameRelay(const QUrl& a, const QUrl& b) {
    return a.scheme().compare(b.scheme(), Qt::CaseInsensitive) == 0 &&
           a.host().compare(b.host(), Qt::CaseInsensitive) == 0 &&
           a.port(a.scheme() == QStringLiteral("http") ? 80 : 443) ==
               b.port(b.scheme() == QStringLiteral("http") ? 80 : 443) &&
           a.path() == b.path();
}

} // namespace hwio::remote
