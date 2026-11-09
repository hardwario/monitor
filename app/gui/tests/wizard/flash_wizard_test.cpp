// FlashWizard (plan/features/0010-m4-flash-wizard.md with the 0035 amendments) against the fake
// J-Link library: the full flow from an empty tab (probe picker → cable check → file → confirm →
// flash → Open console), a tab with a probe skipping the probe and cable steps, the cable step's
// wrong connector and protected MCU, the start points of the FlashFirmware errors (their plan 0035
// § D coverage is in wizard_coverage_test.cpp), an LTE image handed over to the LTE update,
// UICR only with UICR records, cancel, failure with Try again, and the probe-only session.

#include "gui_test_support.h"
#include "hex_builder.h"
#include "hwio/core/error.h"
#include "hwio/device/device_session.h"
#include "hwio/device/session_events.h"
#include "hwio/diag/check.h"
#include "hwio/firmware/firmware_image.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/firmware_source_view_model.h"
#include "viewmodels/flash_view_model.h"
#include "viewmodels/probe_picker_view_model.h"
#include "wizard/check_list_model.h"
#include "wizard/flash_wizard.h"
#include "wizard/troubleshooting_wizard.h"
#include "wizard/wizard.h"
#include "wizard/wizard_view_model.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hwio::ErrorCode;
using hwio::firmware::FirmwareImage;
using hwio::firmware::Segment;
using hwio::gui::AppViewModel;
using hwio::gui::DeviceTabViewModel;
using hwio::gui::FirmwareSourceViewModel;
using hwio::gui::FlashWizard;
using hwio::gui::StepKind;
using hwio::gui::test::waitFor;

// One probe with RTT; `chip` is the scenario's chip object (default: an nRF52840).
std::string probeScenario(const std::string& chip = "{}") {
    return std::string(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
        "product": "Test Probe", "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}]},
        "chip": )") +
           chip + "}]}";
}

class FlashWizardTest : public hwio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        qputenv("HWIO_NRFUTIL", (root() / "no-nrfutil" / "nrfutil").string().c_str());
    }
    void TearDown() override {
        if (m_app && m_app->wizard()->isOpen()) {
            m_app->wizard()->cancel(true);
            (void)waitFor([this] { return !m_app->wizard()->isOpen(); });
        }
        m_app.reset();
        qunsetenv("HWIO_NRFUTIL");
        GuiTest::TearDown();
    }

    // `configure`: the tab has the probe (else it is a new tab on the Choose device page).
    void start(const std::string& scenario, bool configure = true) {
        m_scenario = scenario;
        useScenario(m_scenario.c_str());
        m_app = makeApp();
        if (configure) {
            ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001,
                                        QStringLiteral("Test Probe")));
        }
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] DeviceTabViewModel& tab() { return *m_app->currentTabViewModel(); }

    FlashWizard* open(const hwio::gui::ErrorInfo& context = {}) {
        if (context.valid()) {
            EXPECT_TRUE(app().launcher()->open(FlashWizard::wizardId, context, 0));
        } else {
            tab().openWizard(FlashWizard::wizardId);
        }
        return qobject_cast<FlashWizard*>(app().wizard()->wizard());
    }

    static bool reach(const FlashWizard& wizard, FlashWizard::Step step,
                      std::chrono::milliseconds timeout = std::chrono::seconds(30)) {
        return waitFor([&] { return wizard.step() == step; }, timeout);
    }

    [[nodiscard]] fs::path writeFile(const std::string& name, const QByteArray& bytes) const {
        const fs::path path = root() / name;
        QFile file(QString::fromStdString(path.string()));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
        return path;
    }

    [[nodiscard]] static FirmwareImage
    image(const std::vector<std::pair<std::uint32_t, std::size_t>>& parts = {{0x1000, 4096}}) {
        std::vector<Segment> segments;
        segments.reserve(parts.size());
        for (const auto& [address, size] : parts) {
            segments.push_back(
                Segment{.address = address, .data = std::vector<std::byte>(size, std::byte{0x5A})});
        }
        return *FirmwareImage::fromSegments(std::move(segments));
    }

    [[nodiscard]] fs::path hexFile(const std::string& name = "app.hex",
                                   const FirmwareImage& content = image()) const {
        return writeFile(name, QByteArray::fromStdString(hwio::firmware::test::toHex(content)));
    }

    static bool load(FlashWizard& wizard, const fs::path& path) {
        wizard.source()->loadFile(QUrl::fromLocalFile(QString::fromStdString(path.string())));
        return waitFor(
            [&] { return wizard.source()->sourceState() != FirmwareSourceViewModel::Loading; });
    }

    // From a tab with a probe to the Confirm step with `path` loaded.
    FlashWizard* toConfirm(const fs::path& path) {
        FlashWizard* wizard = open();
        EXPECT_NE(wizard, nullptr);
        if (wizard == nullptr || !reach(*wizard, FlashWizard::Firmware)) {
            return nullptr;
        }
        EXPECT_TRUE(load(*wizard, path));
        EXPECT_TRUE(wizard->canPrimary());
        wizard->primary();
        EXPECT_EQ(wizard->step(), FlashWizard::Confirm);
        return wizard;
    }

    static bool finished(const FlashWizard& wizard) {
        return waitFor([&] { return wizard.step() == FlashWizard::Done; },
                       std::chrono::seconds(60));
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    std::string m_scenario;
};

