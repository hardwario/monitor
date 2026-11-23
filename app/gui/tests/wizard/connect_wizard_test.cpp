// ConnectWizard (plan/features/0009-m4-connect-and-troubleshooting.md with the 0035 amendments)
// against the fake J-Link library: the full flow from an empty tab (probe picker → cable → power
// → check → connect → Open console) with the probe-only session promoted (the same session, the
// probe lock kept), a connected tab as a pure check, the start steps of the ConnectDevice errors,
// each Check failure leading back to its step (no power, wrong connector with the LTE hand-off,
// protected MCU, no console), a probe that cannot be opened handing its error off to
// troubleshooting (ADR-0297), cancel during Diagnose and the tab's busy state.

#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/device/device_session.h"
#include "hwio/device/probe_lock.h"
#include "hwio/device/session_events.h"
#include "hwio/diag/check.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/probe_picker_view_model.h"
#include "wizard/check_list_model.h"
#include "wizard/connect_wizard.h"
#include "wizard/troubleshooting_wizard.h"
#include "wizard/wizard.h"
#include "wizard/wizard_launcher.h"
#include "wizard/wizard_view_model.h"
#include "workers/diag_worker.h"

#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace {

using hwio::ErrorCode;
using hwio::diag::CheckStatus;
using hwio::gui::AppViewModel;
using hwio::gui::ConnectWizard;
using hwio::gui::DeviceTabViewModel;
using hwio::gui::StepKind;
using hwio::gui::test::tabWizard;
using hwio::gui::test::waitFor;
using Outcome = ConnectWizard::Outcome;

// One probe; `probe` adds keys of the probe object, `chip` is the chip object, `rtt` the RTT
// object (default: a console).
std::string scenario(const std::string& probe = {}, const std::string& chip = "{}",
                     const std::string& rtt =
                         R"({"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}]})") {
    return std::string(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
        "product": "Test Probe", "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
        "rtt": )") +
           rtt + "," + probe + R"("chip": )" + chip + "}]}";
}

QStringList actionIds(const hwio::gui::WizardStep& step) {
    QStringList ids;
    for (const QVariant& action : step.actions) {
        ids.append(action.toMap().value(QStringLiteral("id")).toString());
    }
    return ids;
}

class ConnectWizardTest : public hwio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        qputenv("HWIO_NRFUTIL", (root() / "no-nrfutil" / "nrfutil").string().c_str());
    }
    void TearDown() override {
        if (m_app) {
            hwio::gui::test::closeAllWizards(*m_app);
        }
        m_app.reset();
        qunsetenv("HWIO_NRFUTIL");
        GuiTest::TearDown();
    }

    void start(const std::string& json, bool configure = true) {
        m_scenario = json;
        useScenario(m_scenario.c_str());
        m_app = makeApp();
        if (configure) {
            ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001,
                                        QStringLiteral("Test Probe")));
        }
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] DeviceTabViewModel& tab() { return *m_app->currentTabViewModel(); }

    ConnectWizard* open(const hwio::gui::ErrorInfo& context = {}) {
        if (context.valid()) {
            EXPECT_TRUE(app().launcher()->open(ConnectWizard::wizardId, context, 0));
        } else {
            tab().openWizard(ConnectWizard::wizardId);
        }
        return qobject_cast<ConnectWizard*>(tabWizard(app()).wizard());
    }

    static bool checked(const ConnectWizard& wizard,
                        std::chrono::milliseconds timeout = std::chrono::seconds(30)) {
        return waitFor([&] { return wizard.outcome() != Outcome::Running; }, timeout);
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    std::string m_scenario;
};

