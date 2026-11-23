// The wizard framework (plan/features/0007, C1 + C3): WizardViewModel with the test wizard
// (navigation, busy cancel, one wizard at a time, hand-off), WizardLauncher (canOpen),
// CheckListModel, illustration slots, DiagWorker and ReportViewModel (collect → save → copy).

#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/diag/check.h"
#include "pages/builtin_pages.h"
#include "support/test_wizard.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "wizard/check_list_model.h"
#include "wizard/report_view_model.h"
#include "wizard/wizard.h"
#include "wizard/wizard_launcher.h"
#include "wizard/wizard_view_model.h"
#include "workers/diag_worker.h"

#include <QByteArray>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace {

using hwio::ErrorCode;
using hwio::gui::AppViewModel;
using hwio::gui::CheckListModel;
using hwio::gui::DeviceTabViewModel;
using hwio::gui::ErrorInfo;
using hwio::gui::StepKind;
using hwio::gui::WizardViewModel;
using hwio::gui::test::TestWizard;
using hwio::gui::test::waitFor;

class WizardTest : public hwio::gui::test::GuiTest {
protected:
    [[nodiscard]] std::unique_ptr<AppViewModel> makeWizardApp(bool withWizard = true,
                                                              bool withGlobal = false) const {
        return std::make_unique<AppViewModel>(hwio::gui::AppOptions{
            .settings = openSettings(),
            .registry = registryOptions(),
            .scanInterval = std::chrono::milliseconds(200),
            .registerExtensions =
                [withWizard, withGlobal](AppViewModel& app) {
                    // The test wizards first: they keep the ids "connect-device" and
                    // "first-run-setup" (the first registration wins; the real Connect and
                    // Setup wizards of plans 0008/0009 are then not added).
                    if (withWizard) {
                        hwio::gui::test::registerTestWizard(
                            app, QUrl(QStringLiteral("qrc:/TestEmbedded.qml")));
                    }
                    if (withGlobal) {
                        hwio::gui::test::registerGlobalTestWizard(app); // plan 0052
                    }
                    hwio::gui::registerBuiltInPages(app);
                },
            .crashDirectory = {}});
    }

    // Plan 0052: the current tab's wizard.
    static WizardViewModel* tabWizard(AppViewModel& app) {
        return app.currentTabViewModel()->wizard();
    }

    static TestWizard* testWizard(AppViewModel& app) {
        return qobject_cast<TestWizard*>(tabWizard(app)->wizard());
    }

    static int stepKind(const WizardViewModel& wizard) {
        return wizard.step().value(QStringLiteral("kind")).toInt();
    }
};

TEST_F(WizardTest, LauncherOffersOnlyRegisteredWizards) {
    const auto app = makeWizardApp();
    EXPECT_TRUE(app->launcher()->canOpen(QStringLiteral("connect-device")));
    // Plan 0009 registers connect-device and troubleshooting; BlePairing is not (ADR-0250).
    EXPECT_TRUE(app->launcher()->canOpen(QStringLiteral("troubleshooting")));
    EXPECT_FALSE(app->launcher()->canOpen(QStringLiteral("ble-pairing")));
    EXPECT_TRUE(app->launcher()->canOpen(QStringLiteral("first-run-setup"))); // plan 0008
    EXPECT_FALSE(app->launcher()->canOpen(QString()));
    EXPECT_FALSE(app->launcher()->canOpen(QStringLiteral("nonsense")));
    EXPECT_FALSE(app->launcher()->start(QStringLiteral("ble-pairing")));
    EXPECT_FALSE(tabWizard(*app)->isOpen());

    // Without the test wizard, connect-device is the real Connect wizard (plan 0009).
    const auto without = makeWizardApp(false);
    EXPECT_TRUE(without->launcher()->canOpen(QStringLiteral("connect-device")));
}