TEST_F(FlashWizardTest, FullFlowFromAnEmptyTab) {
    start(probeScenario(), false);
    ASSERT_TRUE(tab().choosing());
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    EXPECT_EQ(app().wizard()->wizardId(), QStringLiteral("flash-firmware"));
    EXPECT_EQ(app().wizard()->title(), QStringLiteral("Flash application firmware"));
    // Probe: the probe picker in wizard mode; the one free probe is preselected.
    EXPECT_EQ(wizard->step(), FlashWizard::Probe);
    EXPECT_EQ(wizard->current().kind, StepKind::Embedded);
    EXPECT_EQ(wizard->current().embedded, QStringLiteral("probe-picker"));
    ASSERT_NE(wizard->picker(), nullptr);
    EXPECT_TRUE(wizard->picker()->wizardMode());
    EXPECT_EQ(app().wizard()->embedded(), wizard->picker());
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    EXPECT_FALSE(wizard->canBack());
    wizard->primary();
    // Cable: the tab has the probe now, Diagnose runs on a probe-only session.
    EXPECT_FALSE(tab().choosing());
    EXPECT_EQ(tab().probeSerial(), std::optional<std::uint32_t>(801000001));
    EXPECT_EQ(wizard->step(), FlashWizard::Cable);
    EXPECT_EQ(wizard->current().kind, StepKind::Checks);
    EXPECT_EQ(wizard->current().illustration, QStringLiteral("chester-app-cable"));
    EXPECT_EQ(wizard->checks()->count(), 4); // probe … identify, no RTT rows
    ASSERT_TRUE(waitFor([&] { return wizard->cablePassed(); }));
    EXPECT_EQ(tab().session()->connection().state, hwio::device::ConnectionState::ProbeOpen);
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("identify")), hwio::diag::CheckStatus::Ok);
    EXPECT_TRUE(wizard->canBack());
    wizard->primary();
    // Firmware: the source card; Next needs a ready file.
    ASSERT_EQ(wizard->step(), FlashWizard::Firmware);
    EXPECT_EQ(wizard->current().embedded, QStringLiteral("firmware-source"));
    EXPECT_EQ(app().wizard()->embedded(), wizard);
    EXPECT_EQ(wizard->source()->kind(), FirmwareSourceViewModel::AppFirmware);
    EXPECT_FALSE(wizard->canPrimary());
    EXPECT_TRUE(wizard->explanation().isEmpty());
    ASSERT_TRUE(load(*wizard, hexFile("chester-clime-v3.2.1.hex")));
    ASSERT_TRUE(wizard->canPrimary());
    wizard->primary();
    // Confirm.
    ASSERT_EQ(wizard->step(), FlashWizard::Confirm);
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Flash"));
    EXPECT_EQ(wizard->summaryText(),
              QStringLiteral("chester-clime-v3.2.1.hex (Intel HEX, 4 kB) → Application MCU "
                             "(nRF52840) of CHESTER via J-Link 801000001."));
    EXPECT_EQ(wizard->summaryNote(),
              QStringLiteral("The current application firmware is replaced. Configuration and "
                             "the Product Information Block are kept."));
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    // Flash: the tab is busy; Cancel asks.
    EXPECT_EQ(wizard->step(), FlashWizard::Flash);
    EXPECT_EQ(tab().busyPage(), QStringLiteral("flash-firmware"));
    EXPECT_EQ(app().wizard()->busyCancelText(),
              QStringLiteral("Flashing is in progress. Cancel it and close the wizard?"));
    EXPECT_FALSE(app().wizard()->canBack());
    EXPECT_FALSE(wizard->phases().isEmpty());
    ASSERT_TRUE(finished(*wizard));
    // Done.
    ASSERT_EQ(wizard->stage(), FlashWizard::Succeeded) << wizard->error().detail().toStdString();
    EXPECT_EQ(wizard->current().kind, StepKind::Done);
    EXPECT_TRUE(
        wizard->current().heading.startsWith(QStringLiteral("Firmware flashed and verified in ")));
    EXPECT_TRUE(wizard->current().heading.endsWith(QStringLiteral("Chip: nRF52840")));
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Open console"));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    // The source is in the recent list (flash/recent, as on the page).
    EXPECT_GE(wizard->source()->recent()->rowCount(), 1);
    // [Open console]: the wizard closes and the tab connects with RTT.
    QSignalSpy handOff(app().wizard(), &hwio::gui::WizardViewModel::handOff);
    app().wizard()->primary();
    ASSERT_EQ(handOff.count(), 1);
    EXPECT_EQ(handOff.at(0).at(0).toString(), QStringLiteral("open-console"));
    EXPECT_FALSE(app().wizard()->isOpen());
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    EXPECT_EQ(tab().currentPage(), QStringLiteral("console"));
}

