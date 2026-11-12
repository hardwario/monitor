// `hwio remote host` / `hwio remote join` end to end (plan 0044 B1): the real executable against
// the in-process FakeRelay (libs/remote/tests), with the fake J-Link library / fake BLE backend on
// the host side and an in-process RemoteHost or a scripted web Terminal on the other side.

#include "cli_process.h"
#include "fake_console.h"
#include "fake_relay.h"
#include "hex_builder.h"
#include "http_fixture.h"
#include "hwio/core/error.h"
#include "hwio/device/console_line.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/remote_host.h"
#include "hwio/remote/remote_operation_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "remote_test_support.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QCryptographicHash>
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
#include <QStringLiteral>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
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
    void write(const QByteArray& data) { m_process.write(data); }
#ifndef _WIN32
    // SIGTERM: the CLI handles it like Ctrl+C (exit status 130).
    void terminate() { m_process.terminate(); }
#endif
    // Ends the child for cleanup: SIGTERM on POSIX; on Windows QProcess::terminate() only posts
    // WM_CLOSE, which a console program never sees, so the child is killed (no exit status).
    void stop() {
#ifdef _WIN32
        m_process.kill();
#else
        m_process.terminate();
#endif
    }
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

// Ctrl+C (SIGTERM) and exit status 130: POSIX only, a test cannot send a console Ctrl+C to a
// child on Windows (as mcp_e2e_test.cpp / e2e_test.cpp).
#ifndef _WIN32
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
    // --json: the console's Ctrl+C envelope (cancelled), as `hwio chester app console`.
    EXPECT_EQ(child.lastErrorCode(), QStringLiteral("cancelled")) << child.out();
    EXPECT_TRUE(runUntil([&] { return relay.socketCount() == 0; }));
}

// Human output: Ctrl+C is the normal end of sharing, no error block, cancelled exit status.
TEST_F(RemoteCli, HostCtrlCPrintsSharingEndedWithoutError) {
    FakeRelay relay;
    QStringList args{QStringLiteral("remote"), QStringLiteral("host")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(runUntil(
        [&] { return child.err().find("Press Ctrl+C to end sharing.") != std::string::npos; }, 30s))
        << child.err();
    child.terminate();
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 130) << child.err();
    EXPECT_NE(child.err().find("Sharing ended.\n"), std::string::npos) << child.err();
    EXPECT_EQ(child.err().find("Error:"), std::string::npos) << child.err();
    EXPECT_EQ(child.err().find("Cancelled by user."), std::string::npos) << child.err();
}
#endif // _WIN32

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
    child.stop();
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
    child.stop();
    ASSERT_TRUE(child.finish());
#ifndef _WIN32
    EXPECT_EQ(child.exitCode(), 130); // SIGTERM as Ctrl+C; killed on Windows
#endif
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

// ADR-0363: `--lte-update` asks for the guided LTE update (no target, no firmware ID).
TEST_F(RemoteCli, JoinRequestsAnLteUpdate) {
    FakeRelay relay;
    FakeOperations operations;
    InProcessHost host(relay, &operations);
    ASSERT_TRUE(runUntil([&] { return host.host.state() == hwio::remote::HostState::Sharing; }));
    QStringList args{QStringLiteral("--json"), QStringLiteral("remote"), QStringLiteral("join"),
                     QStringLiteral("482913"), QStringLiteral("--lte-update")};
    args << relayArgs(relay);
    Child child(args, fakeEnvironment());
    ASSERT_TRUE(runUntil([&] { return operations.started.size() == 1; }, 30s)) << child.err();
    EXPECT_EQ(operations.started.front().kind, hwio::remote::tunnel::OpKind::LteUpdate);
    ASSERT_EQ(host.consent.operationQuestions.size(), 1U);
    EXPECT_EQ(host.consent.operationQuestions.front().firmwareVersion, "1.7.0");
    operations.progress(hwio::remote::OperationUpdate{.phase = "connect-lte"});
    operations.finished(hwio::Status{});
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 0) << child.err();
    const QJsonObject data = child.envelopes().back().value(QStringLiteral("data")).toObject();
    EXPECT_EQ(data.value(QStringLiteral("operation")).toString(), QStringLiteral("lteUpdate"));
    EXPECT_FALSE(data.contains(QStringLiteral("target")));

    // --lte-update has no target.
    const auto refused =
        run({QStringLiteral("remote"), QStringLiteral("join"), QStringLiteral("482913"),
             QStringLiteral("--lte-update"), QStringLiteral("--target"), QStringLiteral("lte")},
            fakeEnvironment());
    EXPECT_EQ(refused.exitCode, 2) << refused.err;
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
        << child.out() << child.err();
    EXPECT_NE(child.out().find("before join"), std::string::npos) << child.out() << child.err();
    child.closeInput(); // the end of input ends the console (--linger 0)
    ASSERT_TRUE(child.finish()) << child.err();
    EXPECT_EQ(child.exitCode(), 0) << child.err();
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