TEST_F(WizardTest, NavigationThroughEveryStepKind) {
    const auto app = makeWizardApp();
    WizardViewModel& wizard = *tabWizard(*app);
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    ASSERT_TRUE(wizard.isOpen());
    EXPECT_EQ(wizard.wizardId(), QStringLiteral("connect-device"));
    EXPECT_EQ(wizard.title(), QStringLiteral("Test wizard"));
    EXPECT_EQ(wizard.tab(), app->currentTabViewModel());
    EXPECT_EQ(stepKind(wizard), StepKind::Info);
    EXPECT_FALSE(wizard.canBack()); // hidden on the first step
    EXPECT_EQ(wizard.currentIndex(), 0);
    EXPECT_EQ(wizard.steps().size(), 6);
    EXPECT_EQ(wizard.steps().at(0).toMap().value(QStringLiteral("state")),
              QStringLiteral("current"));
    EXPECT_EQ(wizard.stepLabel(), QStringLiteral("Step 1 of 6 · Probe"));
    EXPECT_EQ(wizard.stepAnnouncement(), QStringLiteral("Step 1 of 6, Probe"));

    wizard.primary();
    EXPECT_EQ(stepKind(wizard), StepKind::Choice);
    EXPECT_TRUE(wizard.canBack());
    EXPECT_EQ(wizard.steps().at(0).toMap().value(QStringLiteral("state")), QStringLiteral("done"));
    wizard.choose(QStringLiteral("lte"));
    const QVariantList choices = wizard.step().value(QStringLiteral("choices")).toList();
    ASSERT_EQ(choices.size(), 2);
    EXPECT_TRUE(choices.at(1).toMap().value(QStringLiteral("selected")).toBool());

    wizard.primary();
    EXPECT_EQ(stepKind(wizard), StepKind::Checks);
    CheckListModel* checks = wizard.checks();
    ASSERT_NE(checks, nullptr);
    EXPECT_EQ(checks->count(), 2);
    EXPECT_TRUE(checks->anyFailed());
    EXPECT_EQ(wizard.step().value(QStringLiteral("primaryText")), QStringLiteral("Check again"));
    wizard.primary(); // Check again
    EXPECT_TRUE(checks->allPassed());
    EXPECT_EQ(wizard.step().value(QStringLiteral("primaryText")), QStringLiteral("Next"));

    wizard.back();
    EXPECT_EQ(stepKind(wizard), StepKind::Choice);
    wizard.primary();
    wizard.primary();
    EXPECT_EQ(stepKind(wizard), StepKind::Commands);
    EXPECT_EQ(wizard.step().value(QStringLiteral("commands")).toStringList().size(), 2);

    wizard.primary();
    EXPECT_EQ(stepKind(wizard), StepKind::Embedded);
    EXPECT_EQ(wizard.embeddedSource(), QUrl(QStringLiteral("qrc:/TestEmbedded.qml")));
    ASSERT_NE(wizard.embedded(), nullptr);

    wizard.primary(); // "Flash": the operation finishes at once in this test
    ASSERT_TRUE(testWizard(*app)->busy());
    testWizard(*app)->finishOperation();
    EXPECT_EQ(stepKind(wizard), StepKind::Done);
    QSignalSpy handOff(&wizard, &WizardViewModel::handOff);
    wizard.action(QStringLiteral("open-console"));
    EXPECT_EQ(handOff.count(), 1);
    wizard.primary(); // Finish
    EXPECT_FALSE(wizard.isOpen());
}

TEST_F(WizardTest, ErrorContextStartsAtTheExplainingStep) {
    const auto app = makeWizardApp();
    const ErrorInfo error(hwio::makeError(ErrorCode::ProbeNotFound));
    ASSERT_EQ(error.wizard(), QStringLiteral("connect-device"));
    ASSERT_TRUE(app->launcher()->open(error.wizard(), error, 0));
    EXPECT_EQ(stepKind(*tabWizard(*app)), StepKind::Checks);
    EXPECT_EQ(testWizard(*app)->context().code(), QStringLiteral("probe.not_found"));
}

TEST_F(WizardTest, OnlyOneWizardAtATime) {
    const auto app = makeWizardApp();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    QSignalSpy raised(tabWizard(*app), &WizardViewModel::raiseRequested);
    EXPECT_FALSE(app->launcher()->start(QStringLiteral("connect-device")));
    EXPECT_EQ(raised.count(), 1);
    EXPECT_TRUE(tabWizard(*app)->isOpen());
}

TEST_F(WizardTest, CancelWhileBusyAsksFirst) {
    const auto app = makeWizardApp();
    WizardViewModel& wizard = *tabWizard(*app);
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    DeviceTabViewModel* tab = app->currentTabViewModel();
    testWizard(*app)->goTo(4);
    testWizard(*app)->setCancelFinishesImmediately(false);
    wizard.primary(); // the operation starts
    EXPECT_EQ(wizard.busyCancelText(), TestWizard::busyText);
    EXPECT_TRUE(tab->isBusy());
    EXPECT_EQ(tab->busyPage(), QStringLiteral("connect-device"));
    EXPECT_FALSE(wizard.canBack()); // hidden while busy

    QSignalSpy confirm(&wizard, &WizardViewModel::cancelConfirmationNeeded);
    wizard.cancel(false);
    ASSERT_EQ(confirm.count(), 1);
    EXPECT_EQ(confirm.at(0).at(0).toString(), TestWizard::busyText);
    EXPECT_TRUE(wizard.isOpen());

    wizard.cancel(true); // [Cancel and close]: waits for the operation to end
    EXPECT_TRUE(wizard.closing());
    EXPECT_TRUE(wizard.isOpen());
    EXPECT_EQ(testWizard(*app)->cancelCalls(), 1);
    testWizard(*app)->finishOperation();
    EXPECT_FALSE(wizard.isOpen());
    EXPECT_FALSE(tab->isBusy());
}

TEST_F(WizardTest, CancelWhenIdleClosesAtOnce) {
    const auto app = makeWizardApp();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    QSignalSpy confirm(tabWizard(*app), &WizardViewModel::cancelConfirmationNeeded);
    tabWizard(*app)->cancel(false);
    EXPECT_EQ(confirm.count(), 0);
    EXPECT_FALSE(tabWizard(*app)->isOpen());
    // The wizard object is deleted on the next event-loop turn.
    EXPECT_TRUE(waitFor([&] { return tabWizard(*app)->findChildren<TestWizard*>().isEmpty(); }));
}

