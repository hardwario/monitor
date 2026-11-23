// TroubleshootingWizard (plan/features/0009 with the 0035 amendments § C) against the fake J-Link
// library: the topic list without an error, an error's introduction and topic, every topic end to
// end, the probe step on a tab without a probe (ADR-0299) and the Bluetooth-tab variant, the manual
// actions ([Reset], [Use 1000 kHz] per ADR-0298), the J-Link firmware actions (ADR-0291 pattern),
// the setup-wizard hand-off of a failed environment check (ADR-0297), the report at the end and
// cancel during Diagnose.

#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/device/device_session.h"
#include "hwio/diag/check.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "wizard/check_list_model.h"
#include "wizard/report_view_model.h"
#include "wizard/troubleshooting_topics.h"
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
#include <QtCore/qtmetamacros.h>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using hwio::ErrorCode;
using hwio::diag::CheckStatus;
using hwio::gui::AppViewModel;
using hwio::gui::DeviceTabViewModel;
using hwio::gui::StepKind;
using hwio::gui::TopicStepKind;
using hwio::gui::TroubleshootingWizard;
using hwio::gui::test::tabWizard;
using hwio::gui::test::waitFor;

std::string scenario(const std::string& probe = {}) {
    return std::string(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
        "product": "Test Probe", "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",)") +
           probe + R"("rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}]},
        "chip": {}}]})";
}

QStringList actionIds(const hwio::gui::WizardStep& step) {
    QStringList ids;
    for (const QVariant& action : step.actions) {
        ids.append(action.toMap().value(QStringLiteral("id")).toString());
    }
    return ids;
}

class TroubleshootingWizardTest : public hwio::gui::test::GuiTest {
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