TEST_F(FlashWizardTest, ATabWithAProbeSkipsTheProbeAndCableSteps) {
    start(probeScenario());
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    EXPECT_EQ(wizard->step(), FlashWizard::Cable);
    // The first Diagnose passes: on to the firmware.
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    // Back shows the passed checks (no Probe step: the tab had its probe).
    wizard->back();
    ASSERT_TRUE(waitFor([&] { return wizard->cablePassed(); }));
    EXPECT_EQ(wizard->step(), FlashWizard::Cable);
    EXPECT_FALSE(wizard->canBack());
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Next"));
    // Closing without a hand-off releases the probe-only session the wizard opened.
    app().wizard()->cancel(false);
    EXPECT_FALSE(app().wizard()->isOpen());
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
}

TEST_F(FlashWizardTest, AConnectedTabKeepsItsSession) {
    start(probeScenario());
    tab().connectDevice();
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    hwio::device::DeviceSession* session = tab().session();
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    app().wizard()->cancel(false);
    EXPECT_EQ(tab().session(), session);
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Connected);
}

TEST_F(FlashWizardTest, TheCableOnTheLteConnectorOffersTheLteUpdate) {
    start(probeScenario(R"({"family": "nrf91"})"));
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return !wizard->cableRunning(); }));
    EXPECT_EQ(wizard->step(), FlashWizard::Cable);
    EXPECT_TRUE(wizard->wrongConnector());
    EXPECT_FALSE(wizard->cablePassed());
    const hwio::gui::WizardStep step = wizard->current();
    EXPECT_EQ(step.primaryText, QStringLiteral("Check again"));
    EXPECT_TRUE(step.body.contains(QStringLiteral("APP connector")));
    ASSERT_EQ(step.actions.size(), 1);
    EXPECT_EQ(step.actions.at(0).toMap().value(QStringLiteral("id")).toString(),
              QStringLiteral("lte-update"));
    // [Update LTE firmware…]: this wizard closes and the LTE update opens on the same tab.
    wizard->action(QStringLiteral("lte-update"));
    ASSERT_TRUE(waitFor([this] {
        return app().wizard()->isOpen() &&
               app().wizard()->wizardId() == QStringLiteral("lte-update");
    }));
    EXPECT_EQ(app().wizard()->tab(), &tab());
}

