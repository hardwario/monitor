// SettingsViewModel and DependencyStatus (plan/features/0006-m3-flash-settings.md, E1): the
// sections, live theme, validated writes, the default J-Link speed with "Automatic speed"
// (ADR-0111, ADR-0110 warning), the custom J-Link / nRF Util paths (checked before they are saved),
// dependency detection results and the read-only mode (settings.newer_schema).

#include "gui_test_support.h"
#include "hwio/core/settings.h"
#include "pages/firmware_services.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/dependency_status.h"
#include "viewmodels/settings_view_model.h"
#include "viewmodels/ui_mode.h"
#include "viewmodels/ui_mode_controller.h"

#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace {

using hwio::gui::AppViewModel;
using hwio::gui::DependencyStatus;
using hwio::gui::FirmwareServices;
using hwio::gui::SettingsViewModel;
using hwio::gui::UiMode;
using hwio::gui::test::waitFor;

class SettingsTest : public hwio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        qputenv("HWIO_NRFUTIL", (root() / "no-nrfutil" / "nrfutil").string().c_str());
        m_app = makeApp();
    }
    void TearDown() override {
        m_app.reset();
        qunsetenv("HWIO_NRFUTIL");
        GuiTest::TearDown();
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] SettingsViewModel& settings() {
        return *qobject_cast<SettingsViewModel*>(
            m_app->globalPageViewModel(SettingsViewModel::pageId));
    }
    void restart() { restartWith({}); }
    // Closes the app, lets `edit` change the settings file directly and starts the app again.
    void restartWith(const std::function<void()>& edit) {
        m_app.reset();
        if (edit) {
            edit();
        }
        m_app = makeApp();
    }

private:
    std::unique_ptr<AppViewModel> m_app;
};

[[nodiscard]] QStringList sectionIds(const SettingsViewModel& settings) {
    QStringList ids;
    for (const QVariant& section : settings.sections()) {
        ids.append(section.toMap().value(QStringLiteral("sectionId")).toString());
    }
    return ids;
}

TEST_F(SettingsTest, SectionsInRegistryOrder) {
    // Console (order 20) is contributed by plan 0005, AI assistants (50) by plan 0014, Expert
    // (80) by plan 0033 (renamed by plan 0045).
    ASSERT_TRUE(app().uiMode()->choose(UiMode::Expert));
    EXPECT_EQ(
        sectionIds(settings()),
        (QStringList{QStringLiteral("general"), QStringLiteral("console"), QStringLiteral("jlink"),
                     QStringLiteral("nrfutil"), QStringLiteral("mcp"), QStringLiteral("expert"),
                     QStringLiteral("about")}));
    EXPECT_EQ(settings().sections().at(2).toMap().value(QStringLiteral("title")).toString(),
              QStringLiteral("J-Link"));
    EXPECT_EQ(settings().sections().at(5).toMap().value(QStringLiteral("title")).toString(),
              QStringLiteral("Expert"));
}

// Plan 0045 (ADR-0359): the sections each mode shows; the first start shows Advanced.
TEST_F(SettingsTest, SectionsPerMode) {
    EXPECT_FALSE(app().uiMode()->chosen());
    EXPECT_EQ(settings().mode(), static_cast<int>(UiMode::Advanced));
    const QStringList advanced{QStringLiteral("general"), QStringLiteral("console"),
                               QStringLiteral("jlink"),   QStringLiteral("nrfutil"),
                               QStringLiteral("mcp"),     QStringLiteral("about")};
    EXPECT_EQ(sectionIds(settings()), advanced);

    QSignalSpy sections(&settings(), &SettingsViewModel::sectionsChanged);
    QSignalSpy mode(&settings(), &SettingsViewModel::modeChanged);
    settings().selectMode(static_cast<int>(UiMode::Basic));
    EXPECT_EQ(sectionIds(settings()),
              (QStringList{QStringLiteral("general"), QStringLiteral("console"),
                           QStringLiteral("about")}));
    EXPECT_EQ(settings().modeDescription(),
              QStringLiteral("The essentials: language, look and console files."));
    EXPECT_EQ(sections.count(), 1);
    EXPECT_EQ(mode.count(), 1);
    EXPECT_TRUE(app().uiMode()->chosen());
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("ui/mode")).toString(),
              QStringLiteral("basic"));
    EXPECT_FALSE(settings().shows(UiMode::Advanced));

    settings().selectMode(static_cast<int>(UiMode::Advanced));
    EXPECT_EQ(sectionIds(settings()), advanced);
    settings().selectMode(42); // ignored
    EXPECT_EQ(settings().mode(), static_cast<int>(UiMode::Advanced));
    EXPECT_EQ(SettingsViewModel::modes().size(), 3);
}

