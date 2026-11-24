#include "hwio/remote/tunnel.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/core/text_sanitizer.h"
#include "hwio/device/command_tree.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/remote/baseline_errors.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QString>
#include <QStringLiteral>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

// The `code` of op-denied, the error of op-result and end: a known hwio::Error id. ADR-0414: an
// optional `detail: {code}` refines it; its id is used when this build knows it (a newer host may
// send ids this build does not know: then `code`).
Result<std::string> errorCode(const QJsonObject& object) {
    const QJsonValue value = object.value(QStringLiteral("code"));
    if (!value.isString()) {
        return bad("error code is not a string");
    }
    std::string code = toStdString(value.toString());
    if (code.size() > maxErrorIdBytes || !errorCodeFromId(code)) {
        return bad("unknown error code");
    }
    const QJsonValue detail = object.value(QStringLiteral("detail"));
    if (detail.isUndefined()) {
        return code;
    }
    const QJsonValue detailCode = detail.toObject().value(QStringLiteral("code"));
    if (!detail.isObject() || !detailCode.isString() ||
        std::cmp_greater(detailCode.toString().size(), maxErrorIdBytes)) {
        return bad("invalid error detail");
    }
    std::string refined = toStdString(detailCode.toString());
    if (errorCodeFromId(refined)) {
        return refined;
    }
    return code;
}

// ADR-0414: inserts `code` as an id of Monitor 2.0.0-rc.4 into `object`, plus `detail: {code}`
// when the real id differs.
void insertErrorCode(QJsonObject& object, const std::string& code) {
    const auto known = errorCodeFromId(code);
    const std::string_view baseline = known ? baselineErrorId(*known) : std::string_view(code);
    object.insert(QStringLiteral("code"), q(baseline));
    if (baseline != code) {
        QJsonObject detail;
        detail.insert(QStringLiteral("code"), q(code));
        object.insert(QStringLiteral("detail"), detail);
    }
}

// ADR-0414: an optional name (absent = "").
Result<std::string> optionalName(const QJsonObject& data, const QString& key) {
    const QJsonValue value = data.value(key);
    if (value.isUndefined()) {
        return std::string();
    }
    if (!value.isString() || !validName(toStdString(value.toString()))) {
        return bad("invalid " + toStdString(key));
    }
    return toStdString(value.toString());
}

// ADR-0414: an optional list of caps (absent = none): names, at most maxCaps, kept in order
// without duplicates.
Result<std::vector<std::string>> capsOf(const QJsonObject& data) {
    const QJsonValue value = data.value(QStringLiteral("caps"));
    std::vector<std::string> caps;
    if (value.isUndefined()) {
        return caps;
    }
    if (!value.isArray() || std::cmp_greater(value.toArray().size(), maxCaps)) {
        return bad("invalid caps");
    }
    for (const auto& entry : value.toArray()) {
        const std::string name = toStdString(entry.toString());
        if (!entry.isString() || !validName(name)) {
            return bad("invalid caps");
        }
        if (std::ranges::find(caps, name) == caps.end()) {
            caps.push_back(name);
        }
    }
    return caps;
}

QJsonArray capsJson(const std::vector<std::string>& caps) {
    QJsonArray out;
    for (const std::string& name : caps) {
        out.append(q(name));
    }
    return out;
}

// A list of kinds (opKinds, a target's kinds): at most maxOpKinds strings; unknown kinds (a later
// version) are skipped, duplicates dropped.
Result<std::vector<OpKind>> kindsOf(const QJsonValue& value) {
    if (!value.isArray() || std::cmp_greater(value.toArray().size(), maxOpKinds)) {
        return bad("invalid kinds");
    }
    std::vector<OpKind> kinds;
    for (const auto& kindValue : value.toArray()) {
        if (!kindValue.isString() || kindValue.toString().size() > 32) {
            return bad("invalid kinds");
        }
        const auto kind = kindFromId(toStdString(kindValue.toString()));
        if (kind && std::ranges::find(kinds, *kind) == kinds.end()) {
            kinds.push_back(*kind);
        }
    }
    return kinds;
}

Result<PreparedFirmware> preparedOf(const QJsonValue& value) {
    if (!value.isObject()) {
        return bad("invalid prepared firmware");
    }
    const QJsonObject object = value.toObject();
    const QJsonValue idValue = object.value(QStringLiteral("id"));
    auto name = text(object, "name");
    auto ver = text(object, "version");
    if (!idValue.isString() || !validName(toStdString(idValue.toString())) || !name || !ver) {
        return bad("invalid prepared firmware");
    }
    return PreparedFirmware{.id = toStdString(idValue.toString()), .name = *name, .version = *ver};
}