TEST_F(WizardTest, ClosingTheTabClosesItsWizard) {
    const auto app = makeWizardApp();
    app->newTab();
    ASSERT_EQ(app->tabs()->count(), 2);
    DeviceTabViewModel* tab = app->tabs()->at(1);
    ASSERT_TRUE(tab->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    ASSERT_TRUE(tab->ensureSession(false)); // a wizard's probe-only session
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 1));
    const QPointer<WizardViewModel> host = tab->wizard();
    EXPECT_EQ(host->tab(), tab);
    const QPointer<TestWizard> wizard = qobject_cast<TestWizard*>(host->wizard());
    ASSERT_FALSE(wizard.isNull());
    app->closeTab(1);
    // Review V2: closed at once, before the tab shut its session down (not on `destroyed`).
    ASSERT_FALSE(host.isNull()); // the tab and its wizard host are deleted later
    EXPECT_FALSE(host->isOpen());
    ASSERT_FALSE(wizard.isNull()); // deleted later
    EXPECT_EQ(wizard->cancelCalls(), 1);
    EXPECT_TRUE(wizard->tabSessionAtCancel());
    EXPECT_TRUE(waitFor([&] { return wizard.isNull() && host.isNull(); }));
}

TEST_F(WizardTest, ClosingAnotherTabKeepsTheWizard) {
    const auto app = makeWizardApp();
    app->newTab();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 0));
    app->closeTab(1);
    WizardViewModel* host = app->tabs()->at(0)->wizard();
    EXPECT_TRUE(host->isOpen());
    EXPECT_EQ(host->tab(), app->tabs()->at(0));
}

// Plan 0052 (ADR-0386): one wizard per tab; two tabs run their wizards at the same time.
TEST_F(WizardTest, TwoTabsRunWizardsAtOnce) {
    const auto app = makeWizardApp();
    app->newTab();
    ASSERT_EQ(app->tabs()->count(), 2);
    DeviceTabViewModel* first = app->tabs()->at(0);
    DeviceTabViewModel* second = app->tabs()->at(1);
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 0));
    EXPECT_EQ(app->currentTab(), 0); // the tab that opened the wizard is shown
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 1));
    EXPECT_EQ(app->currentTab(), 1);
    ASSERT_TRUE(first->wizard()->isOpen());
    ASSERT_TRUE(second->wizard()->isOpen());
    EXPECT_EQ(first->wizard()->tab(), first);
    EXPECT_EQ(second->wizard()->tab(), second);
    EXPECT_FALSE(app->globalWizard()->isOpen());
    EXPECT_EQ(first->currentPage(), DeviceTabViewModel::wizardPageId());
    EXPECT_EQ(second->currentPage(), DeviceTabViewModel::wizardPageId());

    // An operation in one wizard keeps only its tab busy; the other wizard goes on.
    auto* firstWizard = qobject_cast<TestWizard*>(first->wizard()->wizard());
    auto* secondWizard = qobject_cast<TestWizard*>(second->wizard()->wizard());
    ASSERT_NE(firstWizard, nullptr);
    ASSERT_NE(secondWizard, nullptr);
    firstWizard->goTo(4);
    first->wizard()->primary();
    EXPECT_TRUE(first->isBusy());
    EXPECT_FALSE(second->isBusy());
    second->wizard()->primary();
    EXPECT_EQ(secondWizard->stepIndex(), 1);
    EXPECT_EQ(firstWizard->stepIndex(), 4);

    second->wizard()->cancel(false);
    EXPECT_FALSE(second->wizard()->isOpen());
    EXPECT_TRUE(first->wizard()->isOpen());
    firstWizard->finishOperation();
    EXPECT_FALSE(first->isBusy());
}

// Plan 0052: a second wizard in the same tab is refused and the open one is shown (its tab
// becomes current, its page is the wizard view).
TEST_F(WizardTest, SecondWizardInATabIsRefusedAndShown) {
    const auto app = makeWizardApp();
    app->newTab();
    DeviceTabViewModel* first = app->tabs()->at(0);
    ASSERT_TRUE(first->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 0));
    first->setCurrentPage(QStringLiteral("console"));
    app->setCurrentTab(1);
    QSignalSpy raised(first->wizard(), &WizardViewModel::raiseRequested);
    EXPECT_FALSE(app->launcher()->start(QStringLiteral("connect-device"), 0));
    EXPECT_EQ(raised.count(), 1);
    EXPECT_EQ(app->currentTab(), 0);
    EXPECT_EQ(first->currentPage(), DeviceTabViewModel::wizardPageId());
    EXPECT_TRUE(first->wizard()->isOpen());
    EXPECT_FALSE(app->tabs()->at(1)->wizard()->isOpen());
}