#ifndef _WIN32
// ADR-0363: `remote host` runs the guided LTE update of `chester lte flash --guided` for an allowed
// lteUpdate request: the customer answers on stdin (HWIO_TEST_INTERACTIVE stands in for the
// terminal) and moves the cable (the fake board's connector file); the joiner sees the phases and
// the result.
TEST_F(RemoteCli, HostRunsTheGuidedLteUpdate) {
    const std::string appHex = hwio::firmware::test::hexRecord(0, 0x1000, {1, 2, 3, 4}) +
                               hwio::firmware::test::eofRecord();
    const QByteArray modem = hwio::firmware::test::zipBytes(
        {{.name = "firmware.update.image.segments.0.hex", .data = appHex}});
    const QByteArray bundle =
        hwio::firmware::test::zipBytes({{.name = "modem.zip", .data = modem.toStdString()},
                                        {.name = "application.hex", .data = appHex}});
    hwio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    http.set(QStringLiteral("/chester/lte.zip"),
             hwio::firmware::test::HttpFixture::Response::ok(bundle));
    const QString connector = home() + QStringLiteral("/connector");
    const auto moveCable = [&](const char* to) {
        QFile file(connector);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(to);
    };
    moveCable("app");
    {
        QFile file(home() + QStringLiteral("/scenario.json"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(
            QStringLiteral(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
            "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "boot banner\n"}]},
            "connectors": {"file": "%1", "chips": {
              "app": {"memory": [{"address": 4096, "hex": "a5a5a5a5"}]},
              "lte": {"family": "nrf91"}}}}]})")
                .arg(connector)
                .toUtf8());
    }
    QProcessEnvironment env = fakeEnvironment();
    env.insert(QStringLiteral("HWIO_TEST_INTERACTIVE"), QStringLiteral("1"));
    env.insert(QStringLiteral("HWIO_DESIGNATED_URL"), http.baseUrl() + QStringLiteral("/lte.zip"));
    env.insert(
        QStringLiteral("HWIO_DESIGNATED_SHA256"),
        QString::fromLatin1(QCryptographicHash::hash(bundle, QCryptographicHash::Sha256).toHex()));
    FakeRelay relay;
    QStringList args{QStringLiteral("--nrfutil"), QStringLiteral(HWIO_FAKE_NRFUTIL),
                     QStringLiteral("remote"), QStringLiteral("host")};
    args << relayArgs(relay);
    Child child(args, env);
    const auto waitErr = [&](const char* text) {
        return runUntil([&] { return child.err().find(text) != std::string::npos; }, 30s);
    };
    ASSERT_TRUE(waitErr("Session code: ")) << child.err();
    const std::string err = child.err();
    const std::string code = err.substr(err.find("Session code: ") + 14, 7); // "482 913"

    const auto target = hwio::remote::parseJoinInput(code);
    ASSERT_TRUE(target.has_value()) << code;
    hwio::remote::RemoteConsoleSession joiner(
        hwio::remote::JoinOptions{.relay = relay.url(),
                                  .code = target->code,
                                  .appVersion = "2.1.0-support",
                                  .relayLimits = {},
                                  .transcript = {},
                                  .idleNoticeAfter = std::chrono::minutes(30),
                                  .consoleLog = nullptr});
    ASSERT_TRUE(waitErr("Allow them to send commands")) << child.err();
    child.write("y\n");
    ASSERT_TRUE(runUntil(
        [&] {
            return joiner.hostHello() &&
                   hwio::remote::tunnel::supports(*joiner.hostHello(),
                                                  hwio::remote::tunnel::OpKind::LteUpdate);
        },
        30s))
        << child.err();
    hwio::remote::RemoteOperationClient client(joiner);
    std::vector<std::string> phases;
    std::optional<std::pair<bool, hwio::Error>> result;
    QObject::connect(&client, &hwio::remote::RemoteOperationClient::progress, &client,
                     [&](const hwio::remote::tunnel::OpProgress& p) { phases.push_back(p.phase); });
    QObject::connect(&client, &hwio::remote::RemoteOperationClient::finished, &client,
                     [&](bool ok, const hwio::Error& error) { result.emplace(ok, error); });
    ASSERT_TRUE(
        client.request(hwio::remote::tunnel::OpKind::LteUpdate, hwio::remote::tunnel::OpTarget::Lte)
            .has_value());
    ASSERT_TRUE(waitErr("Support asks to update the LTE firmware to HARDWARIO LTE firmware 1.7.0"))
        << child.err();
    child.write("y\n");
    ASSERT_TRUE(waitErr("Connect the J-Link cable to the LTE connector.")) << child.err();
    moveCable("lte");
    ASSERT_TRUE(waitErr("Move the cable to the APP connector.")) << child.err();
    moveCable("app");
    ASSERT_TRUE(runUntil([&] { return result.has_value(); }, 60s)) << child.err();
    EXPECT_TRUE(result && result->first) << (result ? result->second.detail : "") << child.err();
    EXPECT_NE(std::ranges::find(phases, "download"), phases.end());
    EXPECT_NE(std::ranges::find(phases, "connect-lte"), phases.end());
    EXPECT_EQ(joiner.joinState(), hwio::remote::JoinState::Allowed); // sharing stayed active
    child.stop();
    ASSERT_TRUE(child.finish());
}
#endif // _WIN32