TEST_F(ConnectWizardTest, FullFlowFromAnEmptyTab) {
    start(scenario(), false);
    ConnectWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    EXPECT_EQ(tabWizard(app()).title(), QStringLiteral("Connect a device"));
    // The step list: Probe · Cable · Power · Check · Done (no Device step, ADR-0249).
    QStringList titles;
    for (const hwio::gui::WizardStep& step : wizard->plannedSteps()) {
        titles.append(step.title);
    }
    EXPECT_EQ(titles, (QStringList{QStringLiteral("Probe"), QStringLiteral("Cable"),
                                   QStringLiteral("Power"), QStringLiteral("Check"),
                                   QStringLiteral("Done")}));
    // Probe: the picker in wizard mode, the free probe preselected.
    EXPECT_EQ(wizard->step(), ConnectWizard::Probe);
    EXPECT_EQ(wizard->current().embedded, QStringLiteral("probe-picker"));
    ASSERT_NE(wizard->picker(), nullptr);
    EXPECT_TRUE(wizard->picker()->wizardMode());
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    EXPECT_FALSE(wizard->canBack());
    wizard->primary();
    // Cable: the console target's connection guide.
    EXPECT_EQ(tab().probeSerial(), std::optional<std::uint32_t>(801000001));
    ASSERT_EQ(wizard->step(), ConnectWizard::Cable);
    EXPECT_EQ(wizard->current().kind, StepKind::Info);
    EXPECT_EQ(wizard->current().illustration, QStringLiteral("chester-app-cable"));
    EXPECT_EQ(wizard->current().body,
              QStringLiteral("Connect the J-Link flat cable to the programming connector of the "
                             "application MCU."));
    EXPECT_TRUE(wizard->canBack());
    wizard->primary();
    // Power: the profile's power hint.
    ASSERT_EQ(wizard->step(), ConnectWizard::Power);
    EXPECT_EQ(wizard->current().illustration, QStringLiteral("chester-power"));
    EXPECT_EQ(wizard->current().body,
              QStringLiteral("Power CHESTER with batteries or the USB-C power input."));
    wizard->primary();
    // Check: the six Diagnose steps on a probe-only session; the tab is busy meanwhile.
    ASSERT_EQ(wizard->step(), ConnectWizard::Check);
    EXPECT_EQ(wizard->checks()->count(), 6);
    EXPECT_EQ(tab().busyPage(), QStringLiteral("connect-device"));
    EXPECT_EQ(tabWizard(app()).busyCancelText(),
              QStringLiteral("The connection is being checked. Cancel it and close the wizard?"));
    ASSERT_TRUE(checked(*wizard));
    EXPECT_EQ(wizard->outcome(), Outcome::Passed);
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Connect"));
    hwio::device::DeviceSession* session = tab().session();
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->connection().state, hwio::device::ConnectionState::ProbeOpen);
    // [Connect]: the probe-only session is promoted (the same session: probe and lock kept).
    wizard->primary();
    EXPECT_EQ(wizard->step(), ConnectWizard::Done);
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    EXPECT_EQ(tab().session(), session);
    // The probe lock stayed held across the promotion: nobody else can take the probe.
    EXPECT_FALSE(hwio::device::ProbeLock::acquire(801000001, root() / "locks").has_value());
    EXPECT_EQ(wizard->current().kind, StepKind::Done);
    EXPECT_EQ(wizard->current().heading,
              QStringLiteral("Connected to CHESTER · Application MCU (nRF52840) · 801000001."));
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Open console"));
    EXPECT_FALSE(wizard->canBack());
    QSignalSpy handOff(&tabWizard(app()), &hwio::gui::WizardViewModel::handOff);
    tabWizard(app()).primary();
    ASSERT_EQ(handOff.count(), 1);
    EXPECT_EQ(handOff.at(0).at(0).toString(), QStringLiteral("open-console"));
    EXPECT_FALSE(tabWizard(app()).isOpen());
    EXPECT_EQ(tab().currentPage(), QStringLiteral("console"));
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Connected);
}

TEST_F(ConnectWizardTest, AConnectedTabIsAPureCheck) {
    start(scenario());
    tab().connectDevice();
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    hwio::device::DeviceSession* session = tab().session();
    ConnectWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    EXPECT_EQ(wizard->step(), ConnectWizard::Check);
    EXPECT_FALSE(wizard->canBack());
    ASSERT_TRUE(checked(*wizard));
    EXPECT_EQ(wizard->outcome(), Outcome::Passed);
    wizard->primary();
    EXPECT_EQ(wizard->step(), ConnectWizard::Done);
    EXPECT_EQ(wizard->current().kind, StepKind::Done);
    EXPECT_EQ(tab().session(), session);
    // Close keeps the connected tab.
    tabWizard(app()).cancel(false);
    EXPECT_FALSE(tabWizard(app()).isOpen());
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Connected);
}

