// The setup wizard (plan/features/0008 as amended by 0035): step sequences per platform, start
// step per error code, Skip / Done / "Don't show at start" keys, read-only settings, path
// validation, the USB step's uaccess commands, the read-only probe step (ADR-0290), the firmware
// actions and the Done summary (ADR-0291), and the start-up decision (ADR-0071, StartupCheck).

#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/settings.h"
#include "hwio/diag/check.h"
#include "hwio/diag/environment.h"
#include "hwio/diag/usb_access.h"
#include "settings_store.h"
#include "startup_check.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/probe_firmware_view_model.h"
#include "viewmodels/probe_picker_view_model.h"
#include "wizard/check_list_model.h"
#include "wizard/setup_wizard.h"
#include "wizard/wizard.h"
#include "wizard/wizard_launcher.h"
#include "wizard/wizard_view_model.h"

#include <QFile>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hwio::ErrorCode;
using hwio::gui::AppViewModel;
using hwio::gui::ErrorInfo;
using hwio::gui::SetupWizard;
using hwio::gui::StartupCheck;
using hwio::gui::StepKind;
using hwio::gui::WizardViewModel;
using hwio::gui::test::waitFor;
using Platform = SetupWizard::Platform;
using Decision = StartupCheck::Decision;

// One probe with outdated firmware (as the plan 0036 tests).
constexpr const char* outdatedScenario = R"({"probes": [{"serialNumber": 801000001,
    "product": "Test Probe", "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
    "libraryFirmware": "J-Link V11 compiled Oct  1 2024 09:00:00", "updateResult": "updated",
    "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}]}}]})";

// The property a control gets through WizardStep::embeddedOptions (ADR-0290).
class OptionTarget : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool readOnly MEMBER m_readOnly)

public:
    using QObject::QObject;
    [[nodiscard]] bool readOnly() const noexcept { return m_readOnly; }

private:
    bool m_readOnly = false;
};

class SetupWizardTest : public hwio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        m_app = makeApp();
    }
    void TearDown() override {
        m_app.reset();
        GuiTest::TearDown();
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] WizardViewModel& wizard() { return *m_app->wizard(); }
    [[nodiscard]] SetupWizard* setup() { return qobject_cast<SetupWizard*>(wizard().wizard()); }
    [[nodiscard]] QString stepId() { return setup() != nullptr ? setup()->stepId() : QString(); }
    void recreateApp() {
        m_app.reset();
        m_app = makeApp();
    }

    // The checks of this machine without its USB devices and processes: a temporary sysfs tree.
    [[nodiscard]] SetupWizard::Options options(Platform platform) {
        const fs::path usbRoot = root() / "usb";
        return SetupWizard::Options{.platform = platform,
                                    .mode = SetupWizard::Mode::Normal,
                                    .environment = [this, usbRoot] {
                                        hwio::diag::EnvironmentContext context =
                                            m_app->diagEnvironment();
                                        context.usbSysfsRoot = usbRoot / "sys";
                                        context.usbDevRoot = usbRoot / "dev";
                                        context.lockDirectory = root() / "locks";
                                        return context;
                                    }};
    }

    // Opens a SetupWizard with these options (as StartupCheck does).
    bool open(SetupWizard::Options opts, const ErrorInfo& context = ErrorInfo()) {
        // Qt parent ownership: WizardViewModel::start takes it.
        auto* created =
            new SetupWizard(*m_app, std::move(opts), // NOLINT(cppcoreguidelines-owning-memory)
                            m_app->wizard());
        return wizard().start(created, context, nullptr);
    }

    [[nodiscard]] bool checksDone() { return setup() != nullptr && !setup()->running(); }

    void addUsbDevice(bool accessible, bool world) const {
        const fs::path usbRoot = root() / "usb";
        const fs::path dir = usbRoot / "sys/bus/usb/devices/1-2";
        fs::create_directories(dir);
        std::ofstream(dir / "idVendor") << "1366\n";
        std::ofstream(dir / "busnum") << "1\n";
        std::ofstream(dir / "devnum") << "5\n";
        const fs::path node = usbRoot / "dev/bus/usb/001/005";
        fs::create_directories(node.parent_path());
        std::ofstream(node) << "";
        fs::permissions(node, accessible ? fs::perms::owner_read | fs::perms::owner_write
                                         : fs::perms::none);
        if (world) {
            fs::permissions(node, fs::perms::others_read | fs::perms::others_write,
                            fs::perm_options::add);
        }
    }

    [[nodiscard]] QStringList stepIds() {
        QStringList ids;
        for (const QVariant& step : wizard().steps()) {
            ids.append(step.toMap().value(QStringLiteral("id")).toString());
        }
        return ids;
    }

    [[nodiscard]] QVariantList actions() {
        return wizard().step().value(QStringLiteral("actions")).toList();
    }

    [[nodiscard]] QString setting(const char* key) {
        return m_app->settingsStore()->value(QString::fromLatin1(key)).toString();
    }

    // Through the steps to `id` (Next / Continue anyway / Skip, waiting for each step's checks).
    void walkTo(const QString& id) {
        for (int i = 0; i < 8 && stepId() != id; ++i) {
            ASSERT_TRUE(waitFor([&] { return checksDone(); }));
            if (stepId() == QStringLiteral("jlink") && !wizard()
                                                            .step()
                                                            .value(QStringLiteral("primaryText"))
                                                            .toString()
                                                            .startsWith(QStringLiteral("Next"))) {
                wizard().action(QStringLiteral("continue-anyway"));
            } else if (stepId() == QStringLiteral("usb") &&
                       setup()->checks()->status(QStringLiteral("usb-access")) !=
                           hwio::diag::CheckStatus::Ok &&
                       setup()->checks()->status(QStringLiteral("usb-access")) !=
                           hwio::diag::CheckStatus::Skipped) {
                wizard().action(QStringLiteral("next"));
            } else {
                wizard().primary();
            }
        }
        ASSERT_EQ(stepId(), id);
    }