    void start(const std::string& json = scenario(), bool configure = true) {
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

    TroubleshootingWizard* open(ErrorCode code, std::string detail = {}) {
        EXPECT_TRUE(app().launcher()->open(
            TroubleshootingWizard::wizardId,
            hwio::gui::ErrorInfo(hwio::makeError(code, std::move(detail))), 0));
        return qobject_cast<TroubleshootingWizard*>(tabWizard(app()).wizard());
    }
    TroubleshootingWizard* openList() {
        EXPECT_TRUE(app().launcher()->start(TroubleshootingWizard::wizardId, 0));
        return qobject_cast<TroubleshootingWizard*>(tabWizard(app()).wizard());
    }

    static bool idle(const TroubleshootingWizard& wizard) {
        return waitFor([&] { return !wizard.running(); }, std::chrono::seconds(60));
    }

    // Next until "Still not working?"; false when a step never allows Next.
    static bool walkToEnd(TroubleshootingWizard& wizard) {
        for (int guard = 0; guard < 16 && wizard.stepId() != QStringLiteral("end"); ++guard) {
            if (!idle(wizard) || !wizard.canPrimary()) {
                return false;
            }
            wizard.primary();
        }
        return wizard.stepId() == QStringLiteral("end");
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    std::string m_scenario;
};

TEST_F(TroubleshootingWizardTest, WithoutAnErrorTheTopicListComesFirst) {
    start();
    TroubleshootingWizard* wizard = openList();
    ASSERT_NE(wizard, nullptr);
    EXPECT_EQ(tabWizard(app()).title(), QStringLiteral("Troubleshooting"));
    hwio::gui::WizardStep step = wizard->current();
    EXPECT_EQ(step.kind, StepKind::Choice);
    EXPECT_EQ(step.heading, QStringLiteral("What is the problem?"));
    QStringList titles;
    for (const QVariant& choice : step.choices) {
        titles.append(choice.toMap().value(QStringLiteral("title")).toString());
    }
    EXPECT_EQ(titles,
              (QStringList{QStringLiteral("No J-Link found"),
                           QStringLiteral("The J-Link is used by another program"),
                           QStringLiteral("The device has no power or does not respond"),
                           QStringLiteral("The J-Link is on the LTE connector"),
                           QStringLiteral("The console stays empty"),
                           QStringLiteral("Flashing fails"), QStringLiteral("J-Link firmware"),
                           QStringLiteral("LTE modem firmware"), QStringLiteral("Bluetooth")}));
    EXPECT_FALSE(wizard->canPrimary());
    wizard->choose(QStringLiteral("other-connector"));
    ASSERT_TRUE(wizard->canPrimary());
    wizard->primary();
    ASSERT_NE(wizard->topic(), nullptr);
    EXPECT_EQ(wizard->topic()->id, "other-connector");
    step = wizard->current();
    EXPECT_EQ(step.kind, StepKind::Info);
    EXPECT_EQ(step.heading, QStringLiteral("The J-Link is on the LTE connector"));
    EXPECT_EQ(actionIds(step), QStringList{QStringLiteral("step-action")});
    // Back returns to the list with the topic selected.
    ASSERT_TRUE(wizard->canBack());
    wizard->back();
    EXPECT_EQ(wizard->current().kind, StepKind::Choice);
    EXPECT_TRUE(wizard->canPrimary());
    // [Update LTE firmware…] hands off to the LTE update.
    wizard->primary();
    QSignalSpy handOff(&tabWizard(app()), &hwio::gui::WizardViewModel::handOff);
    wizard->action(QStringLiteral("step-action"));
    ASSERT_EQ(handOff.count(), 1);
    EXPECT_EQ(handOff.at(0).at(0).toString(), QStringLiteral("open-wizard:lte-update"));
}

TEST_F(TroubleshootingWizardTest, AnErrorOpensItsTopic) {
    start();
    TroubleshootingWizard* wizard = open(ErrorCode::TargetRttNotFound, "no control block");
    ASSERT_NE(wizard, nullptr);
    ASSERT_NE(wizard->topic(), nullptr);
    EXPECT_EQ(wizard->topic()->id, "no-console");
    // The introduction: the headline as the heading, the cause in the `wizard-error` control.
    hwio::gui::WizardStep step = wizard->current();
    EXPECT_EQ(step.kind, StepKind::Info);
    EXPECT_EQ(step.heading,
              hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetRttNotFound)).message());
    EXPECT_EQ(step.body, QStringLiteral("Let's find the cause."));
    EXPECT_EQ(step.embedded, QStringLiteral("wizard-error"));
    EXPECT_EQ(step.embeddedOptions.value(QStringLiteral("showMessage")), QVariant(false));
    EXPECT_FALSE(step.showInStepList);
    EXPECT_EQ(wizard->shownError().detail(), QStringLiteral("no control block"));
    EXPECT_EQ(tabWizard(app()).embedded(), wizard);
    wizard->primary();
    // A Diagnose (all): six rows on a probe-only session; the tab is busy meanwhile.
    ASSERT_NE(wizard->topicStep(), nullptr);
    EXPECT_EQ(wizard->topicStep()->kind, TopicStepKind::AutoDiagnose);
    EXPECT_EQ(wizard->checks()->count(), 6);
    EXPECT_EQ(tab().busyPage(), QStringLiteral("troubleshooting"));
    ASSERT_TRUE(idle(*wizard));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("rtt")), CheckStatus::Ok);
    EXPECT_TRUE(actionIds(wizard->current()).contains(QStringLiteral("check-again")));
    wizard->primary();
    // M [Reset]: reset(target, Go) on the session, a toast when done.
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Reset the device"));
    wizard->action(QStringLiteral("step-action"));
    ASSERT_TRUE(idle(*wizard));
    wizard->primary();
    // A rtt: only the rtt row (Diagnose stops after it, ADR-0296).
    EXPECT_EQ(wizard->checks()->count(), 1);
    ASSERT_TRUE(idle(*wizard));
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("rtt")), CheckStatus::Ok);
    wizard->primary();
    // H [Flash firmware…] (the flash wizard is registered).
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Flash firmware with the console"));
    wizard->primary();
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Use Automatic speed"));
    wizard->primary();
    // Still not working?
    step = wizard->current();
    EXPECT_EQ(step.kind, StepKind::Done);
    EXPECT_EQ(step.heading, QStringLiteral("Still not working?"));
    EXPECT_EQ(actionIds(step), QStringList{QStringLiteral("report")});
    // [Create diagnostic report…]: this wizard closes, the report dialog opens.
    wizard->action(QStringLiteral("report"));
    EXPECT_FALSE(tabWizard(app()).isOpen());
    EXPECT_TRUE(app().report()->isOpen());
    app().report()->close();
    // The probe-only session of the checks was released.
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));
}