Result<HostTarget> hostTargetOf(const QJsonValue& value) {
    if (!value.isObject()) {
        return bad("invalid target");
    }
    const QJsonObject object = value.toObject();
    const QJsonValue idValue = object.value(QStringLiteral("id"));
    if (!idValue.isString() || !validName(toStdString(idValue.toString()))) {
        return bad("invalid target id");
    }
    HostTarget target{
        .id = toStdString(idValue.toString()), .kinds = {}, .catalog = false, .prepared = {}};
    const QJsonValue kinds = object.value(QStringLiteral("kinds"));
    if (!kinds.isUndefined()) {
        auto decoded = kindsOf(kinds);
        if (!decoded) {
            return std::unexpected(std::move(decoded).error());
        }
        target.kinds = std::move(*decoded);
    }
    const QJsonValue catalog = object.value(QStringLiteral("catalog"));
    if (!catalog.isUndefined() && !catalog.isBool()) {
        return bad("invalid target catalog");
    }
    target.catalog = catalog.toBool(false);
    const QJsonValue prepared = object.value(QStringLiteral("prepared"));
    if (!prepared.isUndefined()) {
        if (!prepared.isArray() || std::cmp_greater(prepared.toArray().size(), maxPrepared)) {
            return bad("invalid prepared firmware");
        }
        for (const auto& entry : prepared.toArray()) {
            auto firmware = preparedOf(entry);
            if (!firmware) {
                return std::unexpected(std::move(firmware).error());
            }
            if (std::ranges::find(target.prepared, firmware->id, &PreparedFirmware::id) !=
                target.prepared.end()) {
                return bad("duplicate prepared firmware");
            }
            target.prepared.push_back(std::move(*firmware));
        }
    }
    return target;
}

// ADR-0414: the optional `targets` of a host hello (absent = nullopt).
Result<std::optional<std::vector<HostTarget>>> targetsOf(const QJsonObject& data) {
    const QJsonValue value = data.value(QStringLiteral("targets"));
    if (value.isUndefined()) {
        return std::optional<std::vector<HostTarget>>();
    }
    if (!value.isArray() || std::cmp_greater(value.toArray().size(), maxTargets)) {
        return bad("invalid targets");
    }
    std::vector<HostTarget> targets;
    for (const auto& entry : value.toArray()) {
        auto target = hostTargetOf(entry);
        if (!target) {
            return std::unexpected(std::move(target).error());
        }
        if (std::ranges::find(targets, target->id, &HostTarget::id) != targets.end()) {
            return bad("duplicate target");
        }
        targets.push_back(std::move(*target));
    }
    return std::optional(std::move(targets));
}

QJsonObject hostTargetJson(const HostTarget& target) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), q(target.id));
    QJsonArray kinds;
    for (const OpKind kind : target.kinds) {
        kinds.append(q(kindId(kind)));
    }
    object.insert(QStringLiteral("kinds"), kinds);
    object.insert(QStringLiteral("catalog"), target.catalog);
    if (!target.prepared.empty()) {
        QJsonArray prepared;
        for (const PreparedFirmware& firmware : target.prepared) {
            QJsonObject entry;
            entry.insert(QStringLiteral("id"), q(firmware.id));
            entry.insert(QStringLiteral("name"), q(firmware.name));
            entry.insert(QStringLiteral("version"), q(firmware.version));
            prepared.append(entry);
        }
        object.insert(QStringLiteral("prepared"), prepared);
    }
    return object;
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

// ADR-0368: an optional bool (absent = false).
Result<bool> optionalBool(const QJsonObject& data, const QString& key) {
    const QJsonValue value = data.value(key);
    if (!value.isUndefined() && !value.isBool()) {
        return bad("invalid " + toStdString(key));
    }
    return value.toBool(false);
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
    } else {
        auto decoded = kindsOf(kinds); // unknown kinds (a later version) are skipped
        if (!decoded) {
            return bad("invalid opKinds");
        }
        if (ops.toBool()) {
            hello.opKinds = std::move(*decoded);
        }
    }
    hello.app = *app;
    hello.version = *ver;
    hello.device = DeviceInfo{
        .name = *name, .serial = *serial, .transport = *transport, .firmware = *firmware};
    hello.ops = ops.toBool();
    const auto questions = optionalBool(data, QStringLiteral("opQuestions")); // ADR-0368
    if (!questions) {
        return std::unexpected(questions.error());
    }
    hello.opQuestions = hello.ops && *questions;
    // Plan 0059 (ADR-0414): optional, absent from older hosts.
    auto caps = capsOf(data);
    auto profile = optionalName(data, QStringLiteral("profile"));
    const QJsonValue console = data.value(QStringLiteral("console"));
    auto targets = targetsOf(data);
    if (!caps || !profile || (!console.isUndefined() && !console.isBool()) || !targets) {
        return bad("invalid hello extension");
    }
    hello.caps = std::move(*caps);
    hello.profile = std::move(*profile);
    hello.console = console.toBool(true);
    hello.targets = std::move(*targets);
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

