#include "hwio/core/error.h"
#include "hwio/device/command_tree.h"
#include "hwio/remote/tunnel.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace hwio::remote::tunnel {
namespace {

QJsonValue json(const char* text) { return QJsonDocument::fromJson(QByteArray(text)).object(); }

TEST(Tunnel, HostMessagesRoundTrip) {
    const std::array<HostMessage, 8> messages = {
        HostHello{.app = "hwio-monitor",
                  .version = "2.1.0",
                  .device = {.name = "CHESTER",
                             .serial = "1",
                             .transport = "jlink",
                             .firmware = "Clime v3"},
                  .ops = true,
                  .opKinds = {OpKind::Reset, OpKind::LteUpdate}},
        OpAccepted{.id = "op-1"},
        OpDenied{.id = "op-1", .code = "internal.busy"},
        OpProgress{
            .id = "op-1", .phase = "programming", .step = 1, .steps = 2, .done = 5, .total = 10},
        OpResult{.id = "op-1", .ok = false, .code = "firmware.verify_failed"},
        OpResult{.id = "op-1", .ok = true, .code = std::nullopt},
        HostEnd{},
        HostEnd{.code = "remote.code_collision"},
    };
    for (const HostMessage& message : messages) {
        const QJsonObject at = encodeHost(message);
        EXPECT_EQ(at.value(QStringLiteral("event")).toString(), QStringLiteral("hwio"));
        EXPECT_TRUE(isTunnelAt(at));
        auto decoded = decodeHost(at);
        ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
        EXPECT_EQ(decoded->index(), message.index());
    }
    const auto hello = std::get<HostHello>(*decodeHost(encodeHost(messages[0])));
    EXPECT_TRUE(hello.ops);
    EXPECT_EQ(hello.device.firmware, "Clime v3");
    EXPECT_EQ(hello.opKinds, (std::vector<OpKind>{OpKind::Reset, OpKind::LteUpdate}));
    EXPECT_TRUE(supports(hello, OpKind::LteUpdate));
    EXPECT_FALSE(supports(hello, OpKind::Flash));
    EXPECT_EQ(std::get<HostEnd>(*decodeHost(encodeHost(messages[7]))).code,
              "remote.code_collision");
    EXPECT_EQ(std::get<HostEnd>(*decodeHost(encodeHost(messages[6]))).code, "cancelled");
}

TEST(Tunnel, JoinerMessagesRoundTrip) {
    const QJsonObject write =
        encodeJoiner(OpRequest{.id = "a",
                               .kind = OpKind::Flash,
                               .target = OpTarget::App,
                               .firmwareId = "0123456789ABCDEF0123456789abcdef"});
    EXPECT_TRUE(write.contains(QStringLiteral("hwio")));
    auto decoded = decodeJoiner(write);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
    const auto request = std::get<OpRequest>(*decoded);
    EXPECT_EQ(request.kind, OpKind::Flash);
    ASSERT_TRUE(request.firmwareId.has_value());
    auto hello = decodeJoiner(encodeJoiner(JoinerHello{.app = "hwio-monitor", .version = "2.1"}));
    ASSERT_TRUE(hello.has_value());
    EXPECT_TRUE(std::holds_alternative<JoinerHello>(*hello));
}

TEST(Tunnel, StringsAreNeverTunnelMessages) {
    EXPECT_FALSE(decodeJoiner(QJsonValue(QStringLiteral("{\"hwio\":{\"v\":1}}"))).has_value());
    EXPECT_FALSE(isTunnelAt(QJsonValue(QStringLiteral("hwio"))));
}

TEST(Tunnel, RejectsInvalidJoinerMessages) {
    for (
        const char* text : {
            R"({})",
            R"({"hwio":"x"})",
            R"({"hwio":{"v":2,"type":"hello","app":"hwio-monitor","version":"1"}})",
            R"({"hwio":{"v":"1","type":"hello","app":"hwio-monitor","version":"1"}})",
            R"({"hwio":{"v":1,"type":"bye"}})",
            R"({"hwio":{"v":1,"type":"hello","app":"other","version":"1"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"format","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"reset","target":"uicr"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a b","kind":"reset","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"","kind":"reset","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"0123456789012345678901234567890123","kind":"reset","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"flash","target":"app"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"flash","target":"app","firmwareId":"xyz"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"flash","target":"app","firmwareId":"../../etc/passwd"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"reset","target":"app","firmwareId":"0123456789abcdef0123456789abcdef"}})",
        }) {
        const auto decoded = decodeJoiner(json(text));
        ASSERT_FALSE(decoded.has_value()) << text;
        EXPECT_EQ(decoded.error().code, ErrorCode::RemoteProtocolError) << text;
    }
}