TEST_F(FlashWizardTest, AProtectedMcuIsInformationWithTheFlashPageHandOff) {
    start(probeScenario(R"({"protected": true})"));
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return !wizard->cableRunning(); }));
    EXPECT_TRUE(wizard->targetProtected());
    EXPECT_FALSE(wizard->canPrimary());
    const hwio::gui::WizardStep step = wizard->current();
    EXPECT_EQ(step.body,
              QStringLiteral("The MCU is protected. Flashing removes the protection only "
                             "through Recover, which erases everything."));
    ASSERT_EQ(step.actions.size(), 1);
    QSignalSpy handOff(app().wizard(), &hwio::gui::WizardViewModel::handOff);
    wizard->action(QStringLiteral("open-flash-page"));
    ASSERT_EQ(handOff.count(), 1);
    EXPECT_EQ(handOff.at(0).at(0).toString(), QStringLiteral("open-flash-page:recover:app"));
    EXPECT_FALSE(app().wizard()->isOpen());
    auto* flash =
        qobject_cast<hwio::gui::FlashViewModel*>(tab().pageViewModel(QStringLiteral("flash")));
    ASSERT_NE(flash, nullptr);
    EXPECT_TRUE(flash->recoverAvailable());
}

TEST_F(FlashWizardTest, StartPointsExplainTheError) {
    start(probeScenario());
    hwio::Error parse = hwio::makeError(ErrorCode::FirmwareHexParseError, "app.hex: line 7: bad");
    FlashWizard* wizard = open(hwio::gui::ErrorInfo(parse));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    EXPECT_EQ(wizard->explanation(),
              QStringLiteral("The file is not a valid HEX file (line 7). Use the .hex file "
                             "produced by the build, or download it from the catalog again."));
    // A file that loads ends the explanation.
    ASSERT_TRUE(load(*wizard, hexFile()));
    EXPECT_TRUE(wizard->explanation().isEmpty());
    app().wizard()->cancel(false);

    wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::FirmwareInvalidImage)));
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    EXPECT_EQ(wizard->explanation(),
              QStringLiteral("This file is not CHESTER firmware. Application and LTE firmware are "
                             ".hex files; LTE bundles and modem firmware are .zip files."));
    app().wizard()->cancel(false);

    wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::FirmwareNotFoundInCatalog)));
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    EXPECT_EQ(wizard->explanation(),
              QStringLiteral("No firmware has this ID. Copy the ID from the firmware catalog "
                             "again."));
    EXPECT_TRUE(wizard->explanationOffersCatalog());
    EXPECT_FALSE(wizard->explanationOffersLteUpdate());
    app().wizard()->cancel(false);

    // firmware.out_of_range without an LTE image on the Flash page: the 0010 text.
    wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::FirmwareOutOfRange)));
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    EXPECT_EQ(wizard->explanation(),
              QStringLiteral("This firmware is for another MCU. Check whether it is application "
                             "or LTE firmware."));
    EXPECT_FALSE(wizard->explanationOffersLteUpdate());
}