TEST_F(ConnectWizardTest, ErrorsOpenAtTheirStep) {
    EXPECT_EQ(ConnectWizard::startStepFor(ErrorCode::ProbeNotFound), ConnectWizard::Probe);
    EXPECT_EQ(ConnectWizard::startStepFor(ErrorCode::TargetNoPower), ConnectWizard::Power);
    EXPECT_EQ(ConnectWizard::startStepFor(ErrorCode::TargetConnectFailed), ConnectWizard::Cable);
    EXPECT_EQ(ConnectWizard::startStepFor(ErrorCode::TargetWrongFamily), ConnectWizard::Cable);
    EXPECT_EQ(ConnectWizard::startStepFor(ErrorCode::TargetNotConnected), ConnectWizard::Check);
    EXPECT_FALSE(ConnectWizard::startStepFor(ErrorCode::ProbeOpenFailed).has_value());

    start(scenario());
    ConnectWizard* wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNoPower)));
    ASSERT_NE(wizard, nullptr);
    EXPECT_EQ(wizard->step(), ConnectWizard::Power);
    EXPECT_TRUE(wizard->canBack()); // back to the cable
    tabWizard(app()).cancel(false);

    wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetConnectFailed)));
    EXPECT_EQ(wizard->step(), ConnectWizard::Cable);
    EXPECT_FALSE(wizard->canBack()); // the Probe step was not shown
    tabWizard(app()).cancel(false);

    wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    EXPECT_EQ(wizard->step(), ConnectWizard::Check);
    ASSERT_TRUE(checked(*wizard));
    tabWizard(app()).cancel(false);
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));

    // probe.not_found: the tab returns to its picker; its probe is preselected.
    wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::ProbeNotFound)));
    EXPECT_EQ(wizard->step(), ConnectWizard::Probe);
    EXPECT_TRUE(tab().choosing());
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    EXPECT_EQ(wizard->picker()->selectedSerial(), std::optional<std::uint32_t>(801000001));
}

TEST_F(ConnectWizardTest, NoPowerShowsThePowerStep) {
    start(scenario(R"("vtref": [[0, 0]], "connectMinMillivolts": 1000,)"));
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(checked(*wizard));
    EXPECT_EQ(wizard->outcome(), Outcome::NoPower);
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("power")), CheckStatus::Failed);
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Check again"));
    EXPECT_EQ(actionIds(wizard->current()), QStringList{QStringLiteral("show-power")});
    wizard->action(QStringLiteral("show-power"));
    EXPECT_EQ(wizard->step(), ConnectWizard::Power);
    // Next goes to the Check again, which runs again.
    wizard->primary();
    EXPECT_EQ(wizard->step(), ConnectWizard::Check);
    EXPECT_EQ(wizard->outcome(), Outcome::Running);
}

TEST_F(ConnectWizardTest, TheCableOnTheLteConnector) {
    start(scenario({}, R"({"family": "nrf91"})"));
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(checked(*wizard));
    EXPECT_EQ(wizard->outcome(), Outcome::WrongConnector);
    const hwio::gui::WizardStep step = wizard->current();
    EXPECT_EQ(step.body,
              QStringLiteral("The J-Link is on the LTE connector. Move it to the APP connector."));
    EXPECT_EQ(actionIds(step),
              (QStringList{QStringLiteral("show-cable"), QStringLiteral("lte-update")}));
    EXPECT_FALSE(wizard->canPrimary() && step.primaryText == QStringLiteral("Connect"));
    wizard->action(QStringLiteral("show-cable"));
    EXPECT_EQ(wizard->step(), ConnectWizard::Cable);
    // [Update LTE firmware…] hands off to the LTE update on the same tab.
    wizard->primary();
    wizard->primary();
    ASSERT_TRUE(checked(*wizard));
    wizard->action(QStringLiteral("lte-update"));
    ASSERT_TRUE(waitFor([this] {
        return tabWizard(app()).isOpen() &&
               tabWizard(app()).wizardId() == QStringLiteral("lte-update");
    }));
}

TEST_F(ConnectWizardTest, AProtectedMcuCanStillConnect) {
    start(scenario({}, R"({"protected": true})"));
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(checked(*wizard));
    EXPECT_EQ(wizard->outcome(), Outcome::Protected);
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Connect"));
    EXPECT_TRUE(wizard->canPrimary());
    EXPECT_TRUE(actionIds(wizard->current()).contains(QStringLiteral("open-flash-page")));
    QSignalSpy handOff(&tabWizard(app()), &hwio::gui::WizardViewModel::handOff);
    wizard->action(QStringLiteral("open-flash-page"));
    ASSERT_EQ(handOff.count(), 1);
    EXPECT_EQ(handOff.at(0).at(0).toString(), QStringLiteral("open-flash-page:recover:app"));
    EXPECT_FALSE(tabWizard(app()).isOpen());
}