// Plan 0052 (ADR-0386): the rail stays usable; the "Wizard" entry shows the wizard again where
// it was; when the wizard closes, the page shown before it comes back.
TEST_F(WizardTest, RailSwitchKeepsTheWizardAndItsStep) {
    const auto app = makeWizardApp();
    DeviceTabViewModel* tab = app->currentTabViewModel();
    ASSERT_TRUE(tab->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    app->showPage(QStringLiteral("flash"));
    EXPECT_EQ(tab->currentPage(), QStringLiteral("flash"));
    // "wizard" is not a page while no wizard is open.
    app->showPage(DeviceTabViewModel::wizardPageId());
    EXPECT_EQ(tab->currentPage(), QStringLiteral("flash"));

    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    EXPECT_EQ(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    tabWizard(*app)->primary();
    EXPECT_EQ(testWizard(*app)->stepIndex(), 1);
    app->showPage(QStringLiteral("console"));
    EXPECT_EQ(tab->currentPage(), QStringLiteral("console"));
    EXPECT_TRUE(tabWizard(*app)->isOpen());
    app->showPage(DeviceTabViewModel::wizardPageId());
    EXPECT_EQ(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    EXPECT_EQ(testWizard(*app)->stepIndex(), 1);

    tabWizard(*app)->cancel(false);
    // The page shown before the wizard view was shown the last time.
    EXPECT_EQ(tab->currentPage(), QStringLiteral("console"));
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    tabWizard(*app)->cancel(false);
    EXPECT_EQ(tab->currentPage(), QStringLiteral("console"));
    app->showPage(QStringLiteral("flash"));
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    tabWizard(*app)->cancel(false);
    EXPECT_EQ(tab->currentPage(), QStringLiteral("flash"));

    // Closed while another page is shown: that page stays.
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    app->showPage(QStringLiteral("console"));
    tabWizard(*app)->cancel(false);
    EXPECT_EQ(tab->currentPage(), QStringLiteral("console"));
}

// Plan 0052: a wizard opened while the tab chooses a device replaces the Choose device page.
TEST_F(WizardTest, WizardWhileChoosingADevice) {
    const auto app = makeWizardApp();
    DeviceTabViewModel* tab = app->currentTabViewModel();
    ASSERT_TRUE(tab->choosing());
    const QString before = tab->currentPage();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    EXPECT_EQ(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    tab->setCurrentPage(before);
    app->showPage(DeviceTabViewModel::wizardPageId()); // the rail's "Wizard" entry
    EXPECT_EQ(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    tabWizard(*app)->cancel(false);
    EXPECT_EQ(tab->currentPage(), before);
}

// Plan 0052: the hand-offs show the page in the wizard's tab; closing the wizard keeps it.
TEST_F(WizardTest, HandOffShowsThePageOfItsTab) {
    const auto app = makeWizardApp();
    app->newTab();
    DeviceTabViewModel* first = app->tabs()->at(0);
    ASSERT_TRUE(first->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 0));
    testWizard(*app)->goTo(5);
    app->setCurrentTab(1);
    first->wizard()->action(QStringLiteral("open-flash-page"));
    EXPECT_EQ(app->currentTab(), 0);
    EXPECT_EQ(first->currentPage(), QStringLiteral("flash"));
    EXPECT_TRUE(first->wizard()->isOpen());
    first->wizard()->primary(); // Finish
    EXPECT_FALSE(first->wizard()->isOpen());
    EXPECT_EQ(first->currentPage(), QStringLiteral("flash"));

    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 0));
    testWizard(*app)->goTo(5);
    first->wizard()->action(QStringLiteral("open-console"));
    first->wizard()->primary();
    EXPECT_EQ(first->currentPage(), QStringLiteral("console"));
}

// Plan 0052: global wizards (setup) run in the global instance, one at a time, next to the tabs'.
TEST_F(WizardTest, GlobalWizardIsHostedByTheApplication) {
    const auto app = makeWizardApp(true, true);
    DeviceTabViewModel* tab = app->currentTabViewModel();
    const QString page = tab->currentPage();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("first-run-setup")));
    WizardViewModel& global = *app->globalWizard();
    ASSERT_TRUE(global.isOpen());
    EXPECT_EQ(global.wizardId(), QStringLiteral("first-run-setup"));
    EXPECT_EQ(global.tab(), nullptr);
    EXPECT_FALSE(tab->wizard()->isOpen());
    EXPECT_EQ(tab->currentPage(), page); // no wizard page in the tab
    QSignalSpy raised(&global, &WizardViewModel::raiseRequested);
    EXPECT_FALSE(app->launcher()->start(QStringLiteral("first-run-setup")));
    EXPECT_EQ(raised.count(), 1);
    // Plan 0056 (D-6): a tab's wizard is refused while the global one is open (toast, raised).
    EXPECT_FALSE(app->launcher()->canStart(QStringLiteral("connect-device")));
    EXPECT_FALSE(app->launcher()->start(QStringLiteral("connect-device")));
    EXPECT_FALSE(tab->wizard()->isOpen());
    EXPECT_EQ(raised.count(), 2);
    EXPECT_TRUE(app->toasts().contains(hwio::gui::WizardLauncher::setupOpenText()));
    global.cancel(false);
    EXPECT_FALSE(global.isOpen());
    // The global one opens while a tab's wizard runs; the tab's keeps running.
    ASSERT_TRUE(app->launcher()->canStart(QStringLiteral("connect-device")));
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("first-run-setup")));
    EXPECT_TRUE(tab->wizard()->isOpen());
    EXPECT_TRUE(global.isOpen());
    EXPECT_TRUE(app->anyWizardOpen());
    global.cancel(false);
    EXPECT_TRUE(tab->wizard()->isOpen());
}