TEST_F(FlashWizardTest, OutOfRangeWithAnLteImageOnTheFlashPageOffersTheLteUpdate) {
    start(probeScenario());
    // An LTE image (UICR of the nRF91) on the Flash page.
    auto* flash =
        qobject_cast<hwio::gui::FlashViewModel*>(tab().pageViewModel(QStringLiteral("flash")));
    ASSERT_NE(flash, nullptr);
    flash->source()->loadFile(QUrl::fromLocalFile(QString::fromStdString(
        hexFile("lte.hex", image({{0x1000, 256}, {0x00FF8000, 16}})).string())));
    ASSERT_TRUE(waitFor(
        [&] { return flash->source()->sourceState() != FirmwareSourceViewModel::Loading; }));
    FlashWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::FirmwareOutOfRange)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    EXPECT_TRUE(wizard->explanation().startsWith(QStringLiteral("This is LTE firmware.")));
    EXPECT_TRUE(wizard->explanationOffersLteUpdate());
}

TEST_F(FlashWizardTest, AnImageThatDoesNotFitTheApplicationMcuCannotBeFlashed) {
    start(probeScenario());
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    ASSERT_TRUE(load(*wizard, hexFile("lte.hex", image({{0x1000, 256}, {0x00FF8000, 16}}))));
    EXPECT_FALSE(wizard->canPrimary());
    EXPECT_TRUE(wizard->explanation().startsWith(QStringLiteral("This is LTE firmware.")));
    EXPECT_TRUE(wizard->explanationOffersLteUpdate());
    ASSERT_TRUE(load(*wizard, hexFile("ram.hex", image({{0x20000000, 16}}))));
    EXPECT_FALSE(wizard->canPrimary());
    EXPECT_EQ(wizard->explanation(),
              QStringLiteral("This firmware is for another MCU. Check whether it is application "
                             "or LTE firmware."));
    EXPECT_FALSE(wizard->explanationOffersLteUpdate());
    // The link below the source card hands over to the LTE update.
    EXPECT_TRUE(wizard->lteUpdateAvailable());
    wizard->updateLteFirmware();
    ASSERT_TRUE(waitFor([this] {
        return app().wizard()->isOpen() &&
               app().wizard()->wizardId() == QStringLiteral("lte-update");
    }));
}

TEST_F(FlashWizardTest, UicrRecordsNeedTheOption) {
    start(probeScenario());
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(reach(*wizard, FlashWizard::Firmware));
    ASSERT_TRUE(load(*wizard, hexFile()));
    EXPECT_FALSE(wizard->source()->hasUicr());
    EXPECT_TRUE(wizard->canPrimary());
    ASSERT_TRUE(load(*wizard, hexFile("uicr.hex", image({{0x1000, 256}, {0x10001000, 16}}))));
    EXPECT_TRUE(wizard->source()->hasUicr());
    EXPECT_FALSE(wizard->canPrimary());
    wizard->source()->setAllowUicr(true);
    EXPECT_TRUE(wizard->canPrimary());
    wizard->primary();
    EXPECT_EQ(wizard->summaryNote(),
              QStringLiteral("The current application firmware is replaced. The UICR "
                             "configuration is also written; the Product Information Block is "
                             "kept."));
}

TEST_F(FlashWizardTest, CancelDuringTheFlash) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    FlashWizard* wizard = toConfirm(hexFile("big.hex", image({{0x1000, std::size_t{64} * 1024}})));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_EQ(wizard->step(), FlashWizard::Flash);
    ASSERT_TRUE(waitFor([&] { return wizard->progressText().contains(QStringLiteral("%")); },
                        std::chrono::seconds(30)));
    // The footer Cancel asks; Keep going changes nothing.
    QSignalSpy asked(app().wizard(), &hwio::gui::WizardViewModel::cancelConfirmationNeeded);
    app().wizard()->cancel(false);
    EXPECT_EQ(asked.count(), 1);
    EXPECT_TRUE(app().wizard()->isOpen());
    wizard->cancelFlash();
    EXPECT_TRUE(wizard->cancelling());
    EXPECT_EQ(wizard->progressText(), QStringLiteral("Cancelling…"));
    ASSERT_TRUE(finished(*wizard));
    EXPECT_EQ(wizard->stage(), FlashWizard::Cancelled);
    EXPECT_EQ(wizard->current().kind, StepKind::Info);
    EXPECT_EQ(wizard->current().body,
              QStringLiteral("The device may not start until you flash it again."));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    // [Flash again]: back to Confirm.
    wizard->primary();
    EXPECT_EQ(wizard->step(), FlashWizard::Confirm);
}