TEST_F(TroubleshootingWizardTest, EveryTopicRunsEndToEnd) {
    start();
    for (const hwio::gui::Topic& topic : hwio::gui::troubleshootingTopics()) {
        SCOPED_TRACE(std::string(topic.id));
        TroubleshootingWizard* wizard = nullptr;
        if (!topic.codes.empty()) {
            wizard = open(topic.codes.front());
            ASSERT_NE(wizard, nullptr);
            ASSERT_NE(wizard->topic(), nullptr);
            EXPECT_EQ(wizard->topic(), &topic);
        } else {
            wizard = openList();
            ASSERT_NE(wizard, nullptr);
            wizard->choose(
                QString::fromLatin1(topic.id.data(), static_cast<qsizetype>(topic.id.size())));
            wizard->primary();
            ASSERT_EQ(wizard->topic(), &topic);
        }
        ASSERT_TRUE(walkToEnd(*wizard)) << wizard->stepId().toStdString();
        tabWizard(app()).cancel(false);
        ASSERT_TRUE(waitFor([this] { return !tabWizard(app()).isOpen(); }));
        ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr && !tab().isBusy(); }));
    }
}

// Plan 0056 (user decision 2026-11-23): too low supply voltage of the LTE MCU (ADR-0383) opens the
// no-power topic.
TEST_F(TroubleshootingWizardTest, LowVoltageOpensTheNoPowerTopic) {
    start();
    TroubleshootingWizard* wizard = open(ErrorCode::TargetLowVoltage);
    ASSERT_NE(wizard, nullptr);
    wizard->primary(); // introduction
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Power the device"));
}

TEST_F(TroubleshootingWizardTest, ATabWithoutAProbeChoosesOneBeforeTheChecks) {
    start(scenario(), false);
    ASSERT_TRUE(tab().choosing());
    TroubleshootingWizard* wizard = open(ErrorCode::TargetNoPower);
    ASSERT_NE(wizard, nullptr);
    wizard->primary(); // introduction
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Power the device"));
    wizard->primary();
    // ADR-0299: the probe step before the first Diagnose step.
    EXPECT_EQ(wizard->stepId(), QStringLiteral("probe"));
    EXPECT_EQ(wizard->current().embedded, QStringLiteral("probe-picker"));
    ASSERT_NE(wizard->picker(), nullptr);
    EXPECT_EQ(tabWizard(app()).embedded(), wizard->picker());
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    EXPECT_FALSE(tab().choosing());
    EXPECT_EQ(tab().probeSerial(), std::optional<std::uint32_t>(801000001));
    // A Diagnose probe, power: it runs up to `connect`, which is not shown (plan 0009 note).
    EXPECT_EQ(wizard->checks()->count(), 2);
    ASSERT_TRUE(idle(*wizard));
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("power")), CheckStatus::Ok);
    EXPECT_EQ(wizard->checks()->row(QStringLiteral("connect")), nullptr);
    // Back skips the probe step.
    wizard->back();
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Power the device"));
}

TEST_F(TroubleshootingWizardTest, UseOneThousandKilohertz) {
    start();
    TroubleshootingWizard* wizard = open(ErrorCode::TargetConnectFailed);
    ASSERT_NE(wizard, nullptr);
    wizard->primary();
    ASSERT_TRUE(idle(*wizard));
    ASSERT_NE(tab().session(), nullptr);
    wizard->primary(); // cable
    wizard->primary(); // speed
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Lower the J-Link speed"));
    wizard->action(QStringLiteral("step-action"));
    // ADR-0298: stored per probe; the wizard's probe-only session is closed.
    EXPECT_FALSE(tab().connection()->autoSpeed());
    EXPECT_EQ(tab().connection()->speedKhz(), 1000);
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));
    wizard->primary();
    ASSERT_TRUE(idle(*wizard));
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("identify")), CheckStatus::Ok);
    tabWizard(app()).cancel(false);
    // The wizard's probe-only session is released when the wizard is deleted.
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));

    // A connected tab connects again (a new session with the speed).
    tab().connectDevice();
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    const hwio::device::DeviceSession* before = tab().session();
    wizard = open(ErrorCode::TargetProgramFailed);
    wizard->primary();
    ASSERT_TRUE(idle(*wizard));
    wizard->primary();
    EXPECT_EQ(wizard->current().heading, QStringLiteral("Lower the J-Link speed"));
    wizard->action(QStringLiteral("step-action"));
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; },
                        std::chrono::seconds(20)));
    EXPECT_NE(tab().session(), before);
}

