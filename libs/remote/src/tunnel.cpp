#include "hwio/remote/tunnel.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/core/text_sanitizer.h"
#include "hwio/device/command_tree.h"
#include "hwio/firmware/firmware_cache.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QString>
#include <QStringLiteral>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace hwio::remote::tunnel {

namespace {

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

std::unexpected<Error> bad(std::string detail) {
    return std::unexpected(
        makeError(ErrorCode::RemoteProtocolError, "tunnel: " + std::move(detail)));
}

QString q(std::string_view text) { return toQString(text); }

// A required text field: a string of at most maxTextBytes UTF-8 bytes, sanitised.
Result<std::string> text(const QJsonObject& object, const char* key, bool required = true) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined() && !required) {
        return std::string();
    }
    if (!value.isString()) {
        return bad(std::format("{} is not a string", key));
    }
    std::string utf8 = toStdString(value.toString());
    if (utf8.size() > maxTextBytes) {
        return bad(std::format("{} is too long", key));
    }
    return sanitizeDeviceText(utf8);
}

Result<std::string> id(const QJsonObject& object) {
    const QJsonValue value = object.value(QStringLiteral("id"));
    if (!value.isString() || !validId(toStdString(value.toString()))) {
        return bad("invalid operation id");
    }
    return toStdString(value.toString());
}

Result<std::string> errorCode(const QJsonObject& object) {
    const QJsonValue value = object.value(QStringLiteral("code"));
    if (!value.isString()) {
        return bad("error code is not a string");
    }
    std::string code = toStdString(value.toString());
    if (code.size() > 64 || !errorCodeFromId(code)) {
        return bad("unknown error code");
    }
    return code;
}

// A non-negative integer up to 2^53.
Result<std::uint64_t> count(const QJsonObject& object, const char* key) {
    const QJsonValue value = object.value(QLatin1String(key));
    const double number = value.toDouble(-1.0);
    if (!value.isDouble() || number < 0.0 || number > 9007199254740992.0 ||
        std::floor(number) != number) {
        return bad(std::format("{} is not a count", key));
    }
    return static_cast<std::uint64_t>(number);
}

QJsonObject envelope(std::string_view type) {
    QJsonObject data;
    data.insert(QStringLiteral("v"), version);
    data.insert(QStringLiteral("type"), q(type));
    return data;
}

Result<QJsonObject> openEnvelope(const QJsonValue& value) {
    if (!value.isObject()) {
        return bad("message is not an object");
    }
    QJsonObject data = value.toObject();
    if (data.value(QStringLiteral("v")).toInt(-1) != version ||
        !data.value(QStringLiteral("v")).isDouble()) {
        return bad("unsupported version");
    }
    if (!data.value(QStringLiteral("type")).isString()) {
        return bad("type is not a string");
    }
    return data;
}

Result<HostMessage> decodeHostHello(const QJsonObject& data) {
    HostHello hello;
    auto app = text(data, "app");
    auto ver = text(data, "version");
    if (!app || !ver) {
        return bad("invalid hello");
    }
    if (*app != appId) {
        return bad("hello from an unknown application");
    }
    const QJsonValue deviceValue = data.value(QStringLiteral("device"));
    const QJsonValue ops = data.value(QStringLiteral("ops"));
    if (!deviceValue.isObject() || !ops.isBool()) {
        return bad("invalid hello");
    }
    const QJsonObject device = deviceValue.toObject();
    auto name = text(device, "name");
    auto serial = text(device, "serial");
    auto transport = text(device, "transport");
    auto firmware = text(device, "firmware");
    if (!name || !serial || !transport || !firmware) {
        return bad("invalid hello device");
    }
    // ADR-0363: the kinds the host runs; absent (an earlier host) = the ADR-0322 kinds.
    const QJsonValue kinds = data.value(QStringLiteral("opKinds"));
    if (kinds.isUndefined()) {
        if (ops.toBool()) {
            hello.opKinds = {OpKind::Flash, OpKind::Erase, OpKind::Recover, OpKind::Reset};
        }
    } else if (!kinds.isArray() || std::cmp_greater(kinds.toArray().size(), maxOpKinds)) {
        return bad("invalid opKinds");
    } else {
        for (const QJsonValue& kindValue : kinds.toArray()) {
            if (!kindValue.isString() || kindValue.toString().size() > 32) {
                return bad("invalid opKinds");
            }
            const auto kind = kindFromId(toStdString(kindValue.toString()));
            if (kind && ops.toBool() &&
                std::ranges::find(hello.opKinds, *kind) == hello.opKinds.end()) {
                hello.opKinds.push_back(*kind); // unknown kinds (a later version) are skipped
            }
        }
    }
    hello.app = *app;
    hello.version = *ver;
    hello.device = DeviceInfo{
        .name = *name, .serial = *serial, .transport = *transport, .firmware = *firmware};
    hello.ops = ops.toBool();
    return hello;
}

