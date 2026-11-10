// `hwio remote host` / `hwio remote join` end to end (plan 0044 B1): the real executable against
// the in-process FakeRelay (libs/remote/tests), with the fake J-Link library / fake BLE backend on
// the host side and an in-process RemoteHost or a scripted web Terminal on the other side.

#include "cli_process.h"
#include "fake_console.h"
#include "fake_relay.h"
#include "hwio/device/console_line.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/remote_host.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "remote_test_support.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;
using hwio::cli::test::CliProcess;
using hwio::remote::testing::FakeConsole;
using hwio::remote::testing::FakeOperations;
using hwio::remote::testing::FakeRelay;
using hwio::remote::testing::RawParticipant;
using hwio::remote::testing::runFor;
using hwio::remote::testing::runUntil;
using hwio::remote::testing::ScriptedConsent;

constexpr const char* scenario = R"({
  "probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "boot banner\n"}],
             "responses": {"version": "Firmware version: v3.1.0\n"}}}
  ]})";

// One running `hwio` child; the test's event loop keeps running (the FakeRelay lives on it).
class Child {
public:
    Child(const QStringList& args, const QProcessEnvironment& env) {
        m_process.setProcessEnvironment(env);
        QObject::connect(&m_process, &QProcess::readyReadStandardOutput, &m_process,
                         [this] { m_out += m_process.readAllStandardOutput(); });
        QObject::connect(&m_process, &QProcess::readyReadStandardError, &m_process,
                         [this] { m_err += m_process.readAllStandardError(); });
        m_process.start(QStringLiteral(HWIO_CLI_EXECUTABLE), args);
    }
    ~Child() {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(5000);
        }
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    Child(Child&&) = delete;
    Child& operator=(Child&&) = delete;

