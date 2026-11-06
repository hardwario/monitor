// End-to-end tests of `hwio stm32` (plan/features/0039, B1): the real `hwio` executable with the
// fake J-Link library's STM32L0 model (tests/fake_jlink).

#include "cli_process.h"
#include "hex_builder.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#ifdef _WIN32
#include <QDir>
#include <QLibraryInfo>
#endif

namespace {

using hwio::cli::test::CliProcess;
using hwio::cli::test::RunResult;

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

class Stm32Cli : public CliProcess {
protected:
    void SetUp() override {
        chip(QStringLiteral(R"("devId": 1047, "flashKib": 64, "eepromBytes": 2048, "rdp": 0)"));
        writeFile(QStringLiteral("fw.bin"), QByteArray(4096, '\x5A'));
        writeFile(
            QStringLiteral("fw.hex"),
            QByteArray::fromStdString(hwio::firmware::test::linearAddress(0x0800) +
                                      hwio::firmware::test::hexRecord(0x00, 0x0000, {1, 2, 3, 4}) +
                                      hwio::firmware::test::eofRecord()));
    }

    // The probe's chip: an STM32L0 with these extra keys.
    void chip(const QString& keys) const {
        writeFile(QStringLiteral("scenario.json"),
                  QStringLiteral(R"({"probes": [{"serialNumber": 801000001,
                      "product": "Test Probe", "chip": {"family": "stm32l0", %1,
                      "ram": [{"address": 536870912, "size": 8192}]}}]})")
                      .arg(keys)
                      .toUtf8());
    }

    void writeFile(const QString& name, const QByteArray& bytes) const {
        ASSERT_TRUE(hwio::firmware::test::writeFile(path(name), bytes));
    }
    [[nodiscard]] QString path(const QString& name) const {
        return home() + QLatin1Char('/') + name;
    }

    [[nodiscard]] RunResult hwio(const QStringList& args) const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HWIO_FAKE_JLINK_SCENARIO"),
                   path(QStringLiteral("scenario.json")));
        env.insert(QStringLiteral("HWIO_JLINK_LIBRARY"), QStringLiteral(HWIO_FAKE_JLINK_LIBRARY));
#ifdef _WIN32
        env.insert(QStringLiteral("PATH"),
                   QDir::toNativeSeparators(QLibraryInfo::path(QLibraryInfo::BinariesPath)));
#endif
        return run(args, env);
    }
};

TEST_F(Stm32Cli, ChipsListsTheCuratedParts) {
    const RunResult result =
        hwio({QStringLiteral("--json"), QStringLiteral("stm32"), QStringLiteral("chips")});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    const QJsonArray chips = finalData(result).value(QStringLiteral("chips")).toArray();
    ASSERT_EQ(chips.size(), 9);
    EXPECT_EQ(chips.at(5).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("stm32l0-cat3-64k"));
    EXPECT_EQ(chips.at(5).toObject().value(QStringLiteral("devId")).toInt(), 0x417);
    EXPECT_EQ(chips.at(5).toObject().value(QStringLiteral("jlinkDevice")).toString(),
              QStringLiteral("STM32L053R8"));
    EXPECT_TRUE(chips.at(6).toObject().value(QStringLiteral("dataEepromBytes")).isNull());
}

TEST_F(Stm32Cli, UnknownChipIdIsAUsageError) {
    const RunResult result = hwio({QStringLiteral("stm32"), QStringLiteral("info"),
                                   QStringLiteral("--chip"), QStringLiteral("stm32f4")});
    EXPECT_EQ(result.exitCode, 2) << result.err;
    EXPECT_NE(result.err.find("hwio stm32 chips"), std::string::npos) << result.err;
}

TEST_F(Stm32Cli, InfoReportsRdpLevel1AndSucceeds) {
    chip(QStringLiteral(R"("devId": 1047, "flashKib": 64, "rdp": 1)"));
    const RunResult result =
        hwio({QStringLiteral("--json"), QStringLiteral("stm32"), QStringLiteral("info"),
              QStringLiteral("--chip"), QStringLiteral("stm32l0-cat3-64k")});
    ASSERT_EQ(result.exitCode, 0) << result.out << result.err;
    const QJsonObject info = finalData(result);
    EXPECT_EQ(info.value(QStringLiteral("rdp")).toInt(), 1);
    EXPECT_EQ(info.value(QStringLiteral("devId")).toInt(), 0x417);
    EXPECT_EQ(info.value(QStringLiteral("flashKib")).toInt(), 64);
    EXPECT_TRUE(info.value(QStringLiteral("matches")).toBool());
    EXPECT_EQ(info.value(QStringLiteral("chip")).toString(), QStringLiteral("stm32l0-cat3-64k"));
}

TEST_F(Stm32Cli, InfoOfAnotherChipSaysSo) {
    const RunResult result = hwio({QStringLiteral("stm32"), QStringLiteral("info"),
                                   QStringLiteral("--chip"), QStringLiteral("stm32l0-cat5-192k")});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_NE(result.out.find("does NOT match"), std::string::npos) << result.out;
}

TEST_F(Stm32Cli, FlashBinAtTheFlashStart) {
    const RunResult result =
        hwio({QStringLiteral("--json"), QStringLiteral("stm32"), QStringLiteral("flash"),
              QStringLiteral("--chip"), QStringLiteral("stm32l0-cat3-64k"),
              path(QStringLiteral("fw.bin"))});
    ASSERT_EQ(result.exitCode, 0) << result.out << result.err;
    const QJsonObject final = finalData(result);
    EXPECT_EQ(final.value(QStringLiteral("part")).toString(),
              QStringLiteral("STM32L0 cat. 3, 64 KiB"));
    EXPECT_EQ(final.value(QStringLiteral("rdp")).toInt(), 0);
    const QJsonObject image = final.value(QStringLiteral("image")).toObject();
    EXPECT_EQ(image.value(QStringLiteral("address")).toString(), QStringLiteral("0x08000000"));
    EXPECT_EQ(image.value(QStringLiteral("bytes")).toInt(), 4096);
}