// ADR-0364: `remote host` learns its command tree (`help`) and sends it to an allowed joiner; a
// joiner's learn request makes it ask `<command> -h` silently and send the grown tree.
TEST_F(RemoteCli, HostSendsItsCommandTreeAndLearnsForTheJoiner) {
    {
        QFile file(home() + QStringLiteral("/scenario.json"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
            "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "boot banner\n"}],
                    "responses": {
                      "help": "Please press the <Tab> button to see all available commands.\nYou can try to call commands with <-h> or <--help> parameter for more information.\n\nAvailable commands:\n  app   :Application commands\n  info  :Info commands\n",
                      "app -h": "app - Application commands\nSubcommands:\n  config  :Configuration commands\n"}}}]})");
    }
    QProcessEnvironment env = fakeEnvironment();
    env.insert(QStringLiteral("HWIO_TEST_INTERACTIVE"), QStringLiteral("1"));
    FakeRelay relay;
    QStringList args{QStringLiteral("remote"), QStringLiteral("host")};
    args << relayArgs(relay);
    Child child(args, env);
    ASSERT_TRUE(
        runUntil([&] { return child.err().find("Session code: ") != std::string::npos; }, 30s))
        << child.err();
    const std::string err = child.err();
    const auto target =
        hwio::remote::parseJoinInput(err.substr(err.find("Session code: ") + 14, 7));
    ASSERT_TRUE(target.has_value());
    hwio::remote::RemoteConsoleSession joiner(
        hwio::remote::JoinOptions{.relay = relay.url(),
                                  .code = target->code,
                                  .appVersion = "2.1.0-support",
                                  .relayLimits = {},
                                  .transcript = {},
                                  .idleNoticeAfter = std::chrono::minutes(30),
                                  .consoleLog = nullptr});
    ASSERT_TRUE(runUntil(
        [&] { return child.err().find("Allow them to send commands") != std::string::npos; }, 30s))
        << child.err();
    child.write("y\n");
    const std::vector<std::string> app{"app"};
    ASSERT_TRUE(runUntil(
        [&] { return joiner.commandTree() && joiner.commandTree()->find(app) != nullptr; }, 30s))
        << child.err();
    joiner.requestLearn(app);
    const std::vector<std::string> config{"app", "config"};
    ASSERT_TRUE(runUntil(
        [&] { return joiner.commandTree() && joiner.commandTree()->find(config) != nullptr; }, 30s))
        << child.err();
    // The help queries are silent: support sees no `help` / `app -h` in the console.
    for (const auto& line : joiner.log().linesSince(0)) {
        EXPECT_NE(line.text, "app -h");
        EXPECT_NE(line.text, "help");
    }
    child.stop();
    ASSERT_TRUE(child.finish());
}

} // namespace
