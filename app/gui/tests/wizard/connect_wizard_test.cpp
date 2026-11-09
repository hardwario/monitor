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
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/probe_picker_view_model.h"
#include "wizard/check_list_model.h"
#include "wizard/connect_wizard.h"
#include "wizard/troubleshooting_wizard.h"
#include "wizard/wizard.h"
#include "wizard/wizard_view_model.h"

#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
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
        if (m_app && m_app->wizard()->isOpen()) {
            m_app->wizard()->cancel(true);
            (void)waitFor([this] { return !m_app->wizard()->isOpen(); });
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
        return qobject_cast<ConnectWizard*>(app().wizard()->wizard());
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
    EXPECT_EQ(app().wizard()->title(), QStringLiteral("Connect a device"));
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
    EXPECT_EQ(app().wizard()->busyCancelText(),
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
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Connected to CHESTER app · 801000001."));
    EXPECT_EQ(wizard->current().primaryText, QStringLiteral("Open console"));
    EXPECT_FALSE(wizard->canBack());
    QSignalSpy handOff(app().wizard(), &hwio::gui::WizardViewModel::handOff);
    app().wizard()->primary();
    ASSERT_EQ(handOff.count(), 1);
    EXPECT_EQ(handOff.at(0).at(0).toString(), QStringLiteral("open-console"));
    EXPECT_FALSE(app().wizard()->isOpen());
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
    app().wizard()->cancel(false);
    EXPECT_FALSE(app().wizard()->isOpen());
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
    app().wizard()->cancel(false);

    wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetConnectFailed)));
    EXPECT_EQ(wizard->step(), ConnectWizard::Cable);
    EXPECT_FALSE(wizard->canBack()); // the Probe step was not shown
    app().wizard()->cancel(false);

    wizard = open(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNotConnected)));
    EXPECT_EQ(wizard->step(), ConnectWizard::Check);
    ASSERT_TRUE(checked(*wizard));
    app().wizard()->cancel(false);
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
        return app().wizard()->isOpen() &&
               app().wizard()->wizardId() == QStringLiteral("lte-update");
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
    QSignalSpy handOff(app().wizard(), &hwio::gui::WizardViewModel::handOff);
    wizard->action(QStringLiteral("open-flash-page"));
    ASSERT_EQ(handOff.count(), 1);
    EXPECT_EQ(handOff.at(0).at(0).toString(), QStringLiteral("open-flash-page:recover:app"));
    EXPECT_FALSE(app().wizard()->isOpen());
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
    QSignalSpy handOff(app().wizard(), &hwio::gui::WizardViewModel::handOff);
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
        return app().wizard()->isOpen() &&
               app().wizard()->wizardId() == QStringLiteral("troubleshooting");
    }));
    const auto* troubleshooting =
        qobject_cast<hwio::gui::TroubleshootingWizard*>(app().wizard()->wizard());
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
    app().wizard()->cancel(false);
    EXPECT_TRUE(app().wizard()->isOpen()); // asks first
    app().wizard()->cancel(true);
    ASSERT_TRUE(waitFor([this] { return !app().wizard()->isOpen(); }, std::chrono::seconds(20)));
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }, std::chrono::seconds(20)));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
}

} // namespace