Result<HostMessage> decodeOpDenied(const QJsonObject& data, const std::string& messageId) {
    auto code = errorCode(data);
    if (!code) {
        return std::unexpected(std::move(code).error());
    }
    return OpDenied{.id = messageId, .code = *code};
}

Result<HostMessage> decodeOpProgress(const QJsonObject& data, const std::string& messageId) {
    auto phase = text(data, "phase", false);
    auto step = count(data, "step");
    auto steps = count(data, "steps");
    auto done = count(data, "done");
    auto total = count(data, "total");
    if (!phase || !step || !steps || !done || !total || *step < 1 || *steps < *step ||
        *steps > 16) {
        return bad("invalid progress");
    }
    return OpProgress{.id = messageId,
                      .phase = *phase,
                      .step = static_cast<int>(*step),
                      .steps = static_cast<int>(*steps),
                      .done = *done,
                      .total = *total};
}

Result<HostMessage> decodeOpResult(const QJsonObject& data, const std::string& messageId) {
    const QJsonValue ok = data.value(QStringLiteral("ok"));
    if (!ok.isBool()) {
        return bad("invalid result");
    }
    OpResult result{.id = messageId, .ok = ok.toBool(), .code = std::nullopt};
    if (!result.ok) {
        const QJsonValue error = data.value(QStringLiteral("error"));
        if (!error.isObject()) {
            return bad("failed result without an error");
        }
        auto code = errorCode(error.toObject());
        if (!code) {
            return std::unexpected(std::move(code).error());
        }
        result.code = *code;
    }
    return result;
}

// ADR-0364: copies the entries of a command tree at `depth` (1 = the top level) with sanitised
// help texts. False when an entry is malformed or a bound is exceeded.
bool copyCommandEntries(const QJsonArray& in, QJsonArray& out, std::size_t depth,
                        std::size_t& nodes) {
    if (depth > maxCommandDepth) {
        return false;
    }
    for (const QJsonValue& value : in) {
        if (!value.isObject() || ++nodes > maxCommandNodes) {
            return false;
        }
        const QJsonObject entry = value.toObject();
        const QJsonValue name = entry.value(QStringLiteral("name"));
        const QJsonValue help = entry.value(QStringLiteral("help"));
        const QJsonValue children = entry.value(QStringLiteral("children"));
        if (!name.isString() || !device::isCommandName(toStdString(name.toString())) ||
            (!help.isUndefined() && !help.isString())) {
            return false;
        }
        QJsonObject copy;
        copy.insert(QStringLiteral("name"), name);
        copy.insert(QStringLiteral("help"), sanitizeDeviceText(help.toString()));
        if (!children.isUndefined()) {
            QJsonArray copiedChildren;
            if (!children.isArray() ||
                !copyCommandEntries(children.toArray(), copiedChildren, depth + 1, nodes)) {
                return false;
            }
            copy.insert(QStringLiteral("children"), copiedChildren);
        }
        out.append(copy);
    }
    return true;
}