TEST(Tunnel, RejectsInvalidHostMessages) {
    for (
        const char* text : {
            R"({"event":"hwio","data":{"v":1,"type":"op-denied","id":"a","code":"made.up"}})",
            R"({"event":"hwio","data":{"v":1,"type":"op-result","id":"a","ok":false}})",
            R"({"event":"hwio","data":{"v":1,"type":"op-progress","id":"a","step":0,"steps":1,"done":0,"total":0}})",
            R"({"event":"hwio","data":{"v":1,"type":"op-progress","id":"a","step":1,"steps":1,"done":-1,"total":0}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"1","ops":true}})",
            R"({"event":"hwio","data":{"v":1,"type":"end","code":"made.up"}})",
            R"({"event":"hwio","data":{"v":1,"type":"end","code":7}})",
            R"({"event":"read","data":"x"})",
        }) {
        EXPECT_FALSE(decodeHost(json(text)).has_value()) << text;
    }
}

TEST(Tunnel, DisplayTextIsSanitisedAndBounded) {
    auto hello = decodeHost(json(
        R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"2\u001b[31m","device":{"name":"A\u0007B","serial":"","transport":"ble","firmware":""},"ops":false}})"));
    ASSERT_TRUE(hello.has_value());
    const auto& h = std::get<HostHello>(*hello);
    EXPECT_EQ(h.device.name.find('\x07'), std::string::npos);
    EXPECT_EQ(h.version.find('\x1b'), std::string::npos);
    const std::string longName(200, 'x');
    QJsonObject at = encodeHost(
        HostHello{.app = "hwio-monitor",
                  .version = "1",
                  .device = {.name = longName, .serial = {}, .transport = {}, .firmware = {}},
                  .ops = false,
                  .opKinds = {}});
    EXPECT_FALSE(decodeHost(at).has_value());
}

// ADR-0363: lteUpdate has no target and no firmware ID; the hello lists the kinds.
TEST(Tunnel, LteUpdateRequestHasNoTarget) {
    const QJsonObject write = encodeJoiner(OpRequest{
        .id = "u", .kind = OpKind::LteUpdate, .target = OpTarget::App, .firmwareId = std::nullopt});
    const QJsonObject data = write.value(QStringLiteral("hwio")).toObject();
    EXPECT_EQ(data.value(QStringLiteral("kind")).toString(), QStringLiteral("lteUpdate"));
    EXPECT_FALSE(data.contains(QStringLiteral("target")));
    auto decoded = decodeJoiner(write);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
    EXPECT_EQ(std::get<OpRequest>(*decoded).kind, OpKind::LteUpdate);
    EXPECT_EQ(std::get<OpRequest>(*decoded).target, OpTarget::Lte);
    for (
        const char* text : {
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"lteUpdate","target":"lte"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"lteUpdate","firmwareId":"0123456789abcdef0123456789abcdef"}})",
            R"({"hwio":{"v":1,"type":"op-request","id":"a","kind":"reset"}})",
        }) {
        EXPECT_FALSE(decodeJoiner(json(text)).has_value()) << text;
    }
}