TEST_F(ConnectWizardTest, NoConsoleOffersFlashingOrConnectAnyway) {
    start(scenario({}, "{}", R"({"foundAfterMs": 600000})"));
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(checked(*wizard, std::chrono::seconds(60)));
    EXPECT_EQ(wizard->outcome(), Outcome::NoConsole);
    EXPECT_EQ(
        wizard->current().body,
        QStringLiteral("The device has no console. It may need firmware with the CHESTER shell."));
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Connect anyway"));
    EXPECT_EQ(actionIds(wizard->current()),
              (QStringList{QStringLiteral("flash-firmware"), QStringLiteral("check-again")}));
    QSignalSpy handOff(&tabWizard(app()), &hwio::gui::WizardViewModel::handOff);
    wizard->action(QStringLiteral("flash-firmware"));
    ASSERT_EQ(handOff.count(), 1);
    EXPECT_EQ(handOff.at(0).at(0).toString(), QStringLiteral("open-wizard:flash-firmware"));
}

TEST_F(ConnectWizardTest, AProbeThatCannotBeOpenedHandsOffToTroubleshooting) {
    start(scenario());
    // Another program holds the probe (its lock file).
    auto lock = hwio::device::ProbeLock::acquire(801000001, root() / "locks");
    ASSERT_TRUE(lock) << (lock ? "" : lock.error().detail);
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(checked(*wizard));
    EXPECT_EQ(wizard->outcome(), Outcome::OtherFailed);
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("probe")), CheckStatus::Failed);
    ASSERT_TRUE(actionIds(wizard->current()).contains(QStringLiteral("help-me-fix-this")));
    wizard->action(QStringLiteral("help-me-fix-this"));
    ASSERT_TRUE(waitFor([this] {
        return tabWizard(app()).isOpen() &&
               tabWizard(app()).wizardId() == QStringLiteral("troubleshooting");
    }));
    const auto* troubleshooting =
        qobject_cast<hwio::gui::TroubleshootingWizard*>(tabWizard(app()).wizard());
    ASSERT_NE(troubleshooting, nullptr);
    ASSERT_NE(troubleshooting->topic(), nullptr);
    EXPECT_EQ(troubleshooting->topic()->id, "probe-busy");
}

TEST_F(ConnectWizardTest, CancelDuringTheCheckClosesTheProbeOnlySession) {
    start(scenario(R"("connectDelayMs": 1500,)"));
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(wizard->running());
    tabWizard(app()).cancel(false);
    EXPECT_TRUE(tabWizard(app()).isOpen()); // asks first
    tabWizard(app()).cancel(true);
    ASSERT_TRUE(waitFor([this] { return !tabWizard(app()).isOpen(); }, std::chrono::seconds(20)));
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }, std::chrono::seconds(20)));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
}

// Decision B (2026-11-09): opened from probe.not_found and cancelled without choosing another
// probe, the tab gets its probe back.
TEST_F(ConnectWizardTest, CancelRestoresTheTabsProbe) {
    start(scenario());
    ConnectWizard* wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::ProbeNotFound)));
    ASSERT_NE(wizard, nullptr);
    EXPECT_TRUE(tab().choosing());
    tabWizard(app()).cancel(false);
    EXPECT_FALSE(tabWizard(app()).isOpen());
    EXPECT_FALSE(tab().choosing());
    EXPECT_EQ(tab().probeSerial(), std::optional<std::uint32_t>(801000001));
}

// Review 0009 F3 (ADR-0300): a failed connection, Check again, close: no session is left.
TEST_F(ConnectWizardTest, CheckAgainAfterAFailedConnectionLeavesNoSession) {
    start(scenario());
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(checked(*wizard));
    wizard->primary(); // [Connect]
    ASSERT_EQ(wizard->step(), ConnectWizard::Done);
    tab().disconnectDevice(); // the connection fails
    ASSERT_TRUE(waitFor([&] { return wizard->connectFailed(); }));
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Check again"));
    wizard->primary();
    ASSERT_EQ(wizard->step(), ConnectWizard::Check);
    ASSERT_TRUE(checked(*wizard));
    EXPECT_TRUE(tab().wizardOwnsSession());
    tabWizard(app()).cancel(false);
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));
}

