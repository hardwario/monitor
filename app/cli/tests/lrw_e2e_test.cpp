// End-to-end tests of `hwio chester lrw` (plan/features/0040 A4, ADR-0277, ADR-0352): the real
// `hwio` executable with the fake J-Link library on a two-connector CHESTER (app = nRF52840,
// lrw = STM32L0 cat. 5 192 KiB that answers only while the app chip is held, "needsHalted"), the
// prepared firmware from a local HTTP fixture (test-only HWIO_DESIGNATED_URL / _SHA256).

#include "cli_process.h"
#include "hex_builder.h"
#include "http_fixture.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#ifdef _WIN32
#include <QDir>
#include <QLibraryInfo>
#endif

namespace {

using hwio::cli::test::CliProcess;
using hwio::cli::test::RunResult;
using hwio::firmware::test::HttpFixture;

std::vector<QJsonObject> envelopes(const std::string& out) {
    std::vector<QJsonObject> result;
    for (const QByteArray& line : QByteArray::fromStdString(out).split('\n')) {
        if (!line.isEmpty()) {
            result.push_back(QJsonDocument::fromJson(line).object());
        }
    }
    return result;
}

QJsonObject finalData(const RunResult& result) {
    const auto all = envelopes(result.out);
    return all.empty() ? QJsonObject() : all.back().value(QStringLiteral("data")).toObject();
}

QString errorCode(const RunResult& result) {
    const auto all = envelopes(result.out);
    return all.empty() ? QString()
                       : all.back()
                             .value(QStringLiteral("error"))
                             .toObject()
                             .value(QStringLiteral("code"))
                             .toString();
}

// Intel HEX at the STM32 flash start (0x08000000).
QByteArray moduleHex() {
    return QByteArray::fromStdString(hwio::firmware::test::linearAddress(0x0800) +
                                     hwio::firmware::test::hexRecord(0x00, 0x0000, {1, 2, 3, 4}) +
                                     hwio::firmware::test::eofRecord());
}

QString sha256Hex(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

class LrwCli : public CliProcess {
protected:
    void SetUp() override {
        board(QStringLiteral("lrw"), true, 0);
        writeFile(QStringLiteral("fw.hex"), moduleHex());
    }

    // The CHESTER board: the cable on `connector`, the app MCU held or running, the module's RDP.
    void board(const QString& connector, bool appHeld, int rdp) const {
        writeFile(QStringLiteral("connector"), connector.toUtf8());
        writeFile(QStringLiteral("scenario.json"),
                  QStringLiteral(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
                      "connectors": {"file": "%1", "initial": "app", "chips": {
                        "app": {"halted": %2},
                        "lrw": {"family": "stm32l0", "devId": 1095, "revId": 8200,
                                "flashKib": 192, "eepromBytes": 6144, "rdp": %3,
                                "needsHalted": "app"}}}}]})")
                      .arg(path(QStringLiteral("connector")),
                           appHeld ? QStringLiteral("true") : QStringLiteral("false"))
                      .arg(rdp)
                      .toUtf8());
    }

    void writeFile(const QString& name, const QByteArray& bytes) const {
        ASSERT_TRUE(hwio::firmware::test::writeFile(path(name), bytes));
    }
    [[nodiscard]] QString path(const QString& name) const {
        return home() + QLatin1Char('/') + name;
    }

    [[nodiscard]] QProcessEnvironment fakeEnvironment() const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HWIO_FAKE_JLINK_SCENARIO"),
                   path(QStringLiteral("scenario.json")));
        env.insert(QStringLiteral("HWIO_JLINK_LIBRARY"), QStringLiteral(HWIO_FAKE_JLINK_LIBRARY));
#ifdef _WIN32
        env.insert(QStringLiteral("PATH"),
                   QDir::toNativeSeparators(QLibraryInfo::path(QLibraryInfo::BinariesPath)));
#endif
        return env;
    }

    [[nodiscard]] RunResult hwio(const QStringList& args) const {
        return run(args, fakeEnvironment());
    }

    // Runs the child while this thread's event loop keeps running (the HTTP fixture lives here).
    [[nodiscard]] static RunResult runWithEvents(const QStringList& args,
                                                 const QProcessEnvironment& env) {
        QProcess process;
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(HWIO_CLI_EXECUTABLE), args);
        RunResult result;
        if (!process.waitForStarted(10000)) {
            return result;
        }
        process.closeWriteChannel();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (process.state() != QProcess::NotRunning &&
               std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            process.waitForFinished(5);
        }
        result.exitCode = process.state() == QProcess::NotRunning ? process.exitCode() : -1;
        result.out = process.readAllStandardOutput().toStdString();
        result.err = process.readAllStandardError().toStdString();
        normalizeLineEnds(result);
        return result;
    }
};