// Plan 0056 (D-6): support's LTE update request (fromRemote) opens while the setup wizard is open.
TEST_F(WizardTest, ARemoteRequestOpensWhileTheSetupWizardIsOpen) {
    const auto app = makeWizardApp(true, true);
    DeviceTabViewModel* tab = app->currentTabViewModel();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("first-run-setup")));
    tab->openWizard(QStringLiteral("connect-device"));
    EXPECT_FALSE(tab->wizard()->isOpen());
    tab->openWizard(QStringLiteral("connect-device"), true);
    EXPECT_TRUE(tab->wizard()->isOpen());
    EXPECT_TRUE(app->globalWizard()->isOpen());
}

// Plan 0056 (D-12): from the Settings tab, the Help menu opens a tab's wizard on the last device
// tab.
TEST_F(WizardTest, HelpMenuFromTheSettingsTabUsesTheLastDeviceTab) {
    const auto app = makeWizardApp();
    app->newTab();
    DeviceTabViewModel* second = app->tabs()->at(1);
    app->openSettings();
    ASSERT_EQ(app->currentTabViewModel(), nullptr);
    ASSERT_TRUE(app->launcher()->canStart(QStringLiteral("connect-device")));
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    EXPECT_TRUE(second->wizard()->isOpen());
    EXPECT_EQ(app->currentTab(), 1);
    EXPECT_FALSE(app->tabs()->at(0)->wizard()->isOpen());
}