// Review 0009 F2 (ADR-0300): Connect → [Flash firmware…] → the flash wizard takes over the
// wizards' session; closing it releases the session.
TEST_F(ConnectWizardTest, AChainedWizardTakesOverTheSession) {
    start(scenario({}, "{}", R"({"foundAfterMs": 600000})"));
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(checked(*wizard, std::chrono::seconds(60)));
    ASSERT_EQ(wizard->outcome(), Outcome::NoConsole);
    const hwio::device::DeviceSession* session = tab().session();
    ASSERT_NE(session, nullptr);
    wizard->action(QStringLiteral("flash-firmware"));
    ASSERT_TRUE(waitFor([this] {
        return tabWizard(app()).isOpen() &&
               tabWizard(app()).wizardId() == QStringLiteral("flash-firmware");
    }));
    hwio::gui::test::spin(std::chrono::milliseconds(200));
    EXPECT_EQ(tab().session(), session); // kept for the flash wizard
    tabWizard(app()).cancel(true);
    ASSERT_TRUE(waitFor([this] { return !tabWizard(app()).isOpen(); }, std::chrono::seconds(20)));
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }, std::chrono::seconds(20)));
}

// ADR-0300: a hand-off to a global wizard (setup) does not keep the session.
TEST_F(ConnectWizardTest, AHandOffToTheSetupWizardReleasesTheSession) {
    start(scenario());
    ConnectWizard* wizard =
        open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    ASSERT_NE(wizard, nullptr);
    ASSERT_TRUE(checked(*wizard));
    ASSERT_NE(tab().session(), nullptr);
    emit wizard->handOffError(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::UsbAccessDenied)));
    emit wizard->finished(false);
    // Plan 0056: the setup wizard is global; the tab's wizard has closed.
    ASSERT_TRUE(waitFor([this] {
        return app().globalWizard()->isOpen() &&
               app().globalWizard()->wizardId() == QStringLiteral("first-run-setup");
    }));
    EXPECT_FALSE(tabWizard(app()).isOpen());
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));
}

// Review 0009 F6: a hand-off whose tab has closed opens nothing.
TEST_F(ConnectWizardTest, AHandOffToAClosedTabOpensNothing) {
    start(scenario());
    app().newTab();
    ASSERT_EQ(app().tabs()->count(), 2);
    DeviceTabViewModel* second = app().currentTabViewModel();
    emit hwio::gui::test::tabWizard(app()).handOffError(
        hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetRttNotFound)), second);
    app().closeTab(1, true);
    hwio::gui::test::spin(std::chrono::milliseconds(300));
    EXPECT_FALSE(tabWizard(app()).isOpen());
}

// Plan 0056 (D-11, ADR-0391): while the wizard is open, the tab's "Choose another device" is
// refused with a toast; after it closed (and while the LTE update guide is open) it works.
TEST_F(ConnectWizardTest, ChooseAnotherDeviceIsRefusedWhileTheWizardIsOpen) {
    start(scenario());
    ConnectWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_EQ(wizard->step(), ConnectWizard::Cable);
    tab().connection()->chooseAnotherProbe(); // the device bar
    EXPECT_FALSE(tab().choosing());
    EXPECT_TRUE(app().toasts().contains(DeviceTabViewModel::wizardOwnsDeviceText()));
    EXPECT_TRUE(tabWizard(app()).isOpen());
    tabWizard(app()).cancel(false);
    ASSERT_FALSE(tabWizard(app()).isOpen());
    tab().connection()->chooseAnotherProbe();
    EXPECT_TRUE(tab().choosing());
}

TEST_F(ConnectWizardTest, ChooseAnotherDeviceWorksWhileTheLteGuideIsOpen) {
    start(scenario());
    tab().openWizard(QStringLiteral("lte-update"));
    ASSERT_TRUE(tabWizard(app()).isOpen());
    ASSERT_EQ(tabWizard(app()).wizardId(), QStringLiteral("lte-update"));
    tab().connection()->chooseAnotherProbe();
    EXPECT_TRUE(tab().choosing());
}