TEST_F(LrwCli, InfoReportsTheModule) {
    board(QStringLiteral("lrw"), true, 1);
    const RunResult result = hwio({QStringLiteral("--json"), QStringLiteral("chester"),
                                   QStringLiteral("lrw"), QStringLiteral("info")});
    ASSERT_EQ(result.exitCode, 0) << result.out << result.err;
    const QJsonObject info = finalData(result);
    EXPECT_EQ(info.value(QStringLiteral("selected")).toString(), QStringLiteral("lrw"));
    EXPECT_TRUE(info.value(QStringLiteral("matches")).toBool());
    EXPECT_EQ(info.value(QStringLiteral("chip")).toString(), QStringLiteral("stm32l0-cat5-192k"));
    EXPECT_EQ(info.value(QStringLiteral("devId")).toInt(), 0x447);
    EXPECT_EQ(info.value(QStringLiteral("flashKib")).toInt(), 192);
    EXPECT_EQ(info.value(QStringLiteral("rdp")).toInt(), 1);
    const RunResult human =
        hwio({QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("info")});
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_NE(human.out.find("unlock: `hwio chester lrw unlock`"), std::string::npos) << human.out;
}

// The module does not answer while the application runs: info and reset say how to hold it.
TEST_F(LrwCli, ANotHeldModuleGetsTheHoldHint) {
    board(QStringLiteral("lrw"), false, 0);
    for (const QString& command : {QStringLiteral("info"), QStringLiteral("reset")}) {
        const RunResult result = hwio({QStringLiteral("chester"), QStringLiteral("lrw"), command});
        EXPECT_NE(result.exitCode, 0) << result.out << result.err;
        EXPECT_NE(result.err.find("hwio chester app reset --halt"), std::string::npos)
            << command.toStdString() << ": " << result.err;
    }
    board(QStringLiteral("lrw"), true, 0);
    const RunResult reset = hwio({QStringLiteral("--json"), QStringLiteral("chester"),
                                  QStringLiteral("lrw"), QStringLiteral("reset")});
    EXPECT_EQ(reset.exitCode, 0) << reset.out << reset.err;
}

// Without a terminal (and with --json) the guided commands need --direct.
TEST_F(LrwCli, GuidedCommandsNeedATerminalOrDirect) {
    for (const QStringList& args :
         {QStringList{QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("flash"),
                      path(QStringLiteral("fw.hex"))},
          QStringList{QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("lrw"),
                      QStringLiteral("erase"), QStringLiteral("--yes")},
          QStringList{QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("unlock"),
                      QStringLiteral("--yes")}}) {
        const RunResult result = hwio(args);
        EXPECT_EQ(result.exitCode, 2) << result.out << result.err;
        EXPECT_NE((result.out + result.err).find("--direct"), std::string::npos)
            << result.out << result.err;
    }
    const RunResult halt =
        hwio({QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("flash"),
              QStringLiteral("--halt"), path(QStringLiteral("fw.hex"))});
    EXPECT_EQ(halt.exitCode, 2);
    EXPECT_NE(halt.err.find("--halt needs --direct"), std::string::npos) << halt.err;
}