// ADR-0361: an error or guide that opens a hidden section shows it until Settings is left.
TEST_F(SettingsTest, HiddenSectionIsRevealedForOneVisit) {
    ASSERT_TRUE(app().uiMode()->choose(UiMode::Basic));
    app().openSettings(QStringLiteral("jlink"));
    EXPECT_EQ(settings().revealedSection(), QStringLiteral("jlink"));
    ASSERT_TRUE(sectionIds(settings()).contains(QStringLiteral("jlink")));
    const QVariantMap jlink = settings().sections().at(2).toMap();
    EXPECT_EQ(jlink.value(QStringLiteral("sectionId")).toString(), QStringLiteral("jlink"));
    EXPECT_TRUE(jlink.value(QStringLiteral("revealed")).toBool());
    EXPECT_EQ(jlink.value(QStringLiteral("minimumMode")).toInt(),
              static_cast<int>(UiMode::Advanced));
    EXPECT_FALSE(settings().sections().at(0).toMap().value(QStringLiteral("revealed")).toBool());
    EXPECT_EQ(app().uiMode()->mode(), UiMode::Basic); // the mode is unchanged

    // Leaving Settings (another tab) hides it again.
    app().setCurrentTab(0);
    EXPECT_EQ(settings().revealedSection(), QString());
    EXPECT_FALSE(sectionIds(settings()).contains(QStringLiteral("jlink")));

    // A row: remembered for its SettingsRow; closing Settings clears it.
    app().openSettings(QStringLiteral("expert"), QStringLiteral("relayUrl"));
    EXPECT_EQ(settings().revealedSection(), QStringLiteral("expert"));
    EXPECT_EQ(settings().revealedRow(), QStringLiteral("relayUrl"));
    app().closeSettings();
    EXPECT_EQ(settings().revealedSection(), QString());
    EXPECT_EQ(settings().revealedRow(), QString());

    // A section the mode shows is not "revealed".
    app().openSettings(QStringLiteral("console"));
    EXPECT_EQ(settings().revealedSection(), QString());
    ASSERT_TRUE(app().uiMode()->choose(UiMode::Advanced));
    app().openSettings(QStringLiteral("nrfutil"));
    EXPECT_EQ(settings().revealedSection(), QString());
}