Result<HostMessage> decodeCommands(const QJsonObject& data) {
    if (std::cmp_greater(QJsonDocument(data).toJson(QJsonDocument::Compact).size(),
                         maxCommandsBytes)) {
        return bad("command tree too large");
    }
    const QJsonValue treeValue = data.value(QStringLiteral("tree"));
    if (!treeValue.isObject()) {
        return bad("commands without a tree");
    }
    QJsonObject tree = treeValue.toObject();
    const QJsonValue root = tree.value(QStringLiteral("root"));
    QJsonArray copied;
    std::size_t nodes = 0;
    if (!root.isArray() || !copyCommandEntries(root.toArray(), copied, 1, nodes)) {
        return bad("invalid command tree");
    }
    tree.insert(QStringLiteral("root"), copied);
    auto parsed = device::CommandTree::fromJson(tree);
    if (!parsed) {
        return bad("invalid command tree: " + parsed.error().detail);
    }
    return HostCommands{.tree = std::make_shared<const device::CommandTree>(std::move(*parsed))};
}

// Removes the help texts of a command tree's entries (recursively).
QJsonArray withoutHelp(const QJsonArray& entries) {
    QJsonArray out;
    for (const QJsonValue& value : entries) {
        QJsonObject entry = value.toObject();
        entry.remove(QStringLiteral("help"));
        if (entry.contains(QStringLiteral("children"))) {
            entry.insert(QStringLiteral("children"),
                         withoutHelp(entry.value(QStringLiteral("children")).toArray()));
        }
        out.append(entry);
    }
    return out;
}

} // namespace

std::string_view kindId(OpKind kind) noexcept {
    switch (kind) {
    case OpKind::Flash:
        return "flash";
    case OpKind::Erase:
        return "erase";
    case OpKind::Recover:
        return "recover";
    case OpKind::Reset:
        return "reset";
    case OpKind::LteUpdate:
        return "lteUpdate";
    }
    return "reset";
}

std::string_view targetId(OpTarget target) noexcept {
    return target == OpTarget::Lte ? "lte" : "app";
}

std::optional<OpKind> kindFromId(std::string_view id) noexcept {
    for (const OpKind kind :
         {OpKind::Flash, OpKind::Erase, OpKind::Recover, OpKind::Reset, OpKind::LteUpdate}) {
        if (kindId(kind) == id) {
            return kind;
        }
    }
    return std::nullopt;
}

std::optional<OpTarget> targetFromId(std::string_view id) noexcept {
    if (id == "app") {
        return OpTarget::App;
    }
    if (id == "lte") {
        return OpTarget::Lte;
    }
    return std::nullopt;
}

bool supports(const HostHello& hello, OpKind kind) noexcept {
    return hello.ops && std::ranges::find(hello.opKinds, kind) != hello.opKinds.end();
}

bool validId(std::string_view id) noexcept {
    if (id.empty() || id.size() > maxIdBytes) {
        return false;
    }
    return std::ranges::all_of(id, [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '_';
    });
}