TEST_F(Stm32Cli, FlashHexWithEraseAllNeedsYesWithoutATerminal) {
    const QStringList args{QStringLiteral("stm32"),       QStringLiteral("flash"),
                           QStringLiteral("--chip"),      QStringLiteral("stm32l0-cat3-64k"),
                           QStringLiteral("--erase-all"), path(QStringLiteral("fw.hex"))};
    EXPECT_EQ(hwio(args).exitCode, 2);
    const RunResult result = hwio(QStringList(args) << QStringLiteral("--yes"));
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_NE(result.err.find("Erasing..."), std::string::npos) << result.err;
}

TEST_F(Stm32Cli, InvalidAddressAndEepromConsent) {
    EXPECT_EQ(hwio({QStringLiteral("stm32"), QStringLiteral("flash"), QStringLiteral("--chip"),
                    QStringLiteral("stm32l0-cat3-64k"), QStringLiteral("--address"),
                    QStringLiteral("nope"), path(QStringLiteral("fw.bin"))})
                  .exitCode,
              2);
    writeFile(QStringLiteral("eeprom.bin"), QByteArray(16, '\x01'));
    const QStringList eeprom{QStringLiteral("--json"),
                             QStringLiteral("stm32"),
                             QStringLiteral("flash"),
                             QStringLiteral("--chip"),
                             QStringLiteral("stm32l0-cat3-64k"),
                             QStringLiteral("--address"),
                             QStringLiteral("0x08080000"),
                             path(QStringLiteral("eeprom.bin"))};
    const RunResult refused = hwio(eeprom);
    EXPECT_EQ(errorCode(refused), QStringLiteral("firmware.out_of_range")) << refused.out;
    const RunResult allowed = hwio(QStringList(eeprom) << QStringLiteral("--allow-eeprom"));
    EXPECT_EQ(allowed.exitCode, 0) << allowed.out << allowed.err;
}

TEST_F(Stm32Cli, WrongChipNamesTheFoundChip) {
    const RunResult result =
        hwio({QStringLiteral("--json"), QStringLiteral("stm32"), QStringLiteral("flash"),
              QStringLiteral("--chip"), QStringLiteral("stm32l0-cat5-192k"),
              path(QStringLiteral("fw.bin"))});
    EXPECT_EQ(errorCode(result), QStringLiteral("target.wrong_chip")) << result.out;
    EXPECT_NE(result.out.find("stm32l0-cat3-64k"), std::string::npos) << result.out;
}

TEST_F(Stm32Cli, ProtectedChipIsNotFlashedAndNamesTheUnlock) {
    chip(QStringLiteral(R"("devId": 1047, "flashKib": 64, "rdp": 1)"));
    const RunResult result =
        hwio({QStringLiteral("stm32"), QStringLiteral("flash"), QStringLiteral("--chip"),
              QStringLiteral("stm32l0-cat3-64k"), path(QStringLiteral("fw.bin"))});
    EXPECT_EQ(result.exitCode, 6) << result.err;
    EXPECT_NE(result.err.find("hwio stm32 unlock --chip stm32l0-cat3-64k"), std::string::npos)
        << result.err;
}

TEST_F(Stm32Cli, Level2IsPermanent) {
    chip(QStringLiteral(R"("devId": 1047, "flashKib": 64, "rdp": 2)"));
    const RunResult result = hwio({QStringLiteral("--json"), QStringLiteral("stm32"),
                                   QStringLiteral("unlock"), QStringLiteral("--chip"),
                                   QStringLiteral("stm32l0-cat3-64k"), QStringLiteral("--yes")});
    EXPECT_EQ(errorCode(result), QStringLiteral("target.permanently_protected")) << result.out;
}

TEST_F(Stm32Cli, UnlockNeedsConfirmationThenUnlocks) {
    chip(QStringLiteral(R"("devId": 1047, "flashKib": 64, "rdp": 1)"));
    const QStringList args{QStringLiteral("--json"), QStringLiteral("stm32"),
                           QStringLiteral("unlock"), QStringLiteral("--chip"),
                           QStringLiteral("stm32l0-cat3-64k")};
    EXPECT_EQ(errorCode(hwio(args)), QStringLiteral("usage.confirmation_required"));
    const RunResult result = hwio(QStringList(args) << QStringLiteral("--yes"));
    ASSERT_EQ(result.exitCode, 0) << result.out << result.err;
    const QJsonObject final = finalData(result);
    EXPECT_EQ(final.value(QStringLiteral("operation")).toString(), QStringLiteral("unlock"));
    EXPECT_EQ(final.value(QStringLiteral("rdp")).toInt(), 0);
}

TEST_F(Stm32Cli, EraseAndReset) {
    EXPECT_EQ(hwio({QStringLiteral("stm32"), QStringLiteral("erase"), QStringLiteral("--chip"),
                    QStringLiteral("stm32l0-cat3-64k"), QStringLiteral("--yes")})
                  .exitCode,
              0);
    EXPECT_EQ(hwio({QStringLiteral("stm32"), QStringLiteral("reset"), QStringLiteral("--chip"),
                    QStringLiteral("stm32l0-cat3-64k"), QStringLiteral("--halt")})
                  .exitCode,
              0);
}

} // namespace