// Plan 0056 (D-8, ADR-0300 per tab): a hand-off to another wizard of the same tab keeps the
// wizards' session and the wizard view; the last wizard's close releases the session.
TEST_F(WizardTest, AChainWithinATabKeepsTheSessionAndThePage) {
    const auto app = makeWizardApp();
    DeviceTabViewModel* tab = app->currentTabViewModel();
    ASSERT_TRUE(tab->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    ASSERT_TRUE(tab->ensureWizardSession().has_value());
    ASSERT_TRUE(tab->wizardOwnsSession());
    EXPECT_EQ(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    QStringList pages;
    QObject::connect(tab, &DeviceTabViewModel::currentPageChanged, tab,
                     [&] { pages.append(tab->currentPage()); });
    testWizard(*app)->goTo(5);
    tab->wizard()->action(QStringLiteral("open-wizard:connect-device"));
    EXPECT_TRUE(tab->wizardChainPending());
    tab->wizard()->primary(); // Finish: the chain is pending
    EXPECT_FALSE(tab->wizard()->isOpen());
    EXPECT_EQ(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    ASSERT_TRUE(waitFor([&] { return tab->wizard()->isOpen(); }));
    EXPECT_FALSE(tab->wizardChainPending());
    EXPECT_TRUE(pages.isEmpty()); // no flicker
    hwio::gui::test::spin(std::chrono::milliseconds(100));
    ASSERT_NE(tab->session(), nullptr); // kept for the next wizard
    EXPECT_TRUE(tab->wizardOwnsSession());
    tab->wizard()->cancel(false);
    EXPECT_NE(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    ASSERT_TRUE(waitFor([&] { return tab->session() == nullptr; }));
}

// Plan 0056 (D-8): two tabs chaining in the same event-loop turn each keep their own state.
TEST_F(WizardTest, TwoTabsChainInTheSameTurn) {
    const auto app = makeWizardApp();
    app->newTab();
    std::array<DeviceTabViewModel*, 2> tabs{app->tabs()->at(0), app->tabs()->at(1)};
    for (int i = 0; i < 2; ++i) {
        ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), i));
        qobject_cast<TestWizard*>(tabs.at(i)->wizard()->wizard())->goTo(5);
    }
    for (DeviceTabViewModel* tab : tabs) {
        tab->wizard()->action(QStringLiteral("open-wizard:connect-device"));
        tab->wizard()->primary();
    }
    for (DeviceTabViewModel* tab : tabs) {
        EXPECT_TRUE(tab->wizardChainPending());
        EXPECT_FALSE(tab->wizard()->isOpen());
    }
    ASSERT_TRUE(
        waitFor([&] { return tabs.at(0)->wizard()->isOpen() && tabs.at(1)->wizard()->isOpen(); }));
    for (DeviceTabViewModel* tab : tabs) {
        EXPECT_FALSE(tab->wizardChainPending());
        EXPECT_EQ(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    }
}

// Plan 0056 (D-8): a hand-off from a tab's wizard to the global setup wizard is not a chain: the
// tab restores its page and releases the wizards' session.
TEST_F(WizardTest, AHandOffToTheSetupWizardReleasesTheTabsSession) {
    const auto app = makeWizardApp(true, true);
    DeviceTabViewModel* tab = app->currentTabViewModel();
    ASSERT_TRUE(tab->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    const QString page = tab->currentPage();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    ASSERT_TRUE(tab->ensureWizardSession().has_value());
    testWizard(*app)->goTo(5);
    tab->wizard()->action(QStringLiteral("open-wizard:first-run-setup"));
    EXPECT_FALSE(tab->wizardChainPending());
    tab->wizard()->primary();
    EXPECT_EQ(tab->currentPage(), page);
    ASSERT_TRUE(waitFor([&] { return app->globalWizard()->isOpen(); }));
    ASSERT_TRUE(waitFor([&] { return tab->session() == nullptr; }));
}

// Plan 0056 (D-8): a chained open that is refused restores the page and releases the session.
TEST_F(WizardTest, ARefusedChainRestoresThePage) {
    const auto app = makeWizardApp(true, true);
    DeviceTabViewModel* tab = app->currentTabViewModel();
    ASSERT_TRUE(tab->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    const QString page = tab->currentPage();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    ASSERT_TRUE(tab->ensureWizardSession().has_value());
    testWizard(*app)->goTo(5);
    tab->wizard()->action(QStringLiteral("open-wizard:connect-device"));
    // The setup wizard opens meanwhile: the chained open is refused (D-6).
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("first-run-setup")));
    tab->wizard()->primary();
    EXPECT_EQ(tab->currentPage(), DeviceTabViewModel::wizardPageId());
    ASSERT_TRUE(waitFor([&] { return !tab->wizardChainPending(); }));
    EXPECT_FALSE(tab->wizard()->isOpen());
    EXPECT_EQ(tab->currentPage(), page);
    ASSERT_TRUE(waitFor([&] { return tab->session() == nullptr; }));
}

TEST_F(WizardTest, OpenWizardFromTheTab) {
    const auto app = makeWizardApp();
    DeviceTabViewModel* tab = app->currentTabViewModel();
    tab->openWizard(QStringLiteral("connect-device"));
    ASSERT_TRUE(tabWizard(*app)->isOpen());
    EXPECT_EQ(tabWizard(*app)->tab(), tab);
}

TEST_F(WizardTest, CopyTextGoesToTheClipboard) {
    const auto app = makeWizardApp();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    tabWizard(*app)->copyText(QStringLiteral("sudo udevadm trigger"));
    QClipboard* clipboard = QGuiApplication::clipboard();
    ASSERT_NE(clipboard, nullptr);
    EXPECT_EQ(clipboard->text(), QStringLiteral("sudo udevadm trigger"));
    // Plan 0028 (ADR-0202): the unified toast; none for an empty text.
    EXPECT_EQ(app->toasts().count(QStringLiteral("Copied to clipboard.")), 1);
    app->copyToClipboard(QString());
    EXPECT_EQ(app->toasts().count(QStringLiteral("Copied to clipboard.")), 1);
}

TEST(CheckListModelTest, RowsAndStatuses) {
    CheckListModel model;
    const std::array ids{QStringLiteral("a"), QStringLiteral("b")};
    const std::array titles{QStringLiteral("A"), QStringLiteral("B")};
    model.reset(ids, titles);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(0), CheckListModel::StatusRole), QStringLiteral("pending"));
    QSignalSpy announced(&model, &CheckListModel::resultAnnounced);
    model.update(hwio::diag::CheckResult{.id = "a",
                                         .status = hwio::diag::CheckStatus::Running,
                                         .title = {},
                                         .result = {},
                                         .error = std::nullopt});
    EXPECT_TRUE(model.running());
    EXPECT_EQ(announced.count(), 0);
    model.update(hwio::device::DiagnoseStep{.id = "a",
                                            .status = hwio::diag::CheckStatus::Failed,
                                            .result = "no power",
                                            .error = hwio::makeError(ErrorCode::TargetNoPower)});
    EXPECT_FALSE(model.running());
    EXPECT_TRUE(model.anyFailed());
    EXPECT_EQ(model.data(model.index(0), CheckListModel::TitleRole), QStringLiteral("A"));
    EXPECT_EQ(model.data(model.index(0), CheckListModel::ResultRole), QStringLiteral("no power"));
    const auto error = model.data(model.index(0), CheckListModel::ErrorRole).value<ErrorInfo>();
    EXPECT_EQ(error.code(), QStringLiteral("target.no_power"));
    ASSERT_EQ(announced.count(), 1);
    EXPECT_EQ(announced.at(0).at(0).toString(), QStringLiteral("A: no power"));
    model.update(hwio::diag::CheckResult{.id = "unknown",
                                         .status = hwio::diag::CheckStatus::Ok,
                                         .title = {},
                                         .result = {},
                                         .error = std::nullopt});
    EXPECT_EQ(model.rowCount(), 2);
    model.setAllPending();
    EXPECT_FALSE(model.anyFailed());
    // Bounded: at most diag::maxChecks rows.
    std::vector<QString> many(20, QStringLiteral("x"));
    model.reset(many, {});
    EXPECT_EQ(model.rowCount(), static_cast<int>(hwio::diag::maxChecks));
}

TEST(WizardIllustrations, EverySlotAndEveryGuideStepResolvesToAFile) {
    for (const QString& slot : WizardViewModel::illustrationSlots()) {
        EXPECT_TRUE(WizardViewModel::hasIllustration(slot)) << slot.toStdString();
    }
    for (const auto* profile : hwio::device::ProfileRegistry::all()) {
        for (const auto& target : profile->targets) {
            for (const auto& step : target.connectionGuide) {
                const QString slot =
                    QString::fromUtf8(step.illustrationSlot.data(),
                                      static_cast<qsizetype>(step.illustrationSlot.size()));
                EXPECT_TRUE(WizardViewModel::illustrationSlots().contains(slot))
                    << slot.toStdString();
                EXPECT_TRUE(WizardViewModel::hasIllustration(slot)) << slot.toStdString();
            }
        }
    }
    EXPECT_TRUE(WizardViewModel::illustrationUrl(QStringLiteral("no-such-slot"), false)
                    .toString()
                    .endsWith(QStringLiteral("/placeholder.svg")));
    // No dark variant yet: the light one is used.
    EXPECT_TRUE(WizardViewModel::illustrationUrl(QStringLiteral("jlink-usb"), true)
                    .toString()
                    .endsWith(QStringLiteral("/jlink-usb.svg")));
}