private:
    std::unique_ptr<AppViewModel> m_app;
};

TEST_F(SetupWizardTest, RegisteredWithTheLauncherAndHelpMenu) {
    EXPECT_TRUE(app().launcher()->canOpen(QStringLiteral("first-run-setup")));
    ASSERT_TRUE(app().launcher()->start(QStringLiteral("first-run-setup")));
    ASSERT_NE(setup(), nullptr);
    EXPECT_EQ(wizard().tab(), nullptr); // global
    EXPECT_EQ(stepId(), QStringLiteral("welcome"));
    EXPECT_EQ(wizard().step().value(QStringLiteral("kind")).toInt(), StepKind::Info);
    EXPECT_EQ(wizard().step().value(QStringLiteral("heading")).toString(),
              QStringLiteral("Welcome to HARDWARIO Monitor"));
    EXPECT_EQ(wizard().embeddedSource(),
              QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/wizard/SetupStep.qml")));
    EXPECT_FALSE(setup()->showAtStartChoice());
}

TEST_F(SetupWizardTest, StepSequencePerPlatform) {
    ASSERT_TRUE(open(options(Platform::Linux)));
    EXPECT_EQ(stepIds(), (QStringList{QStringLiteral("welcome"), QStringLiteral("jlink"),
                                      QStringLiteral("usb"), QStringLiteral("nrfutil"),
                                      QStringLiteral("probe"), QStringLiteral("done")}));
    wizard().cancel();
    ASSERT_TRUE(waitFor([&] { return !wizard().isOpen(); }));
    for (const Platform platform : {Platform::MacOS, Platform::Windows}) {
        ASSERT_TRUE(open(options(platform)));
        EXPECT_EQ(stepIds(), (QStringList{QStringLiteral("welcome"), QStringLiteral("jlink"),
                                          QStringLiteral("nrfutil"), QStringLiteral("probe"),
                                          QStringLiteral("done")}));
        wizard().cancel();
        ASSERT_TRUE(waitFor([&] { return !wizard().isOpen(); }));
    }
}

TEST_F(SetupWizardTest, StartStepPerErrorCode) {
    EXPECT_EQ(SetupWizard::startStepFor(ErrorCode::DependencyJLinkNotFound),
              QStringLiteral("jlink"));
    EXPECT_EQ(SetupWizard::startStepFor(ErrorCode::DependencyJLinkLoadFailed),
              QStringLiteral("jlink"));
    EXPECT_EQ(SetupWizard::startStepFor(ErrorCode::DependencyJLinkTooOld), QStringLiteral("jlink"));
    EXPECT_EQ(SetupWizard::startStepFor(ErrorCode::UsbAccessDenied), QStringLiteral("usb"));
    EXPECT_EQ(SetupWizard::startStepFor(ErrorCode::UsbWorldAccessible), QStringLiteral("usb"));
    EXPECT_EQ(SetupWizard::startStepFor(ErrorCode::DependencyNrfUtilNotFound),
              QStringLiteral("nrfutil"));
    EXPECT_EQ(SetupWizard::startStepFor(ErrorCode::DependencyNrfUtilDeviceMissing),
              QStringLiteral("nrfutil"));
    EXPECT_EQ(SetupWizard::startStepFor(ErrorCode::DependencyNrfUtilTooOld),
              QStringLiteral("nrfutil"));
    EXPECT_TRUE(SetupWizard::startStepFor(ErrorCode::DependencyJLinkFunctionMissing).isEmpty());
    EXPECT_TRUE(SetupWizard::startStepFor(ErrorCode::BlePermissionDenied).isEmpty()); // ADR-0250

    // From an error: no Welcome, the step of the code; Done has [Close] only.
    ASSERT_TRUE(
        open(options(Platform::Linux), ErrorInfo(hwio::makeError(ErrorCode::UsbAccessDenied))));
    EXPECT_TRUE(setup()->startedFromError());
    EXPECT_EQ(stepId(), QStringLiteral("usb"));
    EXPECT_FALSE(stepIds().contains(QStringLiteral("welcome")));
    walkTo(QStringLiteral("done"));
    EXPECT_TRUE(actions().isEmpty());
    wizard().cancel();
    ASSERT_TRUE(waitFor([&] { return !wizard().isOpen(); }));

    // usb.* where there is no USB step: the first step.
    ASSERT_TRUE(open(options(Platform::Windows),
                     ErrorInfo(hwio::makeError(ErrorCode::UsbWorldAccessible))));
    EXPECT_EQ(stepId(), QStringLiteral("jlink"));
    wizard().cancel();
    ASSERT_TRUE(waitFor([&] { return !wizard().isOpen(); }));

    ASSERT_TRUE(
        app().launcher()->open(QStringLiteral("first-run-setup"),
                               ErrorInfo(hwio::makeError(ErrorCode::DependencyNrfUtilTooOld))));
    EXPECT_EQ(stepId(), QStringLiteral("nrfutil"));
}

TEST_F(SetupWizardTest, SkipSetupWritesTheCompletedVersion) {
    EXPECT_TRUE(setting("setup/completedVersion").isEmpty());
    ASSERT_TRUE(open(options(Platform::Linux)));
    const QVariantList welcomeActions = actions();
    ASSERT_EQ(welcomeActions.size(), 1);
    EXPECT_EQ(welcomeActions.at(0).toMap().value(QStringLiteral("text")).toString(),
              QStringLiteral("Skip setup"));
    wizard().action(QStringLiteral("skip-setup"));
    EXPECT_FALSE(wizard().isOpen());
    EXPECT_EQ(setting("setup/completedVersion"), AppViewModel::version());
}

TEST_F(SetupWizardTest, DoneWritesTheCompletedVersionAndSummarises) {
    ASSERT_TRUE(open(options(Platform::Linux)));
    walkTo(QStringLiteral("done"));
    EXPECT_EQ(setting("setup/completedVersion"), AppViewModel::version());
    EXPECT_EQ(wizard().step().value(QStringLiteral("kind")).toInt(), StepKind::Done);
    EXPECT_FALSE(wizard().canBack());
    // Plan 0009: the Connect wizard is registered with the built-in pages: [Connect a device].
    ASSERT_EQ(actions().size(), 1);
    EXPECT_EQ(actions().at(0).toMap().value(QStringLiteral("id")).toString(),
              QStringLiteral("connect-device"));
    // ADR-0291: every serial number the `probes` check found.
    const hwio::gui::CheckListModel& summary = *setup()->summary();
    ASSERT_EQ(summary.count(), 4);
    const hwio::diag::CheckResult* jlink = summary.row(QStringLiteral("jlink"));
    ASSERT_NE(jlink, nullptr);
    EXPECT_EQ(jlink->status, hwio::diag::CheckStatus::Ok);
    EXPECT_EQ(jlink->result, "8.10");
    const hwio::diag::CheckResult* probes = summary.row(QStringLiteral("probe"));
    ASSERT_NE(probes, nullptr);
    EXPECT_EQ(probes->status, hwio::diag::CheckStatus::Ok);
    EXPECT_EQ(probes->result, "801000001, 801000002");
    const hwio::diag::CheckResult* usb = summary.row(QStringLiteral("usb"));
    ASSERT_NE(usb, nullptr);
    EXPECT_EQ(usb->status, hwio::diag::CheckStatus::Skipped); // no J-Link in the sysfs tree
    wizard().primary();
    EXPECT_FALSE(wizard().isOpen());
}

TEST_F(SetupWizardTest, ConnectDeviceHandsOffToTheConnectWizard) {
    recreateApp(); // the Connect wizard of plan 0009 is registered with the built-in pages
    ASSERT_TRUE(open(options(Platform::MacOS)));
    walkTo(QStringLiteral("done"));
    const QVariantList done = actions();
    ASSERT_EQ(done.size(), 1);
    EXPECT_EQ(done.at(0).toMap().value(QStringLiteral("text")).toString(),
              QStringLiteral("Connect a device"));
    wizard().action(QStringLiteral("connect-device"));
    ASSERT_TRUE(waitFor([&] { return wizard().wizardId() == QStringLiteral("connect-device"); }));
    EXPECT_EQ(wizard().tab(), app().currentTabViewModel());
}

TEST_F(SetupWizardTest, DontShowAtStartAndLanguage) {
    SetupWizard::Options opts = options(Platform::Linux);
    opts.mode = SetupWizard::Mode::JLinkNeeded;
    ASSERT_TRUE(open(std::move(opts)));
    EXPECT_EQ(wizard().step().value(QStringLiteral("heading")).toString(),
              QStringLiteral("J-Link Software is needed"));
    EXPECT_TRUE(setup()->showAtStartChoice());
    EXPECT_TRUE(setup()->showAtStart());
    setup()->setShowAtStart(false);
    EXPECT_FALSE(app().settingsStore()->value(QStringLiteral("setup/showAtStart")).toBool());

    // ADR-0254: System default + the five languages; the current one selected.
    const QVariantList languages = setup()->languages();
    ASSERT_EQ(languages.size(), 6);
    EXPECT_EQ(languages.at(0).toMap().value(QStringLiteral("name")).toString(),
              QStringLiteral("System default"));
    EXPECT_EQ(languages.at(0).toMap().value(QStringLiteral("code")).toString(),
              QStringLiteral("system"));
    EXPECT_EQ(setup()->languageIndex(), 0);
}

TEST_F(SetupWizardTest, ReadOnlySettingsSaveNothing) {
    recreateApp();
    {
        QSettings raw(QString::fromStdU16String(settingsFile().u16string()), QSettings::IniFormat);
        raw.setValue(QStringLiteral("meta/schemaVersion"), hwio::currentSettingsSchema + 1);
        raw.sync();
    }
    recreateApp();
    ASSERT_TRUE(app().settingsStore()->readOnly());
    ASSERT_TRUE(open(options(Platform::Linux)));
    EXPECT_TRUE(setup()->settingsReadOnly());
    walkTo(QStringLiteral("nrfutil"));
    EXPECT_TRUE(setup()->buttons().isEmpty()); // no "It is installed elsewhere…"
    walkTo(QStringLiteral("done"));
    EXPECT_TRUE(setting("setup/completedVersion").isEmpty());

    // The start-up check does not open the wizard either.
    wizard().cancel();
    StartupCheck check(app(), nullptr);
    check.run();
    EXPECT_EQ(check.decision(), std::optional<Decision>(Decision::None));
}

TEST_F(SetupWizardTest, JLinkPathIsSavedOnlyWhenTheCheckPasses) {
    qunsetenv("HWIO_JLINK_LIBRARY"); // only the setting and the chosen path count
    SetupWizard::Options opts = options(Platform::Linux);
    auto base = opts.environment;
    opts.environment = [base] {
        hwio::diag::EnvironmentContext context = base();
        context.jlink.environment.reset();
        context.jlink.searchRoots.clear();
        context.jlink.installPaths.clear();
        return context;
    };
    ASSERT_TRUE(open(std::move(opts)));
    walkTo(QStringLiteral("jlink"));
    ASSERT_TRUE(waitFor([&] { return checksDone(); }));
    EXPECT_EQ(setup()->checks()->status(QStringLiteral("jlink-software")),
              hwio::diag::CheckStatus::Failed);
    EXPECT_EQ(wizard().step().value(QStringLiteral("primaryText")).toString(),
              QStringLiteral("Check again"));
    EXPECT_EQ(wizard().step().value(QStringLiteral("linkUrl")).toString(),
              SetupWizard::jlinkDownloadUrl);
    ASSERT_EQ(setup()->buttons().size(), 1);
    EXPECT_EQ(setup()->buttons().at(0).toMap().value(QStringLiteral("text")).toString(),
              QStringLiteral("It is installed elsewhere…"));
    EXPECT_TRUE(setup()->fileNameFilters().at(0).contains(QStringLiteral("libjlinkarm.so*")));

    // Not a J-Link library: the row shows the error, nothing is saved.
    const QString bad = QString::fromStdU16String((root() / "not-a-library.so").u16string());
    {
        QFile file(bad);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("nonsense");
    }
    setup()->choosePath(QUrl::fromLocalFile(bad));
    ASSERT_TRUE(waitFor([&] { return checksDone(); }));
    EXPECT_EQ(setup()->checks()->status(QStringLiteral("jlink-software")),
              hwio::diag::CheckStatus::Failed);
    EXPECT_TRUE(setting("jlink/libraryPath").isEmpty());

    // The fake library: saved, the check passes with the setting.
    const QString good = QStringLiteral(HWIO_FAKE_JLINK_LIBRARY);
    setup()->choosePath(QUrl::fromLocalFile(good));
    ASSERT_TRUE(waitFor([&] {
        return checksDone() && setting("jlink/libraryPath") == good &&
               setup()->checks()->status(QStringLiteral("jlink-software")) ==
                   hwio::diag::CheckStatus::Ok;
    }));
    EXPECT_EQ(wizard().step().value(QStringLiteral("primaryText")).toString(),
              QStringLiteral("Next"));
    EXPECT_TRUE(setup()->statusText().startsWith(QStringLiteral("J-Link Software 8.10 found in ")))
        << setup()->statusText().toStdString();
    EXPECT_TRUE(setup()->buttons().isEmpty());
}

#ifdef Q_OS_LINUX
TEST_F(SetupWizardTest, UsbStepShowsTheUaccessRule) {
    addUsbDevice(false, false);
    ASSERT_TRUE(open(options(Platform::Linux)));
    walkTo(QStringLiteral("usb"));
    ASSERT_TRUE(waitFor([&] { return checksDone(); }));
    if (setup()->checks()->status(QStringLiteral("usb-access")) == hwio::diag::CheckStatus::Ok) {
        GTEST_SKIP() << "running as root: every device node is accessible";
    }
    EXPECT_EQ(setup()->checks()->status(QStringLiteral("usb-access")),
              hwio::diag::CheckStatus::Failed);
    QStringList expected;
    for (const std::string& command : hwio::diag::jlinkUdevCommands()) {
        expected.append(hwio::toQString(command));
    }
    EXPECT_EQ(setup()->commands(), expected);
    EXPECT_TRUE(setup()->commands().at(0).contains(QStringLiteral("TAG+=\"uaccess\"")));
    EXPECT_FALSE(setup()->commands().join(QString()).contains(QStringLiteral("MODE=\"0666\"")));
    EXPECT_EQ(wizard().step().value(QStringLiteral("primaryText")).toString(),
              QStringLiteral("Check again"));

    // usb.world_accessible (ADR-0185 amendment): the same rule.
    wizard().cancel();
    ASSERT_TRUE(waitFor([&] { return !wizard().isOpen(); }));
    addUsbDevice(true, true);
    ASSERT_TRUE(
        open(options(Platform::Linux), ErrorInfo(hwio::makeError(ErrorCode::UsbWorldAccessible))));
    EXPECT_EQ(stepId(), QStringLiteral("usb"));
    ASSERT_TRUE(waitFor([&] { return checksDone(); }));
    EXPECT_EQ(setup()->checks()->status(QStringLiteral("usb-access")),
              hwio::diag::CheckStatus::Warning);
    EXPECT_EQ(setup()->commands(), expected);
    wizard().action(QStringLiteral("next"));
    EXPECT_EQ(stepId(), QStringLiteral("nrfutil"));
}
#endif

// ADR-0290: the probe step is read-only through embeddedOptions.
TEST_F(SetupWizardTest, ProbeStepIsReadOnlyThroughEmbeddedOptions) {
    ASSERT_TRUE(open(options(Platform::Linux)));
    walkTo(QStringLiteral("probe"));
    const QVariantMap step = wizard().step();
    EXPECT_EQ(step.value(QStringLiteral("kind")).toInt(), StepKind::Embedded);
    EXPECT_EQ(step.value(QStringLiteral("embedded")).toString(), QStringLiteral("probe-picker"));
    EXPECT_EQ(step.value(QStringLiteral("embeddedOptions")).toMap(),
              (QVariantMap{{QStringLiteral("readOnly"), true}}));
    ASSERT_NE(setup()->picker(), nullptr);
    EXPECT_EQ(wizard().embedded(), setup()->picker());
    EXPECT_TRUE(setup()->picker()->active());
    EXPECT_TRUE(setup()->picker()->wizardMode());
    EXPECT_TRUE(wizard().canPrimary()); // [Next] always

    // A control without the property: ignored (a warning in the log), nothing breaks.
    QObject plain;
    wizard().applyEmbeddedOptions(&plain);
    wizard().applyEmbeddedOptions(nullptr);

    OptionTarget target;
    wizard().applyEmbeddedOptions(&target);
    EXPECT_TRUE(target.readOnly());
    wizard().applyEmbeddedOptions(&target); // again (every change of the wizard): unchanged
    EXPECT_TRUE(target.readOnly());

    // A later step without the option: the control's own value comes back.
    walkTo(QStringLiteral("done"));
    EXPECT_FALSE(setup()->picker()->active());
    wizard().applyEmbeddedOptions(&target);
    EXPECT_FALSE(target.readOnly());
}

// ADR-0291: one firmware action per outdated probe; disabled with the reason while a tab uses it.
TEST_F(SetupWizardTest, FirmwareActionPerOutdatedProbe) {
    useScenario(outdatedScenario);
    recreateApp();
    ASSERT_TRUE(open(options(Platform::Linux)));
    walkTo(QStringLiteral("probe"));
    ASSERT_TRUE(waitFor([&] {
        return checksDone() && setup()->checks()->status(QStringLiteral("probe-firmware")) ==
                                   hwio::diag::CheckStatus::Warning;
    }));
    QVariantList list = actions();
    ASSERT_EQ(list.size(), 1);
    QVariantMap action = list.at(0).toMap();
    EXPECT_EQ(action.value(QStringLiteral("id")).toString(),
              QStringLiteral("update-firmware:801000001"));
    EXPECT_EQ(action.value(QStringLiteral("text")).toString(),
              QStringLiteral("Update J-Link firmware 801000001…"));
    EXPECT_TRUE(action.value(QStringLiteral("enabled")).toBool());

    // A tab connects to the probe: "Disconnect tab 1 first." instead.
    hwio::gui::DeviceTabViewModel& tab = *app().currentTabViewModel();
    ASSERT_TRUE(tab.configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001,
                              QStringLiteral("Test Probe")));
    tab.connectDevice();
    ASSERT_TRUE(waitFor([&] {
        const QVariantList now = actions();
        return !now.isEmpty() && !now.at(0).toMap().value(QStringLiteral("enabled")).toBool();
    }));
    action = actions().at(0).toMap();
    EXPECT_EQ(action.value(QStringLiteral("text")).toString(),
              QStringLiteral("Disconnect tab 1 first."));
    wizard().action(QStringLiteral("update-firmware:801000001"));
    EXPECT_EQ(app().probeFirmware()->state(), hwio::gui::ProbeFirmwareViewModel::Idle);

    tab.disconnectDevice();
    ASSERT_TRUE(waitFor([&] {
        const QVariantList now = actions();
        return !now.isEmpty() && now.at(0).toMap().value(QStringLiteral("enabled")).toBool();
    }));
    wizard().action(QStringLiteral("update-firmware:801000001"));
    EXPECT_NE(app().probeFirmware()->state(), hwio::gui::ProbeFirmwareViewModel::Idle);
    app().probeFirmware()->cancel();
}