QJsonObject encodeHost(const HostMessage& message) {
    QJsonObject data = std::visit(
        Overloaded{
            [](const HostHello& hello) {
                QJsonObject d = envelope("hello");
                d.insert(QStringLiteral("app"), q(hello.app));
                d.insert(QStringLiteral("version"), q(hello.version));
                QJsonObject device;
                device.insert(QStringLiteral("name"), q(hello.device.name));
                device.insert(QStringLiteral("serial"), q(hello.device.serial));
                device.insert(QStringLiteral("transport"), q(hello.device.transport));
                device.insert(QStringLiteral("firmware"), q(hello.device.firmware));
                d.insert(QStringLiteral("device"), device);
                d.insert(QStringLiteral("ops"), hello.ops);
                QJsonArray kinds;
                if (hello.ops) {
                    for (const OpKind kind : hello.opKinds) {
                        kinds.append(q(kindId(kind)));
                    }
                }
                d.insert(QStringLiteral("opKinds"), kinds);
                return d;
            },
            [](const OpAccepted& accepted) {
                QJsonObject d = envelope("op-accepted");
                d.insert(QStringLiteral("id"), q(accepted.id));
                return d;
            },
            [](const OpDenied& denied) {
                QJsonObject d = envelope("op-denied");
                d.insert(QStringLiteral("id"), q(denied.id));
                d.insert(QStringLiteral("code"), q(denied.code));
                return d;
            },
            [](const OpProgress& progress) {
                QJsonObject d = envelope("op-progress");
                d.insert(QStringLiteral("id"), q(progress.id));
                d.insert(QStringLiteral("phase"), q(progress.phase));
                d.insert(QStringLiteral("step"), progress.step);
                d.insert(QStringLiteral("steps"), progress.steps);
                d.insert(QStringLiteral("done"), static_cast<double>(progress.done));
                d.insert(QStringLiteral("total"), static_cast<double>(progress.total));
                return d;
            },
            [](const OpResult& result) {
                QJsonObject d = envelope("op-result");
                d.insert(QStringLiteral("id"), q(result.id));
                d.insert(QStringLiteral("ok"), result.ok);
                if (result.code) {
                    QJsonObject error;
                    error.insert(QStringLiteral("code"), q(*result.code));
                    d.insert(QStringLiteral("error"), error);
                }
                return d;
            },
            [](const HostEnd& end) {
                QJsonObject d = envelope("end");
                d.insert(QStringLiteral("code"), q(end.code));
                return d;
            },
            [](const HostCommands& commands) {
                QJsonObject d = envelope("commands");
                d.insert(QStringLiteral("tree"),
                         commands.tree ? commands.tree->toJson() : device::CommandTree().toJson());
                return d;
            },
        },
        message);
    QJsonObject at;
    at.insert(QStringLiteral("event"), QStringLiteral("hwio"));
    at.insert(QStringLiteral("data"), data);
    return at;
}

QJsonObject encodeJoiner(const JoinerMessage& message) {
    QJsonObject data =
        std::visit(Overloaded{
                       [](const JoinerHello& hello) {
                           QJsonObject d = envelope("hello");
                           d.insert(QStringLiteral("app"), q(hello.app));
                           d.insert(QStringLiteral("version"), q(hello.version));
                           return d;
                       },
                       [](const OpRequest& request) {
                           QJsonObject d = envelope("op-request");
                           d.insert(QStringLiteral("id"), q(request.id));
                           d.insert(QStringLiteral("kind"), q(kindId(request.kind)));
                           if (request.kind != OpKind::LteUpdate) { // ADR-0363: no target
                               d.insert(QStringLiteral("target"), q(targetId(request.target)));
                           }
                           if (request.firmwareId) {
                               d.insert(QStringLiteral("firmwareId"), q(*request.firmwareId));
                           }
                           return d;
                       },
                       [](const Learn& learn) {
                           QJsonObject d = envelope("learn");
                           QJsonArray path;
                           for (const std::string& word : learn.path) {
                               path.append(q(word));
                           }
                           d.insert(QStringLiteral("path"), path);
                           return d;
                       },
                   },
                   message);
    QJsonObject write;
    write.insert(QStringLiteral("hwio"), data);
    return write;
}

QJsonObject encodeCommands(const device::CommandTree& tree, bool withHelp) {
    QJsonObject at =
        encodeHost(HostCommands{.tree = std::make_shared<const device::CommandTree>(tree)});
    if (withHelp) {
        return at;
    }
    QJsonObject data = at.value(QStringLiteral("data")).toObject();
    QJsonObject json = data.value(QStringLiteral("tree")).toObject();
    json.insert(QStringLiteral("root"), withoutHelp(json.value(QStringLiteral("root")).toArray()));
    data.insert(QStringLiteral("tree"), json);
    at.insert(QStringLiteral("data"), data);
    return at;
}

bool isTunnelAt(const QJsonValue& payload) {
    return payload.isObject() &&
           payload.toObject().value(QStringLiteral("event")).toString() == QStringLiteral("hwio");
}