// ADR-0368: a question id (the operation id rules).
Result<std::string> qid(const QJsonObject& object) {
    const QJsonValue value = object.value(QStringLiteral("qid"));
    if (!value.isString() || !validId(toStdString(value.toString()))) {
        return bad("invalid question id");
    }
    return toStdString(value.toString());
}

Result<HostMessage> decodeOpQuestion(const QJsonObject& data, const std::string& messageId) {
    auto questionId = qid(data);
    if (!questionId) {
        return std::unexpected(std::move(questionId).error());
    }
    const QJsonValue kind = data.value(QStringLiteral("question"));
    const auto question =
        kind.isString() ? questionFromId(toStdString(kind.toString())) : std::nullopt;
    if (!question) {
        return bad("unknown question");
    }
    return OpQuestion{.id = messageId, .qid = *questionId, .question = *question};
}

Result<JoinerMessage> decodeOpAnswer(const QJsonObject& data) {
    auto messageId = id(data);
    if (!messageId) {
        return std::unexpected(std::move(messageId).error());
    }
    auto questionId = qid(data);
    if (!questionId) {
        return std::unexpected(std::move(questionId).error());
    }
    const QJsonValue yes = data.value(QStringLiteral("yes"));
    if (!yes.isBool()) {
        return bad("invalid answer");
    }
    return OpAnswer{.id = *messageId, .qid = *questionId, .yes = yes.toBool()};
}

Result<JoinerMessage> decodeLearn(const QJsonObject& data) {
    const QJsonValue pathValue = data.value(QStringLiteral("path"));
    if (!pathValue.isArray() || pathValue.toArray().isEmpty() ||
        std::cmp_greater(pathValue.toArray().size(), maxCommandDepth)) {
        return bad("invalid learn path");
    }
    Learn learn;
    for (const auto& word : pathValue.toArray()) {
        if (!word.isString() || !device::isCommandName(toStdString(word.toString()))) {
            return bad("invalid learn path");
        }
        learn.path.push_back(toStdString(word.toString()));
    }
    return learn;
}

Result<JoinerMessage> decodeOpRequest(const QJsonObject& data) {
    auto messageId = id(data);
    if (!messageId) {
        return std::unexpected(std::move(messageId).error());
    }
    const auto kind = kindFromId(toStdString(data.value(QStringLiteral("kind")).toString()));
    if (!kind || !data.value(QStringLiteral("kind")).isString()) {
        return bad("invalid kind");
    }
    const QJsonValue targetValue = data.value(QStringLiteral("target"));
    std::optional<OpTarget> target; // ADR-0414: any profile target id
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
    OpRequest request{.id = *messageId,
                      .kind = *kind,
                      .target = *target,
                      .firmwareId = std::nullopt,
                      .questions = false,
                      .prepared = std::nullopt};
    // ADR-0368: optional; only `true` is sent.
    const QJsonValue questions = data.value(QStringLiteral("questions"));
    if (!questions.isUndefined() && !questions.isBool()) {
        return bad("invalid questions");
    }
    request.questions = questions.toBool(false);
    const QJsonValue firmware = data.value(QStringLiteral("firmwareId"));
    // ADR-0418: a prepared firmware instead of a catalog ID (flash only).
    const QJsonValue prepared = data.value(QStringLiteral("prepared"));
    if (!prepared.isUndefined()) {
        if (*kind != OpKind::Flash || !firmware.isUndefined() || !prepared.isString() ||
            !validName(toStdString(prepared.toString()))) {
            return bad("invalid prepared firmware");
        }
        request.prepared = toStdString(prepared.toString());
        return request;
    }
    if (*kind == OpKind::Flash) {
        if (!firmware.isString()) {
            return bad("flash without a firmware ID");
        }
        const auto parsed = firmware::FirmwareId::parse(toStdString(firmware.toString()));
        if (!parsed) {
            return bad("invalid firmware ID");
        }
        request.firmwareId = parsed->value();
    } else if (!firmware.isUndefined()) {
        return bad("firmware ID for an operation other than flash");
    }
    return request;
}