// Plan 0044 (ADR-0330): Settings → Advanced → Support server: https only, "" = the default server;
// the Join dialog compares a link's server with it.
TEST_F(SettingsTest, SupportServerIsValidatedAndUsedByTheJoinDialog) {
    EXPECT_EQ(settings().relayUrl(), QString());
    EXPECT_EQ(app().relayUrl().toString(), QStringLiteral("https://terminal.hardwario.com"));
    EXPECT_FALSE(settings().setRelayUrl(QStringLiteral("http://example.com")).isEmpty());
    EXPECT_FALSE(settings().setRelayUrl(QStringLiteral("example.com")).isEmpty());
    EXPECT_EQ(settings().relayUrl(), QString());
    EXPECT_TRUE(settings().setRelayUrl(QStringLiteral("https://staging.example.com/")).isEmpty());
    EXPECT_EQ(settings().relayUrl(), QStringLiteral("https://staging.example.com"));
    EXPECT_EQ(app().relayUrl().host(), QStringLiteral("staging.example.com"));

    QVariantMap check = app().checkJoinInput(QStringLiteral("482 913"));
    EXPECT_TRUE(check.value(QStringLiteral("valid")).toBool());
    EXPECT_EQ(check.value(QStringLiteral("code")).toString(), QStringLiteral("482 913"));
    EXPECT_EQ(check.value(QStringLiteral("foreignRelay")).toString(), QString());
    check = app().checkJoinInput(QStringLiteral("https://terminal.hardwario.com/?session=012345"));
    EXPECT_TRUE(check.value(QStringLiteral("valid")).toBool());
    EXPECT_EQ(check.value(QStringLiteral("codeValue")).toString(), QStringLiteral("12345"));
    EXPECT_EQ(check.value(QStringLiteral("foreignRelay")).toString(),
              QStringLiteral("https://terminal.hardwario.com"));
    EXPECT_FALSE(
        app().checkJoinInput(QStringLiteral("12a")).value(QStringLiteral("valid")).toBool());
    // A foreign server is not joined without the confirmation.
    EXPECT_FALSE(app().joinRemoteSession(
        QStringLiteral("https://terminal.hardwario.com/?session=482913"), false));

    EXPECT_TRUE(settings().setRelayUrl(QStringLiteral("https://terminal.hardwario.com")).isEmpty());
    EXPECT_EQ(settings().relayUrl(), QString()); // the default is stored as ""
}

TEST_F(SettingsTest, GeneralItemsApplyImmediately) {
    settings().setTheme(QStringLiteral("dark"));
    EXPECT_EQ(app().theme(), QStringLiteral("dark")); // live
    EXPECT_TRUE(settings().restoreTabs());
    settings().setRestoreTabs(false);
    EXPECT_FALSE(app().settingsStore()->value(QStringLiteral("ui/restoreTabs")).toBool());
    EXPECT_TRUE(settings().writeError().isEmpty());
    ASSERT_GE(settings().languages().size(), 3);
    EXPECT_EQ(settings().languages().at(0).toMap().value(QStringLiteral("name")).toString(),
              QStringLiteral("System"));
}

TEST_F(SettingsTest, DefaultSpeedIsAutomaticAndKeepsTheFixedValue) {
    EXPECT_TRUE(settings().autoSpeed());
    EXPECT_EQ(settings().defaultSpeedKhz(), hwio::gui::ConnectionOptions::defaultFixedSpeedKhz);
    settings().setAutoSpeed(false);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("jlink/speedKhz")).toInt(), 4000);
    settings().setDefaultSpeedKhz(100);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("jlink/speedKhz")).toInt(), 100);
    EXPECT_FALSE(settings().speedWarning().isEmpty()); // ADR-0110: below 125 kHz
    settings().setDefaultSpeedKhz(60000);              // out of range: not saved
    EXPECT_EQ(settings().defaultSpeedKhz(), 100);
    settings().setAutoSpeed(true);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("jlink/speedKhz")).toInt(), 0);
    EXPECT_EQ(settings().defaultSpeedKhz(), 100); // kept while automatic
    EXPECT_TRUE(settings().speedWarning().isEmpty());

    // The same model as the ⋯ Advanced popover: a new tab starts with these defaults.
    settings().setAutoSpeed(false);
    settings().setDefaultSpeedKhz(1000);
    restart();
    EXPECT_FALSE(settings().autoSpeed());
    EXPECT_EQ(settings().defaultSpeedKhz(), 1000);
}