TEST_F(TroubleshootingWizardTest, OutdatedJLinkFirmwareActions) {
    start(scenario(R"("libraryFirmware": "J-Link V11 compiled Sep 21 2025 10:38:43",)"));
    TroubleshootingWizard* wizard = open(ErrorCode::ProbeFirmwareOutdated);
    ASSERT_NE(wizard, nullptr);
    wizard->primary();
    ASSERT_TRUE(idle(*wizard));
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("probe-firmware")), CheckStatus::Warning);
    wizard->primary();
    const hwio::gui::WizardStep step = wizard->current();
    EXPECT_EQ(step.kind, StepKind::Commands);
    EXPECT_EQ(step.commands,
              QStringList{QStringLiteral("hwio jlink update-firmware -n 801000001")});
    ASSERT_EQ(step.actions.size(), 1);
    QVariantMap action = step.actions.at(0).toMap();
    EXPECT_EQ(action.value(QStringLiteral("id")).toString(),
              QStringLiteral("probe-firmware:801000001"));
    EXPECT_TRUE(action.value(QStringLiteral("enabled")).toBool());
    EXPECT_EQ(action.value(QStringLiteral("text")).toString(),
              QStringLiteral("Update J-Link firmware 801000001…"));
    // ADR-0246: while a tab is connected to the probe the action is disabled and says why.
    tab().connectDevice();
    ASSERT_TRUE(waitFor([this] { return tab().state() == DeviceTabViewModel::Connected; }));
    action = wizard->current().actions.at(0).toMap();
    EXPECT_FALSE(action.value(QStringLiteral("enabled")).toBool());
    EXPECT_EQ(action.value(QStringLiteral("text")).toString(),
              QStringLiteral("Disconnect tab 1 first."));
}

TEST_F(TroubleshootingWizardTest, AFailedNrfUtilCheckOpensTheSetupWizard) {
    start();
    TroubleshootingWizard* wizard = open(ErrorCode::FirmwareModemUpdateFailed);
    ASSERT_NE(wizard, nullptr);
    wizard->primary();
    ASSERT_TRUE(idle(*wizard));
    EXPECT_NE(wizard->checks()->status(QStringLiteral("nrfutil")), CheckStatus::Ok);
    ASSERT_TRUE(actionIds(wizard->current()).contains(QStringLiteral("setup")));
    QSignalSpy handOffError(&tabWizard(app()), &hwio::gui::WizardViewModel::handOffError);
    wizard->action(QStringLiteral("setup"));
    ASSERT_EQ(handOffError.count(), 1);
    // Plan 0056: the setup wizard is global.
    ASSERT_TRUE(waitFor([this] {
        return app().globalWizard()->isOpen() &&
               app().globalWizard()->wizardId() == QStringLiteral("first-run-setup");
    }));
    EXPECT_FALSE(tabWizard(app()).isOpen());
}

TEST_F(TroubleshootingWizardTest, ABluetoothTabCannotRunTheChecks) {
    start(scenario(), false);
    tab().setTransport(DeviceTabViewModel::bleTransport());
    if (!tab().isBle()) {
        GTEST_SKIP() << "no Bluetooth on this machine";
    }
    TroubleshootingWizard* wizard = open(ErrorCode::TargetConnectFailed);
    ASSERT_NE(wizard, nullptr);
    wizard->primary();
    EXPECT_EQ(wizard->checks()->count(), 4);
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("probe")), CheckStatus::Skipped);
    EXPECT_EQ(wizard->checks()->row(QStringLiteral("probe"))->result,
              "Not checked: no J-Link chosen");
    EXPECT_EQ(actionIds(wizard->current()), QStringList{QStringLiteral("connect-device")});
}