TEST_F(WizardTest, DiagWorkerRunsChecksWithGenerations) {
    hwio::gui::DiagWorker worker;
    hwio::gui::DiagWorker::ChecksRequest request;
    request.context.jlink.option = std::filesystem::path(HWIO_FAKE_JLINK_LIBRARY);
    request.context.instanceRoot = root() / "instances";
    request.context.lockDirectory = root() / "locks";
    request.context.settingsDir = root() / "settings";
    request.context.logDir = root() / "logs";
    request.ids = {"jlink-software", "probes"};
    std::vector<hwio::diag::CheckResult> finished;
    std::uint64_t finishedGeneration = 0;
    int progress = 0;
    QObject::connect(&worker, &hwio::gui::DiagWorker::checkProgress,
                     [&progress](quint64, const hwio::diag::CheckResult&) { ++progress; });
    QObject::connect(&worker, &hwio::gui::DiagWorker::checksFinished,
                     [&](quint64 generation, const std::vector<hwio::diag::CheckResult>& results) {
                         finishedGeneration = generation;
                         finished = results;
                     });
    const std::uint64_t generation = worker.runChecks(request);
    EXPECT_GT(generation, 0U);
    ASSERT_TRUE(waitFor([&] { return finishedGeneration == generation; }));
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[0].id, "jlink-software");
    EXPECT_EQ(finished[0].status, hwio::diag::CheckStatus::Ok);
    EXPECT_EQ(finished[1].result, "2 found: 801000001, 801000002");
    EXPECT_EQ(progress, 4); // Running + result per check
    // A cancelled pending request never finishes.
    const std::uint64_t second = worker.runChecks(request);
    worker.cancel(second);
    const std::uint64_t third = worker.runChecks(request);
    ASSERT_TRUE(waitFor([&] { return finishedGeneration == third; }));
}

// ADR-0394 (plan 0056 D-10): a pending request is replaced only by one of the same requester;
// requester 0 replaces any pending one of the same kind; the queue holds 12.
TEST_F(WizardTest, DiagWorkerReplacesOnlyTheSameRequester) {
    const std::filesystem::path log = root() / "nrfutil.log";
    qputenv("HWIO_FAKE_NRFUTIL_MODE", "slowversion"); // the version probe answers after 30 s
    qputenv("HWIO_FAKE_NRFUTIL_LOG", QByteArray::fromStdString(log.string()));
    hwio::gui::DiagWorker worker;
    std::vector<std::uint64_t> dropped;
    QObject::connect(&worker, &hwio::gui::DiagWorker::requestDropped,
                     [&dropped](quint64 generation) { dropped.push_back(generation); });
    const auto request = [this](std::uint64_t requester) {
        hwio::gui::DiagWorker::ChecksRequest checks;
        checks.context.nrfutil.option = std::filesystem::path(HWIO_FAKE_NRFUTIL);
        checks.context.instanceRoot = root() / "instances";
        checks.context.lockDirectory = root() / "locks";
        checks.context.settingsDir = root() / "settings";
        checks.context.logDir = root() / "logs";
        checks.ids = {"nrfutil"};
        checks.requester = requester;
        return checks;
    };
    const std::uint64_t first = worker.newRequester();
    const std::uint64_t second = worker.newRequester();
    EXPECT_GT(first, 0U);
    EXPECT_NE(first, second);
    // The worker is kept busy by the slow version probe.
    const std::uint64_t blocker = worker.runChecks(request(worker.newRequester()));
    ASSERT_TRUE(waitFor([&log] { return std::filesystem::exists(log); }));

    // Two requesters keep their pending requests; the same requester replaces its own.
    const std::uint64_t a = worker.runChecks(request(first));
    const std::uint64_t b = worker.runChecks(request(second));
    hwio::gui::test::spin(std::chrono::milliseconds(50));
    EXPECT_TRUE(dropped.empty());
    const std::uint64_t a2 = worker.runChecks(request(first));
    ASSERT_TRUE(waitFor([&dropped] { return dropped.size() == 1; }));
    EXPECT_EQ(dropped.front(), a);
    // Requester 0 replaces a pending request of the same kind (the old rule): the oldest one.
    const std::uint64_t anonymous = worker.runChecks(request(0));
    ASSERT_TRUE(waitFor([&dropped] { return dropped.size() == 2; }));
    EXPECT_EQ(dropped.back(), a2);

    // Twelve pending requests fit; the thirteenth drops the oldest pending one (anonymous).
    dropped.clear();
    std::vector<std::uint64_t> pending{b};
    for (int i = 0; i < 10; ++i) { // 2 pending + 10 = 12
        pending.push_back(worker.runChecks(request(worker.newRequester())));
    }
    hwio::gui::test::spin(std::chrono::milliseconds(50));
    EXPECT_TRUE(dropped.empty());
    pending.push_back(worker.runChecks(request(worker.newRequester())));
    ASSERT_TRUE(waitFor([&dropped] { return dropped.size() == 1; }));
    EXPECT_EQ(dropped.front(), anonymous);
    EXPECT_EQ(hwio::gui::DiagWorker::maxQueued, 12U);

    // The pending requests (each would wait for the slow probe) and then the running one.
    for (const std::uint64_t generation : pending) {
        worker.cancel(generation);
    }
    worker.cancel(blocker); // stops the version probe
    qunsetenv("HWIO_FAKE_NRFUTIL_MODE");
    qunsetenv("HWIO_FAKE_NRFUTIL_LOG");
}