TEST_F(SettingsTest, ConnectionDefaults) {
    EXPECT_GT(settings().defaultLatencyMs(), 0); // the profile's value while 0 is stored
    settings().setDefaultLatencyMs(25);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("rtt/latencyMs")).toInt(), 25);
    settings().setDefaultLatencyMs(5000); // invalid: not saved
    EXPECT_EQ(settings().defaultLatencyMs(), 25);
    settings().setAutoReconnect(false);
    EXPECT_FALSE(app().settingsStore()->value(QStringLiteral("rtt/autoReconnect")).toBool());
    settings().setResetOnConnect(true);
    EXPECT_TRUE(app().settingsStore()->value(QStringLiteral("rtt/resetOnOpen")).toBool());
}

TEST_F(SettingsTest, DependencyDetection) {
    DependencyStatus* status = settings().dependencies();
    EXPECT_EQ(status->jlinkState(), DependencyStatus::Unknown); // nothing before the page is shown
    settings().pageShown();
    ASSERT_TRUE(waitFor([&] {
        return status->jlinkState() != DependencyStatus::Checking &&
               status->jlinkState() != DependencyStatus::Unknown &&
               status->nrfUtilState() != DependencyStatus::Checking;
    }));
    EXPECT_EQ(status->jlinkState(), DependencyStatus::Found)
        << status->jlinkError().detail().toStdString();
    EXPECT_EQ(status->jlinkVersion(), QStringLiteral("8.10"));
    EXPECT_TRUE(status->jlinkPath().contains(QStringLiteral("fake_jlink")));
    EXPECT_EQ(status->jlinkSourceText(),
              QStringLiteral("Set by the environment variable HWIO_JLINK_LIBRARY"));
    EXPECT_EQ(status->nrfUtilState(), DependencyStatus::Problem);
    EXPECT_EQ(status->nrfUtilError().code(), QStringLiteral("dependency.nrfutil_not_found"));
    EXPECT_EQ(settings().nrfUtilStatusText(),
              QStringLiteral("Not found — needed only for LTE modem firmware"));
    EXPECT_TRUE(settings().aboutText().contains(QStringLiteral("J-Link Software: 8.10")));
    // Plan 0028 (ADR-0202): Copy shows the unified toast once.
    settings().copyAbout();
    EXPECT_EQ(app().toasts().count(QStringLiteral("Copied to clipboard.")), 1);

    // With nRF Util (the fake).
    qputenv("HWIO_NRFUTIL", QByteArray(HWIO_FAKE_NRFUTIL));
    status->refresh();
    ASSERT_TRUE(waitFor([&] { return status->nrfUtilState() == DependencyStatus::Found; }));
    EXPECT_TRUE(status->nrfUtilVersion().startsWith(QStringLiteral("7.13.0")));
}

TEST_F(SettingsTest, CustomPathsAreCheckedBeforeTheyAreSaved) {
    settings().setJlinkLibraryMode(QStringLiteral("custom"));
    EXPECT_EQ(settings().jlinkLibraryMode(), QStringLiteral("custom"));
    settings().setJlinkLibraryPath(
        QUrl::fromLocalFile(QString::fromStdString((root() / "nothing.so").string())));
    EXPECT_TRUE(settings().jlinkPathChecking());
    ASSERT_TRUE(waitFor([&] { return !settings().jlinkPathChecking(); }));
    EXPECT_FALSE(settings().jlinkPathError().isEmpty());
    EXPECT_TRUE(app().settingsStore()->value(FirmwareServices::jlinkPathKey).toString().isEmpty());

    // A loadable library is saved.
    settings().setJlinkLibraryPath(QUrl::fromLocalFile(QStringLiteral(HWIO_FAKE_JLINK_LIBRARY)));
    ASSERT_TRUE(waitFor([&] { return !settings().jlinkPathChecking(); }));
    EXPECT_TRUE(settings().jlinkPathError().isEmpty()) << settings().jlinkPathError().toStdString();
    EXPECT_EQ(app().settingsStore()->value(FirmwareServices::jlinkPathKey).toString(),
              QStringLiteral(HWIO_FAKE_JLINK_LIBRARY));
    settings().setJlinkLibraryMode(QStringLiteral("auto"));
    EXPECT_TRUE(app().settingsStore()->value(FirmwareServices::jlinkPathKey).toString().isEmpty());
    EXPECT_EQ(settings().jlinkLibraryMode(), QStringLiteral("auto"));

    // nRF Util: a missing program is not saved, the fake is.
    settings().setNrfUtilPath(
        QUrl::fromLocalFile(QString::fromStdString((root() / "nrfutil").string())));
    ASSERT_TRUE(waitFor([&] { return !settings().nrfUtilPathChecking(); }));
    EXPECT_FALSE(settings().nrfUtilPathError().isEmpty());
    EXPECT_TRUE(
        app().settingsStore()->value(FirmwareServices::nrfUtilPathKey).toString().isEmpty());
    settings().setNrfUtilPath(QUrl::fromLocalFile(QStringLiteral(HWIO_FAKE_NRFUTIL)));
    ASSERT_TRUE(waitFor([&] { return !settings().nrfUtilPathChecking(); }));
    EXPECT_EQ(app().settingsStore()->value(FirmwareServices::nrfUtilPathKey).toString(),
              QStringLiteral(HWIO_FAKE_NRFUTIL));
    EXPECT_EQ(settings().nrfUtilMode(), QStringLiteral("custom"));
}