TEST_F(TroubleshootingWizardTest, CancelDuringDiagnose) {
    start(scenario(R"("connectDelayMs": 1500,)"));
    TroubleshootingWizard* wizard = open(ErrorCode::TargetConnectFailed);
    ASSERT_NE(wizard, nullptr);
    wizard->primary();
    ASSERT_TRUE(wizard->running());
    tabWizard(app()).cancel(true);
    ASSERT_TRUE(waitFor([this] { return !tabWizard(app()).isOpen(); }, std::chrono::seconds(20)));
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }, std::chrono::seconds(20)));
    EXPECT_TRUE(tab().busyPage().isEmpty());
}

// Review 0009 F4: Back and Next after the probe step do not show it again.
TEST_F(TroubleshootingWizardTest, BackAndNextSkipTheProbeStep) {
    start(scenario(), false);
    TroubleshootingWizard* wizard = open(ErrorCode::TargetNoPower);
    ASSERT_NE(wizard, nullptr);
    wizard->primary(); // introduction
    wizard->primary(); // power hint
    ASSERT_EQ(wizard->stepId(), QStringLiteral("probe"));
    ASSERT_TRUE(waitFor([&] { return wizard->canPrimary(); }));
    wizard->primary();
    EXPECT_EQ(wizard->stepId(), QStringLiteral("step-2"));
    ASSERT_TRUE(idle(*wizard));
    wizard->back();
    EXPECT_EQ(wizard->stepId(), QStringLiteral("step-1"));
    wizard->primary();
    EXPECT_EQ(wizard->stepId(), QStringLiteral("step-2"));
    ASSERT_TRUE(idle(*wizard));
    EXPECT_EQ(wizard->checks()->status(QStringLiteral("power")), CheckStatus::Ok);
}

// Review 0009 F5: a step that is not shown and fails gives its reason to the shown row.
TEST_F(TroubleshootingWizardTest, AHiddenFailedStepExplainsTheShownRow) {
    start(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
        "product": "Test Probe", "chip": {"family": "nrf91"}}]})");
    TroubleshootingWizard* wizard = open(ErrorCode::TargetRttNotFound);
    ASSERT_NE(wizard, nullptr);
    wizard->primary(); // introduction
    ASSERT_TRUE(idle(*wizard));
    wizard->primary(); // Diagnose (all) → [Reset]
    wizard->primary(); // A rtt
    ASSERT_TRUE(idle(*wizard));
    ASSERT_EQ(wizard->checks()->count(), 1);
    const hwio::diag::CheckResult* rtt = wizard->checks()->row(QStringLiteral("rtt"));
    ASSERT_NE(rtt, nullptr);
    EXPECT_EQ(rtt->status, CheckStatus::Failed);
    EXPECT_EQ(rtt->error ? rtt->error->code : ErrorCode::InternalUnexpected,
              ErrorCode::TargetWrongFamily);
}

// Review 0009 F2 (ADR-0300, ADR-0297): troubleshooting reached from a failed flash ([Help me fix
// this]) takes over the wizards' session; closing it leaves no session.
TEST_F(TroubleshootingWizardTest, ClosingTheLastChainedWizardReleasesTheSession) {
    start();
    TroubleshootingWizard* wizard = open(ErrorCode::TargetConnectFailed);
    ASSERT_NE(wizard, nullptr);
    wizard->primary();
    ASSERT_TRUE(idle(*wizard));
    ASSERT_TRUE(tab().wizardOwnsSession());
    const hwio::device::DeviceSession* session = tab().session();
    // [Setup wizard…]-like hand-off to the Connect wizard (per tab): the session is kept.
    emit wizard->handOffError(hwio::gui::ErrorInfo(hwio::makeError(ErrorCode::TargetNoPower)));
    emit wizard->finished(false);
    ASSERT_TRUE(waitFor([this] {
        return tabWizard(app()).isOpen() &&
               tabWizard(app()).wizardId() == QStringLiteral("connect-device");
    }));
    hwio::gui::test::spin(std::chrono::milliseconds(200));
    EXPECT_EQ(tab().session(), session);
    EXPECT_TRUE(tab().wizardOwnsSession());
    tabWizard(app()).cancel(true);
    ASSERT_TRUE(waitFor([this] { return tab().session() == nullptr; }));
}

} // namespace