TEST_F(LrwCli, DirectFlashEraseAndUnlock) {
    const RunResult flash =
        hwio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("lrw"),
              QStringLiteral("flash"), QStringLiteral("--direct"), path(QStringLiteral("fw.hex"))});
    ASSERT_EQ(flash.exitCode, 0) << flash.out << flash.err;
    const QJsonObject final = finalData(flash);
    EXPECT_EQ(final.value(QStringLiteral("profile")).toString(), QStringLiteral("chester"));
    EXPECT_EQ(final.value(QStringLiteral("target")).toString(), QStringLiteral("lrw"));
    EXPECT_EQ(final.value(QStringLiteral("part")).toString(),
              QStringLiteral("STM32L0 cat. 5, 192 KiB"));
    EXPECT_EQ(
        final.value(QStringLiteral("image")).toObject().value(QStringLiteral("bytes")).toInt(), 4);
    EXPECT_EQ(hwio({QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("erase"),
                    QStringLiteral("--direct"), QStringLiteral("--yes")})
                  .exitCode,
              0);
    // A protected module: flash refused with the unlock command; --direct unlock works.
    board(QStringLiteral("lrw"), true, 1);
    const RunResult protectedFlash =
        hwio({QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("flash"),
              QStringLiteral("--direct"), path(QStringLiteral("fw.hex"))});
    EXPECT_NE(protectedFlash.exitCode, 0);
    EXPECT_NE(protectedFlash.err.find("hwio chester lrw unlock"), std::string::npos)
        << protectedFlash.err;
    const RunResult unlock =
        hwio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("lrw"),
              QStringLiteral("unlock"), QStringLiteral("--direct"), QStringLiteral("--yes")});
    ASSERT_EQ(unlock.exitCode, 0) << unlock.out << unlock.err;
    EXPECT_EQ(finalData(unlock).value(QStringLiteral("rdp")).toInt(), 0);
}

// `flash` without FILE: the prepared lora-modem firmware, its name and version printed before the
// download, verified, cached; --no-cache downloads again.
TEST_F(LrwCli, DirectFlashOfThePreparedFirmware) {
#ifndef HWIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HWIO_TEST_OVERRIDES (HWIO_DESIGNATED_URL)";
#endif
    HttpFixture http;
    ASSERT_TRUE(http.listening());
    const QByteArray hex = moduleHex();
    http.set(QStringLiteral("/chester/lora-modem.hex"), HttpFixture::Response::ok(hex));
    QProcessEnvironment env = fakeEnvironment();
    env.insert(QStringLiteral("HWIO_DESIGNATED_URL"),
               http.baseUrl() + QStringLiteral("/lora-modem.hex"));
    env.insert(QStringLiteral("HWIO_DESIGNATED_SHA256"), sha256Hex(hex));
    const QStringList args{QStringLiteral("--json"), QStringLiteral("chester"),
                           QStringLiteral("lrw"), QStringLiteral("flash"),
                           QStringLiteral("--direct")};
    const RunResult first = runWithEvents(args, env);
    ASSERT_EQ(first.exitCode, 0) << first.out << first.err;
    const QJsonObject image = finalData(first).value(QStringLiteral("image")).toObject();
    EXPECT_EQ(image.value(QStringLiteral("source")).toString(), QStringLiteral("designated"));
    EXPECT_EQ(image.value(QStringLiteral("name")).toString(),
              QStringLiteral("lora-modem for CHESTER"));
    EXPECT_EQ(image.value(QStringLiteral("version")).toString(), QStringLiteral("1.4.1"));
    EXPECT_FALSE(image.value(QStringLiteral("cached")).toBool());

    const RunResult second = runWithEvents({QStringLiteral("chester"), QStringLiteral("lrw"),
                                            QStringLiteral("flash"), QStringLiteral("--direct")},
                                           env);
    ASSERT_EQ(second.exitCode, 0) << second.err;
    const auto named = second.err.find("Prepared firmware: lora-modem for CHESTER 1.4.1");
    ASSERT_NE(named, std::string::npos) << second.err;
    const auto cached = second.err.find("lora-modem for CHESTER 1.4.1 (cached");
    ASSERT_NE(cached, std::string::npos) << second.err;
    EXPECT_LT(named, cached);
    EXPECT_EQ(http.requests(QStringLiteral("/chester/lora-modem.hex")), 1);

    const RunResult forced = runWithEvents(QStringList(args) << QStringLiteral("--no-cache"), env);
    ASSERT_EQ(forced.exitCode, 0) << forced.err;
    EXPECT_EQ(http.requests(QStringLiteral("/chester/lora-modem.hex")), 2);

    // A file with another hash: firmware.checksum_mismatch, nothing flashed.
    env.insert(QStringLiteral("HWIO_DESIGNATED_SHA256"), sha256Hex("another file"));
    const RunResult mismatch =
        runWithEvents(QStringList(args) << QStringLiteral("--no-cache"), env);
    EXPECT_EQ(mismatch.exitCode, 7) << mismatch.out;
    EXPECT_EQ(errorCode(mismatch), QStringLiteral("firmware.checksum_mismatch"));
}