    void closeInput() { m_process.closeWriteChannel(); }
    void terminate() { m_process.terminate(); }
    bool finish(std::chrono::milliseconds timeout = 30s) {
        return runUntil([this] { return m_process.state() == QProcess::NotRunning; }, timeout);
    }
    [[nodiscard]] int exitCode() const { return m_process.exitCode(); }
    [[nodiscard]] std::string err() const { return m_err.toStdString(); }
    [[nodiscard]] std::string out() const { return m_out.toStdString(); }
    // The NDJSON envelopes printed so far.
    [[nodiscard]] std::vector<QJsonObject> envelopes() const {
        std::vector<QJsonObject> result;
        for (const QByteArray& line : m_out.split('\n')) {
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject()) {
                result.push_back(doc.object());
            }
        }
        return result;
    }
    // The data of the first `remote` event `name`.
    [[nodiscard]] std::optional<QJsonObject> remoteEvent(const QString& name) const {
        for (const QJsonObject& envelope : envelopes()) {
            QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
            if (data.value(QStringLiteral("type")) == QStringLiteral("remote") &&
                data.value(QStringLiteral("event")) == name) {
                return data;
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] QString lastErrorCode() const {
        const auto all = envelopes();
        return all.empty() ? QString()
                           : all.back()
                                 .value(QStringLiteral("error"))
                                 .toObject()
                                 .value(QStringLiteral("code"))
                                 .toString();
    }

private:
    QProcess m_process;
    QByteArray m_out;
    QByteArray m_err;
};

class RemoteCli : public CliProcess {
protected:
    void SetUp() override {
        QFile file(home() + QStringLiteral("/scenario.json"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(scenario);
    }
    [[nodiscard]] QProcessEnvironment fakeEnvironment() const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HWIO_FAKE_JLINK_SCENARIO"),
                   home() + QStringLiteral("/scenario.json"));
        env.insert(QStringLiteral("HWIO_JLINK_LIBRARY"), QStringLiteral(HWIO_FAKE_JLINK_LIBRARY));
        env.insert(QStringLiteral("HWIO_FAKE_BLE_SCENARIO"),
                   QStringLiteral(HWIO_BLE_SCENARIOS_DIR "/chester.json"));
        return env;
    }
    [[nodiscard]] static QStringList relayArgs(const FakeRelay& relay) {
        return {QStringLiteral("--relay"), relay.url().toString()};
    }

    // An in-process Monitor host on `relay`, code 482913.
    struct InProcessHost {
        InProcessHost(const FakeRelay& relay, hwio::remote::HostOperations* operations)
            : host(console, consent, operations,
                   hwio::remote::HostOptions{.relay = relay.url(),
                                             .limits = limits(),
                                             .appVersion = "2.1.0-customer",
                                             .code =
                                                 *hwio::remote::SessionCode::fromValue(482913)}) {
            host.start();
        }
        static hwio::remote::HostLimits limits() {
            hwio::remote::HostLimits l;
            l.collisionWindow = 100ms;
            return l;
        }
        FakeConsole console;
        ScriptedConsent consent;
        hwio::remote::RemoteHost host;
    };
};

TEST_F(RemoteCli, HostSharesAndWithoutATerminalEndsAtTheFirstJoin) {
    FakeRelay relay;
    QStringList args{QStringLiteral("--json"), QStringLiteral("remote"), QStringLiteral("host")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    child.closeInput();
    ASSERT_TRUE(
        runUntil([&] { return child.remoteEvent(QStringLiteral("sharing")).has_value(); }, 30s))
        << child.err();
    const QJsonObject sharing =
        child.remoteEvent(QStringLiteral("sharing")).value_or(QJsonObject());
    const QString code = sharing.value(QStringLiteral("code")).toString();
    EXPECT_EQ(code.size(), 6);
    EXPECT_TRUE(sharing.value(QStringLiteral("link"))
                    .toString()
                    .endsWith(QStringLiteral("/?session=") + code));

    RawParticipant web(relay.url(), QStringLiteral("/") + code);
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 130) << child.err(); // remote.join_not_allowed: cancelled
    EXPECT_EQ(child.lastErrorCode(), QStringLiteral("remote.join_not_allowed")) << child.out();
    ASSERT_TRUE(child.remoteEvent(QStringLiteral("ended")).has_value());
    EXPECT_TRUE(web.named(QStringLiteral("sync")).isEmpty());
}

TEST_F(RemoteCli, HostStreamsTheConsoleAndCtrlCEndsSharing) {
    FakeRelay relay;
    QStringList args{QStringLiteral("--json"), QStringLiteral("remote"), QStringLiteral("host")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(
        runUntil([&] { return child.remoteEvent(QStringLiteral("sharing")).has_value(); }, 30s))
        << child.err();
    child.terminate();
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 130) << child.err();
    EXPECT_TRUE(runUntil([&] { return relay.socketCount() == 0; }));
}

TEST_F(RemoteCli, HostOverBluetooth) {
    FakeRelay relay;
    QStringList args{QStringLiteral("--json"), QStringLiteral("remote"),
                     QStringLiteral("host"),   QStringLiteral("--ble"),
                     QStringLiteral("-a"),     QStringLiteral("F1:22:33:44:55:66")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(
        runUntil([&] { return child.remoteEvent(QStringLiteral("sharing")).has_value(); }, 30s))
        << child.err();
    child.terminate();
    ASSERT_TRUE(child.finish());
}

TEST_F(RemoteCli, HostRejectsAnHttpRelay) {
    const auto result =
        run({QStringLiteral("remote"), QStringLiteral("host"), QStringLiteral("--relay"),
             QStringLiteral("http://terminal.hardwario.com")},
            fakeEnvironment());
    EXPECT_EQ(result.exitCode, 2) << result.err;
    EXPECT_NE(result.err.find("support server address"), std::string::npos) << result.err;
}

TEST_F(RemoteCli, JoinSendsOneCommandAfterTheCustomerAllows) {
    FakeRelay relay;
    InProcessHost host(relay, nullptr);
    ASSERT_TRUE(runUntil([&] { return host.host.state() == hwio::remote::HostState::Sharing; }));
    QObject::connect(&host.console, &FakeConsole::linesReceived, &host.console,
                     [&](const QList<hwio::device::ConsoleLine>& lines) {
                         for (const auto& line : lines) {
                             if (line.dir == hwio::device::Direction::In &&
                                 line.text == "version") {
                                 host.console.shell("Firmware version: v3.1.0");
                             }
                         }
                     });
    QStringList args{QStringLiteral("--json"),    QStringLiteral("remote"),
                     QStringLiteral("join"),      QStringLiteral("482 913"),
                     QStringLiteral("--command"), QStringLiteral("version")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 0) << child.err();
    const QJsonObject data = child.envelopes().back().value(QStringLiteral("data")).toObject();
    EXPECT_EQ(data.value(QStringLiteral("output")).toArray().at(0).toString(),
              QStringLiteral("Firmware version: v3.1.0"))
        << child.out();
    ASSERT_EQ(host.console.sent.size(), 1U);
    EXPECT_EQ(host.console.sent.front().source, hwio::device::LineSource::Remote);
}

TEST_F(RemoteCli, JoinNotAllowed) {
    FakeRelay relay;
    InProcessHost host(relay, nullptr);
    host.consent.joins = ScriptedConsent::Mode::Deny;
    ASSERT_TRUE(runUntil([&] { return host.host.state() == hwio::remote::HostState::Sharing; }));
    QStringList args{QStringLiteral("--json"),    QStringLiteral("remote"),
                     QStringLiteral("join"),      QStringLiteral("482913"),
                     QStringLiteral("--command"), QStringLiteral("version"),
                     QStringLiteral("-t"),        QStringLiteral("0.5")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    // The host ends the session; the relay never says so: the joiner waits up to the allow
    // timeout. End it sooner by stopping the relay.
    ASSERT_TRUE(runUntil([&] { return host.host.state() == hwio::remote::HostState::Ended; }));
    EXPECT_TRUE(host.console.sent.empty());
    child.terminate();
    ASSERT_TRUE(child.finish());
    EXPECT_EQ(child.exitCode(), 130);
}

TEST_F(RemoteCli, JoinRequestsAResetAndTheCustomerAllows) {
    FakeRelay relay;
    FakeOperations operations;
    InProcessHost host(relay, &operations);
    ASSERT_TRUE(runUntil([&] { return host.host.state() == hwio::remote::HostState::Sharing; }));
    QStringList args{QStringLiteral("--json"),  QStringLiteral("remote"),
                     QStringLiteral("join"),    QStringLiteral("482913"),
                     QStringLiteral("--reset"), QStringLiteral("--target"),
                     QStringLiteral("lte")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(runUntil([&] { return operations.started.size() == 1; }, 30s)) << child.err();
    EXPECT_EQ(operations.started.front().kind, hwio::remote::tunnel::OpKind::Reset);
    EXPECT_EQ(operations.started.front().target, hwio::remote::tunnel::OpTarget::Lte);
    operations.finished(hwio::Status{});
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 0) << child.err();
    const QJsonObject data = child.envelopes().back().value(QStringLiteral("data")).toObject();
    EXPECT_EQ(data.value(QStringLiteral("operation")).toString(), QStringLiteral("reset"));
}

TEST_F(RemoteCli, JoinOperationDenied) {
    FakeRelay relay;
    FakeOperations operations;
    InProcessHost host(relay, &operations);
    host.consent.operations = ScriptedConsent::Mode::Deny;
    ASSERT_TRUE(runUntil([&] { return host.host.state() == hwio::remote::HostState::Sharing; }));
    QStringList args{QStringLiteral("--json"),  QStringLiteral("remote"),
                     QStringLiteral("join"),    QStringLiteral("482913"),
                     QStringLiteral("--flash"), QStringLiteral("0123456789abcdef0123456789abcdef")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 130) << child.err();
    EXPECT_EQ(child.lastErrorCode(), QStringLiteral("remote.operation_denied"));
    ASSERT_EQ(host.consent.operationQuestions.size(), 1U);
    EXPECT_EQ(host.consent.operationQuestions.front().firmwareName, "CHESTER-Clime");
}

// The web Terminal host (console.tsx): a sync on every newClient, no hello.
TEST_F(RemoteCli, JoinWebTerminalHostHasNoOperations) {
    FakeRelay relay;
    RawParticipant webHost(relay.url(), QStringLiteral("/12345"));
    ASSERT_TRUE(webHost.waitConnected());
    QObject::connect(&relay, &FakeRelay::namespaceConnected, &relay, [&](const QString& nsp) {
        if (nsp == QStringLiteral("/12345")) {
            QJsonObject sync;
            sync.insert(QStringLiteral("state"), QStringLiteral("connected"));
            sync.insert(QStringLiteral("commands"), QJsonArray());
            webHost.emitEvent(QStringLiteral("sync"), sync);
        }
    });
    QStringList args{QStringLiteral("--json"), QStringLiteral("remote"), QStringLiteral("join"),
                     QStringLiteral("012345"), QStringLiteral("--reset")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 2) << child.err();
    EXPECT_EQ(child.lastErrorCode(), QStringLiteral("remote.operation_unsupported_peer"));
    for (const auto& write : webHost.named(QStringLiteral("write"))) {
        EXPECT_TRUE(write.payload.isString()); // never a tunnel object to a web host
    }
}

TEST_F(RemoteCli, JoinRejectsAForeignLinkWithoutATerminalAndInvalidCodes) {
    FakeRelay relay;
    QStringList args{QStringLiteral("remote"), QStringLiteral("join"),
                     QStringLiteral("https://evil.example.com/?session=482913")};
    args << relayArgs(relay);
    auto result = run(args, fakeEnvironment());
    EXPECT_EQ(result.exitCode, 2) << result.err;
    EXPECT_NE(result.err.find("another support server"), std::string::npos) << result.err;

    result = run({QStringLiteral("--json"), QStringLiteral("remote"), QStringLiteral("join"),
                  QStringLiteral("12a456")},
                 fakeEnvironment());
    EXPECT_EQ(result.exitCode, 2) << result.err;
    EXPECT_NE(result.out.find("remote.code_invalid"), std::string::npos) << result.out;

    result = run({QStringLiteral("remote"), QStringLiteral("join"), QStringLiteral("482913"),
                  QStringLiteral("--reset"), QStringLiteral("--erase")},
                 fakeEnvironment());
    EXPECT_EQ(result.exitCode, 2) << result.err;
}

TEST_F(RemoteCli, JoinConsoleShowsTheHostConsole) {
    FakeRelay relay;
    InProcessHost host(relay, nullptr);
    ASSERT_TRUE(runUntil([&] { return host.host.state() == hwio::remote::HostState::Sharing; }));
    host.console.shell("before join");
    QStringList args{QStringLiteral("--json"), QStringLiteral("remote"),   QStringLiteral("join"),
                     QStringLiteral("482913"), QStringLiteral("--linger"), QStringLiteral("0")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(runUntil([&] { return host.host.commandsAllowed(); }, 30s)) << child.err();
    host.console.shell("live line");
    ASSERT_TRUE(runUntil([&] { return child.out().find("live line") != std::string::npos; }, 10s))
        << child.out();
    EXPECT_NE(child.out().find("before join"), std::string::npos);
    child.terminate();
    ASSERT_TRUE(child.finish());
}

// ADR-0354: the host's End sharing ends `remote join` with remote.host_ended.
TEST_F(RemoteCli, JoinEndsWhenTheHostEndsSharing) {
    FakeRelay relay;
    InProcessHost host(relay, nullptr);
    ASSERT_TRUE(runUntil([&] { return host.host.state() == hwio::remote::HostState::Sharing; }));
    QStringList args{QStringLiteral("--json"), QStringLiteral("remote"),   QStringLiteral("join"),
                     QStringLiteral("482913"), QStringLiteral("--linger"), QStringLiteral("0")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(runUntil([&] { return host.host.commandsAllowed(); }, 30s)) << child.err();
    runFor(200ms); // the joiner has the host's hello
    host.host.end();
    ASSERT_TRUE(child.finish(10s)) << child.err();
    EXPECT_NE(child.exitCode(), 0);
    EXPECT_EQ(child.lastErrorCode(), QStringLiteral("remote.host_ended")) << child.out();
}

} // namespace