TEST_F(SettingsTest, ReadOnlySettingsFile) {
    // A file of a newer schema is opened read-only.
    restartWith([this] {
        QSettings raw(QString::fromStdU16String(settingsFile().u16string()), QSettings::IniFormat);
        raw.setValue(QStringLiteral("meta/schemaVersion"), hwio::currentSettingsSchema + 1);
        raw.sync();
    });
    EXPECT_TRUE(settings().readOnly());
    settings().setRestoreTabs(false);
    EXPECT_FALSE(settings().writeError().isEmpty());
    EXPECT_TRUE(settings().restoreTabs());
    // Plan 0045: the mode applies for this run; the write error is shown.
    settings().selectMode(static_cast<int>(UiMode::Basic));
    EXPECT_EQ(settings().mode(), static_cast<int>(UiMode::Basic));
    EXPECT_FALSE(settings().writeError().isEmpty());
}

// Plan 0038 A0 (ADR-0263): Nordic's direct nrfutil downloads per OS / CPU, else the product page.
TEST(SettingsNrfUtilDownload, UrlPerOsAndArch) {
    const QString base = QStringLiteral(
        "https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/");
    EXPECT_EQ(SettingsViewModel::nrfUtilDownloadUrlFor(u"windows", u"x86_64"),
              base + QStringLiteral("x86_64-pc-windows-msvc/nrfutil.exe"));
    EXPECT_EQ(SettingsViewModel::nrfUtilDownloadUrlFor(u"windows", u"arm64"),
              base + QStringLiteral("x86_64-pc-windows-msvc/nrfutil.exe"));
    EXPECT_EQ(SettingsViewModel::nrfUtilDownloadUrlFor(u"macos", u"arm64"),
              base + QStringLiteral("universal-apple-darwin/nrfutil"));
    EXPECT_EQ(SettingsViewModel::nrfUtilDownloadUrlFor(u"linux", u"x86_64"),
              base + QStringLiteral("x86_64-unknown-linux-gnu/nrfutil"));
    EXPECT_EQ(SettingsViewModel::nrfUtilDownloadUrlFor(u"linux", u"arm64"),
              base + QStringLiteral("aarch64-unknown-linux-gnu/nrfutil"));
    const QString page =
        QStringLiteral("https://www.nordicsemi.com/Products/Development-tools/nrf-util");
    EXPECT_EQ(SettingsViewModel::nrfUtilDownloadUrlFor(u"linux", u"riscv64"), page);
    EXPECT_EQ(SettingsViewModel::nrfUtilDownloadUrlFor(u"other", u"x86_64"), page);
    EXPECT_TRUE(SettingsViewModel::nrfUtilDownloadUrl().startsWith(QStringLiteral("https://")));
}

} // namespace