// ---- start-up (ADR-0071) -----------------------------------------------------------------------

TEST(StartupDecision, Table) {
    EXPECT_EQ(StartupCheck::decide(false, true, true), Decision::Welcome);
    EXPECT_EQ(StartupCheck::decide(false, false, false), Decision::Welcome);
    EXPECT_EQ(StartupCheck::decide(true, true, true), Decision::None);
    EXPECT_EQ(StartupCheck::decide(true, false, true), Decision::JLinkNeeded);
    EXPECT_EQ(StartupCheck::decide(true, false, false), Decision::None);
    EXPECT_EQ(StartupCheck::decide(true, true, false), Decision::None);
}

class StartupCheckTest : public SetupWizardTest {
protected:
    Decision runCheck() {
        StartupCheck check(app(), nullptr);
        check.run();
        EXPECT_TRUE(waitFor([&] { return check.decision().has_value(); }));
        return check.decision().value_or(Decision::None);
    }
    void complete() {
        ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("setup/completedVersion"),
                                                    QStringLiteral("2.0.0")));
    }
    void closeWizard() {
        wizard().cancel();
        ASSERT_TRUE(waitFor([&] { return !wizard().isOpen(); }));
    }
};

TEST_F(StartupCheckTest, FirstStartOpensWelcome) {
    EXPECT_EQ(runCheck(), Decision::Welcome);
    ASSERT_NE(setup(), nullptr);
    EXPECT_EQ(stepId(), QStringLiteral("welcome"));
    EXPECT_EQ(wizard().step().value(QStringLiteral("heading")).toString(),
              QStringLiteral("Welcome to HARDWARIO Monitor"));
}