#ifdef HWIO_TEST_OVERRIDES
// The guided flash end to end (HWIO_TEST_INTERACTIVE stands in for the terminal): the app MCU is
// stopped on APP, the test moves the cable to the LoRaWAN connector, answers the unlock question of
// the protected module, moves the cable back; the app MCU runs again.
class LrwGuided : public LrwCli {
protected:
    struct Child {
        QProcess process;
        QByteArray err;

        bool waitFor(const char* text) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!err.contains(text) && std::chrono::steady_clock::now() < deadline &&
                   process.state() != QProcess::NotRunning) {
                process.waitForReadyRead(50);
                err += process.readAllStandardError();
            }
            err += process.readAllStandardError();
            return err.contains(text);
        }
    };

    void start(Child& child, const QStringList& args) const {
        QProcessEnvironment env = fakeEnvironment();
        env.insert(QStringLiteral("HWIO_TEST_INTERACTIVE"), QStringLiteral("1"));
        child.process.setProcessEnvironment(env);
        child.process.start(QStringLiteral(HWIO_CLI_EXECUTABLE), args);
        ASSERT_TRUE(child.process.waitForStarted(10000));
    }
};

TEST_F(LrwGuided, FlashWithCableMovesAndTheUnlockQuestion) {
    board(QStringLiteral("app"), false, 1);
    Child child;
    start(child, {QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("flash"),
                  path(QStringLiteral("fw.hex"))});
    ASSERT_TRUE(child.waitFor("Move the J-Link cable to the LoRaWAN connector"))
        << child.err.toStdString();
    EXPECT_TRUE(child.err.contains("Stopping the application MCU")) << child.err.toStdString();
    writeFile(QStringLiteral("connector"), "lrw");
    ASSERT_TRUE(child.waitFor("Unlock the LoRaWAN module (it is erased)? [y/N]"))
        << child.err.toStdString();
    EXPECT_TRUE(child.err.contains("original Murata firmware is removed"))
        << child.err.toStdString();
    child.process.write("y\n");
    ASSERT_TRUE(child.waitFor("Move the J-Link cable back to the APP connector."))
        << child.err.toStdString();
    writeFile(QStringLiteral("connector"), "app");
    ASSERT_TRUE(child.process.waitForFinished(60000));
    child.err += child.process.readAllStandardError();
    const std::string out = child.process.readAllStandardOutput().toStdString();
    EXPECT_EQ(child.process.exitCode(), 0) << out << child.err.toStdString();
    EXPECT_NE(out.find("Completed: The LoRaWAN module was flashed with fw.hex and the application "
                       "is running again."),
              std::string::npos)
        << out;
}

TEST_F(LrwGuided, ANoToTheUnlockCancelsAndSaysTheAppIsStopped) {
    board(QStringLiteral("app"), false, 1);
    Child child;
    start(child, {QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("flash"),
                  path(QStringLiteral("fw.hex"))});
    ASSERT_TRUE(child.waitFor("Move the J-Link cable to the LoRaWAN connector"))
        << child.err.toStdString();
    writeFile(QStringLiteral("connector"), "lrw");
    ASSERT_TRUE(child.waitFor("Unlock the LoRaWAN module (it is erased)? [y/N]"))
        << child.err.toStdString();
    child.process.write("n\n");
    ASSERT_TRUE(child.process.waitForFinished(60000));
    child.err += child.process.readAllStandardError();
    EXPECT_EQ(child.process.exitCode(), 130) << child.err.toStdString();
    EXPECT_TRUE(child.err.contains("hwio chester app reset")) << child.err.toStdString();
}

TEST_F(LrwGuided, EraseOfAnUnprotectedModule) {
    board(QStringLiteral("app"), false, 0);
    Child child;
    start(child, {QStringLiteral("chester"), QStringLiteral("lrw"), QStringLiteral("erase"),
                  QStringLiteral("--yes")});
    ASSERT_TRUE(child.waitFor("Move the J-Link cable to the LoRaWAN connector"))
        << child.err.toStdString();
    writeFile(QStringLiteral("connector"), "lrw");
    ASSERT_TRUE(child.waitFor("Move the J-Link cable back to the APP connector."))
        << child.err.toStdString();
    writeFile(QStringLiteral("connector"), "app");
    ASSERT_TRUE(child.process.waitForFinished(60000));
    const std::string out = child.process.readAllStandardOutput().toStdString();
    EXPECT_EQ(child.process.exitCode(), 0) << out << child.err.toStdString();
    EXPECT_NE(out.find("The LoRaWAN module was erased"), std::string::npos) << out;
}
#endif

} // namespace