TEST(Tunnel, HelloOpKinds) {
    const auto decode = [](const char* text) { return decodeHost(json(text)); };
    // Without opKinds (an earlier host): the four ADR-0322 kinds when ops is true.
    auto old = decode(
        R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"2","device":{"name":"","serial":"","transport":"","firmware":""},"ops":true}})");
    ASSERT_TRUE(old.has_value());
    EXPECT_TRUE(supports(std::get<HostHello>(*old), OpKind::Reset));
    EXPECT_FALSE(supports(std::get<HostHello>(*old), OpKind::LteUpdate));
    // Unknown kinds (a later version) are skipped; ops false means none.
    auto later = decode(
        R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"3","device":{"name":"","serial":"","transport":"","firmware":""},"ops":true,"opKinds":["lteUpdate","teleport"]}})");
    ASSERT_TRUE(later.has_value());
    EXPECT_EQ(std::get<HostHello>(*later).opKinds, std::vector<OpKind>{OpKind::LteUpdate});
    auto none = decode(
        R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"3","device":{"name":"","serial":"","transport":"","firmware":""},"ops":false,"opKinds":["reset"]}})");
    ASSERT_TRUE(none.has_value());
    EXPECT_FALSE(supports(std::get<HostHello>(*none), OpKind::Reset));
    for (
        const char* text : {
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"3","device":{"name":"","serial":"","transport":"","firmware":""},"ops":true,"opKinds":"reset"}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"3","device":{"name":"","serial":"","transport":"","firmware":""},"ops":true,"opKinds":[1]}})",
            R"({"event":"hwio","data":{"v":1,"type":"hello","app":"hwio-monitor","version":"3","device":{"name":"","serial":"","transport":"","firmware":""},"ops":true,"opKinds":["a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a"]}})",
        }) {
        EXPECT_FALSE(decode(text).has_value()) << text;
    }
}

// ADR-0364: the command tree and learn requests.
device::CommandTree sampleTree() {
    device::CommandTree tree;
    tree.setDashHSupported(true);
    const std::vector<device::CommandEntry> top{{.name = "app", .help = "Application"},
                                                {.name = "lte", .help = "LTE"}};
    EXPECT_TRUE(tree.setChildren({}, top));
    const std::vector<std::string> path{"app"};
    const std::vector<device::CommandEntry> sub{{.name = "config", .help = "Configuration"}};
    EXPECT_TRUE(tree.setChildren(path, sub));
    return tree;
}

// ADR-0368: support's questions - the hello flag, the request flag, op-question and op-answer.
TEST(Tunnel, OperationQuestionsRoundTrip) {
    HostHello hello{
        .app = "hwio-monitor",
        .version = "2.1.0",
        .device = {.name = "CHESTER", .serial = "1", .transport = "jlink", .firmware = ""},
        .ops = true,
        .opKinds = {OpKind::LteUpdate},
        .opQuestions = true};
    auto decodedHello = decodeHost(encodeHost(hello));
    ASSERT_TRUE(decodedHello.has_value()) << decodedHello.error().detail;
    EXPECT_TRUE(std::get<HostHello>(*decodedHello).opQuestions);
    hello.opQuestions = false;
    const QJsonObject without = encodeHost(hello);
    EXPECT_FALSE(
        without.value(QStringLiteral("data")).toObject().contains(QStringLiteral("opQuestions")));
    EXPECT_FALSE(std::get<HostHello>(*decodeHost(without)).opQuestions);

    const OpRequest request{.id = "op-1",
                            .kind = OpKind::LteUpdate,
                            .target = OpTarget::Lte,
                            .firmwareId = std::nullopt,
                            .questions = true};
    auto decodedRequest = decodeJoiner(encodeJoiner(request));
    ASSERT_TRUE(decodedRequest.has_value()) << decodedRequest.error().detail;
    EXPECT_TRUE(std::get<OpRequest>(*decodedRequest).questions);

    for (const OpQuestionKind kind :
         {OpQuestionKind::Continue, OpQuestionKind::ContinueWithoutCopy}) {
        auto question =
            decodeHost(encodeHost(OpQuestion{.id = "op-1", .qid = "q-1", .question = kind}));
        ASSERT_TRUE(question.has_value()) << question.error().detail;
        const auto& q = std::get<OpQuestion>(*question);
        EXPECT_EQ(q.id, "op-1");
        EXPECT_EQ(q.qid, "q-1");
        EXPECT_EQ(q.question, kind);
        EXPECT_EQ(questionFromId(questionId(kind)), kind);
    }
    EXPECT_EQ(questionId(OpQuestionKind::Continue), "continue");
    EXPECT_EQ(questionId(OpQuestionKind::ContinueWithoutCopy), "continueWithoutCopy");
    EXPECT_FALSE(questionFromId("erase").has_value());

    auto answer = decodeJoiner(encodeJoiner(OpAnswer{.id = "op-1", .qid = "q-1", .yes = true}));
    ASSERT_TRUE(answer.has_value()) << answer.error().detail;
    EXPECT_TRUE(std::get<OpAnswer>(*answer).yes);
    EXPECT_EQ(std::get<OpAnswer>(*answer).qid, "q-1");
}