// Review V3: a subset of the checks runs with the checks it depends on (transitively), in order.
TEST_F(WizardTest, DiagWorkerAddsTheDependenciesOfRequestedChecks) {
    hwio::gui::DiagWorker worker;
    hwio::gui::DiagWorker::ChecksRequest request;
    request.context.jlink.option = std::filesystem::path(HWIO_FAKE_JLINK_LIBRARY);
    request.context.instanceRoot = root() / "instances";
    request.context.lockDirectory = root() / "locks";
    request.context.settingsDir = root() / "settings";
    request.context.logDir = root() / "logs";
    request.ids = {"probe-firmware"};
    std::vector<hwio::diag::CheckResult> finished;
    std::uint64_t finishedGeneration = 0;
    QObject::connect(&worker, &hwio::gui::DiagWorker::checksFinished,
                     [&](quint64 generation, const std::vector<hwio::diag::CheckResult>& results) {
                         finishedGeneration = generation;
                         finished = results;
                     });
    const std::uint64_t generation = worker.runChecks(request);
    ASSERT_TRUE(waitFor([&] { return finishedGeneration == generation; }));
    ASSERT_EQ(finished.size(), 3U);
    EXPECT_EQ(finished[0].id, "jlink-software");
    EXPECT_EQ(finished[1].id, "probes");
    EXPECT_EQ(finished[2].id, "probe-firmware");
    EXPECT_EQ(finished[2].status, hwio::diag::CheckStatus::Ok);
}

TEST_F(WizardTest, ReportCollectSaveAndCopy) {
    const auto app = makeWizardApp();
    hwio::gui::ReportViewModel& report = *app->report();
    report.show();
    EXPECT_TRUE(report.isOpen());
    EXPECT_EQ(report.state(), QStringLiteral("collecting"));
    EXPECT_TRUE(report.defaultFileName().startsWith(QStringLiteral("hwio-diagnostics-")));
    ASSERT_TRUE(waitFor([&] { return report.state() == QStringLiteral("ready"); },
                        std::chrono::seconds(30)));
    EXPECT_TRUE(report.summary().contains(QStringLiteral("HARDWARIO Monitor diagnostic report")));
    EXPECT_TRUE(report.summary().contains(QStringLiteral("801000001")));

    report.copySummary();
    EXPECT_EQ(QGuiApplication::clipboard()->text(), report.summary());
    EXPECT_TRUE(app->toasts().contains(QStringLiteral("Copied to clipboard."))); // ADR-0202

    // Plan 0028 (D13): a location that is not on this computer is refused with an error.
    report.save(QUrl(QStringLiteral("smb://server/share/report.zip")));
    EXPECT_EQ(report.state(), QStringLiteral("error"));
    EXPECT_EQ(report.error().code(), QStringLiteral("io.write_failed"));
    EXPECT_EQ(report.error().cause(), QStringLiteral("Only folders on this computer can be used."));

    const QString target = QString::fromStdU16String((root() / "report.zip").u16string());
    report.save(QUrl::fromLocalFile(target));
    EXPECT_EQ(report.state(), QStringLiteral("saving"));
    ASSERT_TRUE(waitFor([&] { return report.state() == QStringLiteral("saved"); }));
    EXPECT_TRUE(QFile::exists(target));
    EXPECT_TRUE(app->toasts().contains(QStringLiteral("Diagnostic report saved.")));

    // A folder that does not exist: diag.report_write_failed in the dialog.
    report.save(
        QUrl::fromLocalFile(QString::fromStdU16String((root() / "missing" / "r.zip").u16string())));
    ASSERT_TRUE(waitFor([&] { return report.state() == QStringLiteral("error"); }));
    EXPECT_EQ(report.error().code(), QStringLiteral("diag.report_write_failed"));
    report.close();
    EXPECT_FALSE(report.isOpen());
}

TEST_F(WizardTest, ReportCancelWhileCollecting) {
    const auto app = makeWizardApp();
    hwio::gui::ReportViewModel& report = *app->report();
    report.show();
    report.close();
    EXPECT_FALSE(report.isOpen());
    hwio::gui::test::spin(std::chrono::milliseconds(300));
    EXPECT_FALSE(report.isOpen());
    // It can be opened again.
    report.show();
    ASSERT_TRUE(waitFor([&] { return report.state() == QStringLiteral("ready"); },
                        std::chrono::seconds(30)));
}

} // namespace