TEST_F(StartupCheckTest, CompletedWithJLinkOpensNothing) {
    complete();
    EXPECT_EQ(runCheck(), Decision::None);
    EXPECT_FALSE(wizard().isOpen());
}

TEST_F(StartupCheckTest, CompletedWithoutJLinkOpensTheNeededVariantUntilDontShow) {
    complete();
    qputenv("HWIO_JLINK_LIBRARY", QByteArray("/nonexistent/libjlinkarm.so"));
    EXPECT_EQ(runCheck(), Decision::JLinkNeeded);
    ASSERT_NE(setup(), nullptr);
    EXPECT_EQ(wizard().step().value(QStringLiteral("heading")).toString(),
              QStringLiteral("J-Link Software is needed"));
    EXPECT_TRUE(setup()->showAtStartChoice());
    setup()->setShowAtStart(false);
    closeWizard();
    EXPECT_EQ(runCheck(), Decision::None);
    EXPECT_FALSE(wizard().isOpen());
}

TEST_F(StartupCheckTest, WaitsForAnotherWizard) {
    recreateApp(); // the Connect wizard of plan 0009
    ASSERT_TRUE(app().launcher()->start(QStringLiteral("connect-device")));
    StartupCheck check(app(), nullptr);
    check.run();
    hwio::gui::test::spin(std::chrono::milliseconds(500));
    EXPECT_EQ(wizard().wizardId(), QStringLiteral("connect-device"));
    EXPECT_FALSE(check.decision().has_value());
    closeWizard();
    ASSERT_TRUE(waitFor([&] { return check.decision().has_value(); }));
    EXPECT_EQ(check.decision(), std::optional<Decision>(Decision::Welcome));
    EXPECT_EQ(wizard().wizardId(), QStringLiteral("first-run-setup"));
}

} // namespace

#include "setup_wizard_test.moc"