// ADR-0364: copies the entries of a command tree at `depth` (1 = the top level) with sanitised
// help texts. False when an entry is malformed or a bound is exceeded.
bool copyCommandEntries(const QJsonArray& in, QJsonArray& out, std::size_t depth,
                        std::size_t& nodes) {
    if (depth > maxCommandDepth) {
        return false;
    }
    for (const auto& value : in) {
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
    for (const auto& value : entries) {
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

std::string_view targetId(const OpTarget& target) noexcept { return target.id(); }

bool hasCap(const std::vector<std::string>& caps, std::string_view name) noexcept {
    return std::ranges::find(caps, name) != caps.end();
}

const HostTarget* findTarget(const HostHello& hello, std::string_view id) noexcept {
    if (!hello.targets) {
        return nullptr;
    }
    const auto it = std::ranges::find(*hello.targets, id, &HostTarget::id);
    return it == hello.targets->end() ? nullptr : &*it;
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

std::string_view questionId(OpQuestionKind kind) noexcept {
    return kind == OpQuestionKind::ContinueWithoutCopy ? "continueWithoutCopy" : "continue";
}

std::optional<OpQuestionKind> questionFromId(std::string_view id) noexcept {
    if (id == "continue") {
        return OpQuestionKind::Continue;
    }
    if (id == "continueWithoutCopy") {
        return OpQuestionKind::ContinueWithoutCopy;
    }
    return std::nullopt;
}

std::optional<OpTarget> targetFromId(std::string_view id) noexcept { return OpTarget::fromId(id); }

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
                if (hello.opQuestions) { // ADR-0368
                    d.insert(QStringLiteral("opQuestions"), true);
                }
                // Plan 0059 (ADR-0414): only what is set, so that a CHESTER host's hello stays
                // as an older host's apart from caps and profile.
                if (!hello.caps.empty()) {
                    d.insert(QStringLiteral("caps"), capsJson(hello.caps));
                }
                if (!hello.profile.empty()) {
                    d.insert(QStringLiteral("profile"), q(hello.profile));
                }
                if (!hello.console) {
                    d.insert(QStringLiteral("console"), false);
                }
                if (hello.targets) {
                    QJsonArray targets;
                    for (const HostTarget& target : *hello.targets) {
                        targets.append(hostTargetJson(target));
                    }
                    d.insert(QStringLiteral("targets"), targets);
                }
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
                insertErrorCode(d, denied.code); // ADR-0414
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
                    insertErrorCode(error, *result.code); // ADR-0414: detail inside `error`
                    d.insert(QStringLiteral("error"), error);
                }
                return d;
            },
            [](const OpQuestion& question) {
                QJsonObject d = envelope("op-question");
                d.insert(QStringLiteral("id"), q(question.id));
                d.insert(QStringLiteral("qid"), q(question.qid));
                d.insert(QStringLiteral("question"), q(questionId(question.question)));
                return d;
            },
            [](const HostEnd& end) {
                QJsonObject d = envelope("end");
                insertErrorCode(d, end.code); // ADR-0414
                return d;
            },
            [](const LineRefused& refused) {
                QJsonObject d = envelope("line-refused");
                insertErrorCode(d, refused.code);
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
                           if (!hello.caps.empty()) { // plan 0059
                               d.insert(QStringLiteral("caps"), capsJson(hello.caps));
                           }
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
                           if (request.questions) { // ADR-0368
                               d.insert(QStringLiteral("questions"), true);
                           }
                           if (request.prepared) { // ADR-0418
                               d.insert(QStringLiteral("prepared"), q(*request.prepared));
                           }
                           return d;
                       },
                       [](const OpAnswer& answer) {
                           QJsonObject d = envelope("op-answer");
                           d.insert(QStringLiteral("id"), q(answer.id));
                           d.insert(QStringLiteral("qid"), q(answer.qid));
                           d.insert(QStringLiteral("yes"), answer.yes);
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
    if (type == QStringLiteral("line-refused")) { // plan 0059 (ADR-0416)
        auto code = errorCode(*data);
        if (!code) {
            return std::unexpected(std::move(code).error());
        }
        return LineRefused{.code = *code};
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
    if (type == QStringLiteral("op-question")) {
        return decodeOpQuestion(*data, *messageId);
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
        auto caps = capsOf(*data); // plan 0059
        if (!app || !ver || *app != appId || !caps) {
            return bad("invalid hello");
        }
        return JoinerHello{.app = *app, .version = *ver, .caps = std::move(*caps)};
    }
    if (type == QStringLiteral("learn")) {
        return decodeLearn(*data);
    }
    if (type == QStringLiteral("op-answer")) {
        return decodeOpAnswer(*data);
    }
    if (type != QStringLiteral("op-request")) {
        return bad("unknown joiner message type");
    }
    return decodeOpRequest(*data);
}

} // namespace hwio::remote::tunnel