TEST(Tunnel, RejectsInvalidOperationQuestions) {
    for (
        const char* text : {
            R"({"event": "hwio", "data": {"v": 1, "type": "op-question", "id": "a", "question": "continue"}})",
            R"({"event": "hwio", "data": {"v": 1, "type": "op-question", "id": "a", "qid": "q 1", "question": "continue"}})",
            R"({"event": "hwio", "data": {"v": 1, "type": "op-question", "id": "a", "qid": "q", "question": "erase"}})",
            R"({"event": "hwio", "data": {"v": 1, "type": "op-question", "id": "a", "qid": "q", "question": 1}})",
            R"({"event": "hwio", "data": {"v": 1, "type": "op-question", "qid": "q", "question": "continue"}})",
            R"({"event": "hwio", "data": {"v": 1, "type": "hello", "app": "hwio-monitor", "version": "2", "device": {"name": "", "serial": "", "transport": "", "firmware": ""}, "ops": true, "opQuestions": "yes"}})",
        }) {
        const auto decoded = decodeHost(json(text));
        ASSERT_FALSE(decoded.has_value()) << text;
        EXPECT_EQ(decoded.error().code, ErrorCode::RemoteProtocolError);
    }
    for (
        const char* text : {
            R"({"hwio": {"v": 1, "type": "op-answer", "id": "a", "qid": "q"}})",
            R"({"hwio": {"v": 1, "type": "op-answer", "id": "a", "qid": "q", "yes": "y"}})",
            R"({"hwio": {"v": 1, "type": "op-answer", "id": "a", "yes": true}})",
            R"({"hwio": {"v": 1, "type": "op-answer", "qid": "q", "yes": true}})",
            R"({"hwio": {"v": 1, "type": "op-answer", "id": "a", "qid": "0123456789012345678901234567890123", "yes": true}})",
            R"({"hwio": {"v": 1, "type": "op-request", "id": "a", "kind": "lteUpdate", "questions": 1}})",
        }) {
        const auto decoded = decodeJoiner(json(text));
        ASSERT_FALSE(decoded.has_value()) << text;
        EXPECT_EQ(decoded.error().code, ErrorCode::RemoteProtocolError);
    }
    // ops: false never offers questions.
    const auto noOps = decodeHost(json(
        R"({"event": "hwio", "data": {"v": 1, "type": "hello", "app": "hwio-monitor", "version": "2", "device": {"name": "", "serial": "", "transport": "", "firmware": ""}, "ops": false, "opQuestions": true}})"));
    ASSERT_TRUE(noOps.has_value());
    EXPECT_FALSE(std::get<HostHello>(*noOps).opQuestions);
}

TEST(Tunnel, CommandsRoundTrip) {
    const device::CommandTree tree = sampleTree();
    auto decoded = decodeHost(encodeCommands(tree, true));
    ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
    const auto& commands = std::get<HostCommands>(*decoded);
    ASSERT_NE(commands.tree, nullptr);
    EXPECT_EQ(commands.tree->nodeCount(), 3U);
    EXPECT_TRUE(commands.tree->dashHSupported());
    const std::vector<std::string> path{"app", "config"};
    ASSERT_NE(commands.tree->find(path), nullptr);
    const std::vector<std::string> app{"app"};
    EXPECT_EQ(commands.tree->find(app)->help, "Application");
    auto bare = decodeHost(encodeCommands(tree, false));
    ASSERT_TRUE(bare.has_value());
    EXPECT_EQ(std::get<HostCommands>(*bare).tree->find(app)->help, "");
}