// Plan 0056 (D-11): connect / disconnect in the device bar stay usable; the wizard follows them.
TEST_F(ConnectWizardTest, TheWizardFollowsConnectAndDisconnect) {
    start(scenario());
    ConnectWizard* wizard = open();
    ASSERT_NE(wizard, nullptr);
    ASSERT_EQ(wizard->step(), ConnectWizard::Cable);
    QSignalSpy changed(wizard, &hwio::gui::Wizard::changed);
    tab().connectDevice();
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    EXPECT_GT(changed.count(), 0);
    EXPECT_TRUE(tabWizard(app()).isOpen());
    changed.clear();
    tab().disconnectDevice();
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));
    EXPECT_GT(changed.count(), 0);
    EXPECT_TRUE(tabWizard(app()).isOpen());
    EXPECT_EQ(wizard->step(), ConnectWizard::Cable);
    // Check: the device bar's Connect after the check promotes the wizards' probe-only session
    // (ADR-0300); the wizard follows the new state.
    wizard->primary(); // Power
    wizard->primary(); // Check
    ASSERT_EQ(wizard->step(), ConnectWizard::Check);
    ASSERT_TRUE(checked(*wizard));
    ASSERT_NE(tab().session(), nullptr);
    changed.clear();
    tab().connectDevice();
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    EXPECT_GT(changed.count(), 0);
    EXPECT_TRUE(tabWizard(app()).isOpen());
    EXPECT_FALSE(tab().wizardOwnsSession());
}

// Plan 0056 (U-2, D-7, ADR-0393): on a remote-support tab (also after the session ended) the
// connect and troubleshooting wizards refuse with a toast; the tab stays remote.
TEST_F(ConnectWizardTest, LocalDeviceWizardsRefuseARemoteTab) {
#ifndef HWIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HWIO_TEST_OVERRIDES (an http loopback relay URL)";
#endif
    start(scenario(), false);
    ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("remote/relayUrl"),
                                                QStringLiteral("http://127.0.0.1:9")));
    ASSERT_TRUE(app().joinRemoteSession(QStringLiteral("12345"), false));
    ASSERT_TRUE(tab().isRemote());
    for (const QString& id :
         {QStringLiteral("connect-device"), QStringLiteral("troubleshooting")}) {
        EXPECT_FALSE(app().launcher()->canStart(id)) << id.toStdString();
        EXPECT_FALSE(app().launcher()->start(id)) << id.toStdString(); // Help menu
        EXPECT_FALSE(tabWizard(app()).isOpen());
    }
    // "Help me fix this" of an error panel on the tab.
    EXPECT_FALSE(app().launcher()->open(
        QStringLiteral("troubleshooting"),
        hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetConnectFailed)), 0));
    EXPECT_TRUE(app().toasts().contains(hwio::gui::WizardLauncher::localDeviceOnlyText()));
    EXPECT_FALSE(tabWizard(app()).isOpen());
    EXPECT_TRUE(tab().isRemote());
    // The setup wizard (global) is not affected.
    EXPECT_TRUE(app().launcher()->canStart(QStringLiteral("first-run-setup")));
}

// Plan 0056 (D-9, D-10): two tabs' wizards with their own pickers. Their probe checks run at
// once without replacing each other's requests; when tab 1 takes the probe, tab 2's picker shows
// it in use and clears the selection.
TEST_F(ConnectWizardTest, TwoTabsWithWizardPickers) {
    start(scenario(), false);
    app().newTab();
    ASSERT_EQ(app().tabs()->count(), 2);
    DeviceTabViewModel* first = app().tabs()->at(0);
    DeviceTabViewModel* second = app().tabs()->at(1);
    QSignalSpy dropped(&app().diagWorker(), &hwio::gui::DiagWorker::requestDropped);
    ASSERT_TRUE(app().launcher()->start(ConnectWizard::wizardId, 0));
    ASSERT_TRUE(app().launcher()->start(ConnectWizard::wizardId, 1));
    auto* one = qobject_cast<ConnectWizard*>(first->wizard()->wizard());
    auto* two = qobject_cast<ConnectWizard*>(second->wizard()->wizard());
    ASSERT_NE(one, nullptr);
    ASSERT_NE(two, nullptr);
    ASSERT_EQ(one->step(), ConnectWizard::Probe);
    ASSERT_EQ(two->step(), ConnectWizard::Probe);
    ASSERT_TRUE(waitFor([&] { return one->canPrimary() && two->canPrimary(); }));
    hwio::gui::test::spin(std::chrono::milliseconds(1000));
    EXPECT_LE(dropped.count(), 2); // no ping-pong between the two wizards (ADR-0394)
    one->primary();                // tab 1 takes 801000001
    ASSERT_EQ(first->probeSerial(), std::optional<std::uint32_t>(801000001));
    EXPECT_FALSE(two->picker()->selectedSerial().has_value());
    EXPECT_FALSE(two->canPrimary());
    EXPECT_FALSE(second->probeSerial().has_value());
}

} // namespace