TEST_F(FlashWizardTest, CancelAndCloseWhileFlashing) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    FlashWizard* wizard = toConfirm(hexFile("big.hex", image({{0x1000, std::size_t{64} * 1024}})));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_EQ(wizard->step(), FlashWizard::Flash);
    app().wizard()->cancel(true); // [Cancel and close]: closes when the operation has ended
    EXPECT_TRUE(app().wizard()->closing());
    ASSERT_TRUE(waitFor([this] { return !app().wizard()->isOpen(); }, std::chrono::seconds(60)));
    ASSERT_TRUE(waitFor([this] { return !tab().isBusy(); }));
}

TEST_F(FlashWizardTest, AFailureOffersTryAgain) {
    start(probeScenario(R"({"writeFault": true})"));
    FlashWizard* wizard = toConfirm(hexFile());
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_TRUE(finished(*wizard));
    ASSERT_EQ(wizard->stage(), FlashWizard::Failed);
    EXPECT_EQ(wizard->error().code(), QStringLiteral("target.program_failed"));
    EXPECT_EQ(wizard->resultText(),
              QStringLiteral(
                  "Flashing was interrupted. The firmware may be partly written: flash it again."));
    const hwio::gui::WizardStep step = wizard->current();
    EXPECT_EQ(step.kind, StepKind::Embedded);
    EXPECT_EQ(step.embedded, QStringLiteral("phase-progress"));
    EXPECT_EQ(step.primaryText, QStringLiteral("Try again"));
    // target.program_failed names Troubleshooting (plan 0009): [Help me fix this].
    ASSERT_EQ(step.actions.size(), 1);
    EXPECT_EQ(step.actions.at(0).toMap().value(QStringLiteral("id")).toString(),
              QStringLiteral("help-me-fix-this"));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    wizard->primary();
    EXPECT_EQ(wizard->step(), FlashWizard::Confirm);
}

// ADR-0297: [Help me fix this] hands the error itself off, so troubleshooting opens at the topic.
TEST_F(FlashWizardTest, HelpMeFixThisOpensTheMatchingTopic) {
    start(probeScenario(R"({"writeFault": true})"));
    FlashWizard* wizard = toConfirm(hexFile());
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_TRUE(finished(*wizard));
    ASSERT_EQ(wizard->stage(), FlashWizard::Failed);
    QSignalSpy handOffError(app().wizard(), &hwio::gui::WizardViewModel::handOffError);
    wizard->action(QStringLiteral("help-me-fix-this"));
    ASSERT_EQ(handOffError.count(), 1);
    ASSERT_TRUE(waitFor([this] {
        return app().wizard()->isOpen() &&
               app().wizard()->wizardId() == QStringLiteral("troubleshooting");
    }));
    const auto* troubleshooting =
        qobject_cast<hwio::gui::TroubleshootingWizard*>(app().wizard()->wizard());
    ASSERT_NE(troubleshooting, nullptr);
    ASSERT_NE(troubleshooting->topic(), nullptr);
    EXPECT_EQ(troubleshooting->topic()->id, "program-failed");
    EXPECT_EQ(troubleshooting->shownError().code(), QStringLiteral("target.program_failed"));
    EXPECT_FALSE(troubleshooting->shownError().detail().isEmpty()); // the detail is kept
    // ADR-0300: troubleshooting took over the wizards' session; closing it releases it.
    app().wizard()->cancel(true);
    ASSERT_TRUE(waitFor([this] { return !app().wizard()->isOpen(); }));
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));
}