QJsonObject commandsWithRoot(const QJsonArray& root) {
    QJsonObject tree;
    tree.insert(QStringLiteral("schema"), 2);
    tree.insert(QStringLiteral("dashH"), true);
    tree.insert(QStringLiteral("root"), root);
    QJsonObject data;
    data.insert(QStringLiteral("v"), 1);
    data.insert(QStringLiteral("type"), QStringLiteral("commands"));
    data.insert(QStringLiteral("tree"), tree);
    QJsonObject at;
    at.insert(QStringLiteral("event"), QStringLiteral("hwio"));
    at.insert(QStringLiteral("data"), data);
    return at;
}

QJsonObject entry(const QString& name, const QJsonValue& help = QStringLiteral("")) {
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("help"), help);
    return object;
}

TEST(Tunnel, RejectsMalformedOrOversizeCommandTrees) {
    // A bad name, a help that is not text, children that are not a list.
    EXPECT_FALSE(decodeHost(commandsWithRoot({entry(QStringLiteral("rm -rf"))})).has_value());
    EXPECT_FALSE(decodeHost(commandsWithRoot({entry(QStringLiteral("a"), 5)})).has_value());
    QJsonObject badChildren = entry(QStringLiteral("a"));
    badChildren.insert(QStringLiteral("children"), QStringLiteral("x"));
    EXPECT_FALSE(decodeHost(commandsWithRoot({badChildren})).has_value());
    EXPECT_FALSE(
        decodeHost(json(R"({"event":"hwio","data":{"v":1,"type":"commands"}})")).has_value());
    // Deeper than 8 levels.
    QJsonObject deep = entry(QStringLiteral("x"));
    for (int i = 0; i < 9; ++i) {
        QJsonObject parent = entry(QStringLiteral("x"));
        parent.insert(QStringLiteral("children"), QJsonArray{deep});
        deep = parent;
    }
    EXPECT_FALSE(decodeHost(commandsWithRoot({deep})).has_value());
    // More than 4000 nodes.
    QJsonArray many;
    for (int i = 0; i < 4001; ++i) {
        many.append(entry(QStringLiteral("c%1").arg(i)));
    }
    EXPECT_FALSE(decodeHost(commandsWithRoot(many)).has_value());
    // More than 256 KiB.
    QJsonArray big;
    for (int i = 0; i < 1100; ++i) {
        big.append(entry(QStringLiteral("c%1").arg(i), QString(256, QLatin1Char('h'))));
    }
    EXPECT_FALSE(decodeHost(commandsWithRoot(big)).has_value());
    // Help texts are sanitised.
    auto clean = decodeHost(
        commandsWithRoot({entry(QStringLiteral("a"), QString::fromLatin1("x\x1b[31my\x07"))}));
    ASSERT_TRUE(clean.has_value());
    const std::vector<std::string> a{"a"};
    const std::string help = std::get<HostCommands>(*clean).tree->find(a)->help;
    EXPECT_EQ(help.find('\x1b'), std::string::npos) << help;
    EXPECT_EQ(help.find('\x07'), std::string::npos) << help;
}

TEST(Tunnel, LearnRequests) {
    auto decoded = decodeJoiner(encodeJoiner(Learn{.path = {"app", "config"}}));
    ASSERT_TRUE(decoded.has_value()) << decoded.error().detail;
    EXPECT_EQ(std::get<Learn>(*decoded).path, (std::vector<std::string>{"app", "config"}));
    for (const char* text : {
             R"({"hwio":{"v":1,"type":"learn"}})",
             R"({"hwio":{"v":1,"type":"learn","path":[]}})",
             R"({"hwio":{"v":1,"type":"learn","path":["a b"]}})",
             R"({"hwio":{"v":1,"type":"learn","path":[1]}})",
             R"({"hwio":{"v":1,"type":"learn","path":["a","a","a","a","a","a","a","a","a"]}})",
         }) {
        EXPECT_FALSE(decodeJoiner(json(text)).has_value()) << text;
    }
}

} // namespace
} // namespace hwio::remote::tunnel