Result<HostMessage> decodeHost(const QJsonValue& payload) {
    if (!isTunnelAt(payload)) {
        return bad("not a tunnel message");
    }
    auto data = openEnvelope(payload.toObject().value(QStringLiteral("data")));
    if (!data) {
        return std::unexpected(std::move(data).error());
    }
    const QString type = data->value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("hello")) {
        return decodeHostHello(*data);
    }
    if (type == QStringLiteral("end")) {
        auto code = errorCode(*data);
        if (!code) {
            return std::unexpected(std::move(code).error());
        }
        return HostEnd{.code = *code};
    }
    if (type == QStringLiteral("commands")) {
        return decodeCommands(*data);
    }
    auto messageId = id(*data);
    if (!messageId) {
        return std::unexpected(std::move(messageId).error());
    }
    if (type == QStringLiteral("op-accepted")) {
        return OpAccepted{.id = *messageId};
    }
    if (type == QStringLiteral("op-denied")) {
        return decodeOpDenied(*data, *messageId);
    }
    if (type == QStringLiteral("op-progress")) {
        return decodeOpProgress(*data, *messageId);
    }
    if (type == QStringLiteral("op-result")) {
        return decodeOpResult(*data, *messageId);
    }
    return bad("unknown host message type");
}

Result<JoinerMessage> decodeJoiner(const QJsonValue& payload) {
    if (!payload.isObject()) {
        return bad("write is not an object");
    }
    auto data = openEnvelope(payload.toObject().value(QStringLiteral("hwio")));
    if (!data) {
        return std::unexpected(std::move(data).error());
    }
    const QString type = data->value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("hello")) {
        auto app = text(*data, "app");
        auto ver = text(*data, "version");
        if (!app || !ver || *app != appId) {
            return bad("invalid hello");
        }
        return JoinerHello{.app = *app, .version = *ver};
    }
    if (type == QStringLiteral("learn")) {
        const QJsonValue pathValue = data->value(QStringLiteral("path"));
        if (!pathValue.isArray() || pathValue.toArray().isEmpty() ||
            std::cmp_greater(pathValue.toArray().size(), maxCommandDepth)) {
            return bad("invalid learn path");
        }
        Learn learn;
        for (const QJsonValue& word : pathValue.toArray()) {
            if (!word.isString() || !device::isCommandName(toStdString(word.toString()))) {
                return bad("invalid learn path");
            }
            learn.path.push_back(toStdString(word.toString()));
        }
        return learn;
    }
    if (type != QStringLiteral("op-request")) {
        return bad("unknown joiner message type");
    }
    auto messageId = id(*data);
    if (!messageId) {
        return std::unexpected(std::move(messageId).error());
    }
    const auto kind = kindFromId(toStdString(data->value(QStringLiteral("kind")).toString()));
    if (!kind || !data->value(QStringLiteral("kind")).isString()) {
        return bad("invalid kind");
    }
    const QJsonValue targetValue = data->value(QStringLiteral("target"));
    std::optional<OpTarget> target;
    if (*kind == OpKind::LteUpdate) {
        // ADR-0363: the guided LTE update has no target (it is always the LTE MCU).
        if (!targetValue.isUndefined()) {
            return bad("a target for lteUpdate");
        }
        target = OpTarget::Lte;
    } else if (targetValue.isString()) {
        target = targetFromId(toStdString(targetValue.toString()));
    }
    if (!target) {
        return bad("invalid target");
    }
    OpRequest request{
        .id = *messageId, .kind = *kind, .target = *target, .firmwareId = std::nullopt};
    const QJsonValue firmware = data->value(QStringLiteral("firmwareId"));
    if (*kind == OpKind::Flash) {
        if (!firmware.isString()) {
            return bad("flash without a firmware ID");
        }
        const std::string firmwareText = toStdString(firmware.toString());
        if (!firmware::FirmwareId::parse(firmwareText)) {
            return bad("invalid firmware ID");
        }
        request.firmwareId = firmware::FirmwareId::parse(firmwareText)->value();
    } else if (!firmware.isUndefined()) {
        return bad("firmware ID for an operation other than flash");
    }
    return request;
}

} // namespace hwio::remote::tunnel