// ADR-0300: the failure's [Open Settings] hands off to a page: the session is released.
TEST_F(FlashWizardTest, AHandOffToSettingsReleasesTheSession) {
    start(probeScenario(R"({"writeFault": true})"));
    FlashWizard* wizard = toConfirm(hexFile());
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_TRUE(finished(*wizard));
    ASSERT_EQ(wizard->stage(), FlashWizard::Failed);
    ASSERT_TRUE(tab().wizardOwnsSession());
    DeviceTabViewModel* flashed = &tab(); // the Settings page becomes current
    wizard->triggerErrorAction(hwio::gui::error_action::OpenSettings);
    EXPECT_FALSE(app().wizard()->isOpen());
    ASSERT_TRUE(waitFor([flashed] { return flashed->session() == nullptr; }));
}

// Review 0009 F1: closing during the Cable step's Diagnose cancels it and leaves no session.
TEST_F(FlashWizardTest, ClosingDuringTheCableCheckCancelsIt) {
    std::string slow = probeScenario();
    slow.insert(slow.find(R"("chip": )"), R"("connectDelayMs": 1500, )");
    start(slow);
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_EQ(wizard->step(), FlashWizard::Cable);
    ASSERT_TRUE(wizard->cableRunning());
    EXPECT_FALSE(app().wizard()->busyCancelText().isEmpty()); // Cancel asks first
    app().wizard()->cancel(true);
    ASSERT_TRUE(waitFor([this] { return !app().wizard()->isOpen(); }, std::chrono::seconds(20)));
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }, std::chrono::seconds(20)));
    EXPECT_TRUE(tab().busyPage().isEmpty());
}

TEST_F(FlashWizardTest, FlashAnotherGoesBackToTheFirmware) {
    start(probeScenario());
    FlashWizard* wizard = toConfirm(hexFile());
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_TRUE(finished(*wizard));
    ASSERT_EQ(wizard->stage(), FlashWizard::Succeeded) << wizard->error().detail().toStdString();
    wizard->action(QStringLiteral("flash-another"));
    EXPECT_EQ(wizard->step(), FlashWizard::Firmware);
    EXPECT_EQ(wizard->source()->sourceState(), FirmwareSourceViewModel::None);
    EXPECT_FALSE(wizard->canPrimary());
}

TEST_F(FlashWizardTest, AnotherBusyPageDisablesFlash) {
    start(probeScenario());
    FlashWizard* wizard = toConfirm(hexFile());
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    tab().setBusy(QStringLiteral("other"), QStringLiteral("Working…"), QStringLiteral("?"), [] {});
    EXPECT_FALSE(wizard->canPrimary());
    EXPECT_EQ(wizard->cannotFlashReason(), QStringLiteral("Another operation is running"));
    tab().clearBusy(QStringLiteral("other"));
    EXPECT_TRUE(wizard->canPrimary());
}

TEST_F(FlashWizardTest, BackFromTheCableStepChoosesAnotherProbe) {
    start(probeScenario(), false);
    FlashWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_EQ(wizard->step(), FlashWizard::Cable);
    ASSERT_TRUE(waitFor([&] { return !wizard->cableRunning(); }));
    wizard->back();
    EXPECT_EQ(wizard->step(), FlashWizard::Probe);
    EXPECT_TRUE(tab().choosing());
    EXPECT_EQ(tab().session(), nullptr);
    // The probe is free again (not "in use" by this tab).
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
}

TEST_F(FlashWizardTest, ClosingTheTabMidFlashCancelsAndCloses) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    FlashWizard* wizard = toConfirm(hexFile("big.hex", image({{0x1000, std::size_t{64} * 1024}})));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    ASSERT_EQ(wizard->step(), FlashWizard::Flash);
    const QPointer<DeviceTabViewModel> closing(&tab());
    // [Cancel and close] of the busy confirmation: the flash is cancelled; the wizard closes
    // before the tab shuts its session down (WizardViewModel::tabClosing).
    app().closeTabObject(closing.data(), true);
    EXPECT_TRUE(wizard->cancelling());
    ASSERT_TRUE(waitFor([&closing] { return closing.isNull(); }, std::chrono::seconds(30)));
    EXPECT_FALSE(app().wizard()->isOpen());
}

} // namespace
