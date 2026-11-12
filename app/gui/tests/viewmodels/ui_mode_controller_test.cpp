// UiMode and UiModeController (plan/features/0045-user-modes.md, A1; ADR-0359, ADR-0360,
// ADR-0362): parsing, the first-start state, the preselection of upgrading users (the rc.3 switch
// advanced/expertActions), choosing (stored, the old key removed), an invalid stored value and a
// read-only settings file.

#include "hwio/core/settings.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/ui_mode.h"
#include "viewmodels/ui_mode_controller.h"

#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QVariant>

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace {

using hwio::gui::SettingsStore;
using hwio::gui::UiMode;
using hwio::gui::UiModeController;

class UiModeTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(m_dir.isValid()); }
    void TearDown() override { close(); }

    [[nodiscard]] QString file() const { return m_dir.filePath(QStringLiteral("Monitor.ini")); }

    // Lets `edit` change the file directly (QSettings), then opens the store and the controller.
    void open(const std::function<void(QSettings&)>& edit = {}) {
        close();
        if (edit) {
            QSettings raw(file(), QSettings::IniFormat);
            edit(raw);
            raw.sync();
        }
        std::optional<hwio::Settings> settings;
        if (auto opened = hwio::Settings::openFile(std::filesystem::path(file().toStdU16String()));
            opened) {
            settings.emplace(std::move(*opened));
        }
        m_store = std::make_unique<SettingsStore>(std::move(settings));
        hwio::gui::AppViewModel::registerShellKeys(*m_store);
        m_controller = std::make_unique<UiModeController>(*m_store, nullptr);
    }
    void close() {
        m_controller.reset();
        m_store.reset();
    }
    [[nodiscard]] QVariant raw(const QString& key) const {
        const QSettings settings(file(), QSettings::IniFormat);
        return settings.value(key);
    }

    [[nodiscard]] UiModeController& controller() { return *m_controller; }
    [[nodiscard]] SettingsStore& store() { return *m_store; }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<SettingsStore> m_store;
    std::unique_ptr<UiModeController> m_controller;
};

TEST(UiMode, ParseAndIds) {
    for (const UiMode mode : {UiMode::Basic, UiMode::Advanced, UiMode::Expert}) {
        EXPECT_EQ(hwio::gui::parseUiMode(hwio::gui::uiModeId(mode)), mode);
        EXPECT_EQ(hwio::gui::uiModeFromInt(static_cast<int>(mode)), mode);
    }
    EXPECT_EQ(hwio::gui::uiModeId(UiMode::Basic), QStringLiteral("basic"));
    EXPECT_EQ(hwio::gui::parseUiMode(u""), std::nullopt);
    EXPECT_EQ(hwio::gui::parseUiMode(u"Expert"), std::nullopt);
    EXPECT_EQ(hwio::gui::uiModeFromInt(3), std::nullopt);
    EXPECT_EQ(hwio::gui::uiModeFromInt(-1), std::nullopt);
    EXPECT_TRUE(hwio::gui::atLeast(UiMode::Expert, UiMode::Basic));
    EXPECT_TRUE(hwio::gui::atLeast(UiMode::Advanced, UiMode::Advanced));
    EXPECT_FALSE(hwio::gui::atLeast(UiMode::Basic, UiMode::Advanced));
    EXPECT_FALSE(hwio::gui::atLeast(UiMode::Advanced, UiMode::Expert));
}

// First start: nothing stored → not chosen, Advanced preselected (ADR-0359).
TEST_F(UiModeTest, FirstStartPreselectsAdvanced) {
    open();
    EXPECT_FALSE(controller().chosen());
    EXPECT_EQ(controller().mode(), UiMode::Advanced);
    EXPECT_TRUE(controller().shows(UiMode::Advanced));
    EXPECT_FALSE(controller().shows(UiMode::Expert));
    ASSERT_EQ(controller().modes().size(), 3);
    EXPECT_EQ(controller().modes().at(0).toMap().value(QStringLiteral("name")).toString(),
              QStringLiteral("Basic"));
    EXPECT_EQ(
        controller().modes().at(2).toMap().value(QStringLiteral("description")).toString(),
        QStringLiteral("Everything, including timing options and expert maintenance actions."));
}

// ADR-0362: upgrading with the expert switch on → Expert preselected; off → Advanced.
TEST_F(UiModeTest, UpgradePreselection) {
    open([](QSettings& raw) { raw.setValue(QStringLiteral("advanced/expertActions"), true); });
    EXPECT_FALSE(controller().chosen());
    EXPECT_EQ(controller().preselection(), UiMode::Expert);
    EXPECT_EQ(controller().mode(), UiMode::Expert);

    open([](QSettings& raw) { raw.setValue(QStringLiteral("advanced/expertActions"), false); });
    EXPECT_FALSE(controller().chosen());
    EXPECT_EQ(controller().mode(), UiMode::Advanced);
}

// Choosing stores ui/mode, removes advanced/expertActions (ADR-0360) and is kept after a restart.
TEST_F(UiModeTest, ChoosingStoresAndRemovesTheOldSwitch) {
    open([](QSettings& raw) { raw.setValue(QStringLiteral("advanced/expertActions"), true); });
    QSignalSpy chosen(&controller(), &UiModeController::chosenChanged);
    QSignalSpy mode(&controller(), &UiModeController::modeChanged);
    ASSERT_TRUE(controller().choose(UiMode::Basic));
    EXPECT_TRUE(controller().chosen());
    EXPECT_EQ(controller().mode(), UiMode::Basic);
    EXPECT_EQ(chosen.count(), 1);
    EXPECT_EQ(mode.count(), 1);
    close();
    EXPECT_EQ(raw(QStringLiteral("ui/mode")).toString(), QStringLiteral("basic"));
    EXPECT_FALSE(raw(QStringLiteral("advanced/expertActions")).isValid());

    open();
    EXPECT_TRUE(controller().chosen());
    EXPECT_EQ(controller().mode(), UiMode::Basic);
    controller().chooseMode(static_cast<int>(UiMode::Expert));
    EXPECT_EQ(controller().mode(), UiMode::Expert);
    controller().chooseMode(7); // ignored
    EXPECT_EQ(controller().mode(), UiMode::Expert);
}

// A hand-edited invalid value reads as not chosen: Advanced and the dialog again.
TEST_F(UiModeTest, InvalidStoredValue) {
    open([](QSettings& raw) { raw.setValue(QStringLiteral("ui/mode"), QStringLiteral("guru")); });
    EXPECT_FALSE(controller().chosen());
    EXPECT_EQ(controller().mode(), UiMode::Advanced);
}

// A write through the store (another view, tests) updates the mode.
TEST_F(UiModeTest, FollowsTheStore) {
    open();
    QSignalSpy mode(&controller(), &UiModeController::modeChanged);
    ASSERT_TRUE(store().setValue(QStringLiteral("ui/mode"), QStringLiteral("expert")));
    EXPECT_EQ(controller().mode(), UiMode::Expert);
    EXPECT_TRUE(controller().chosen());
    EXPECT_EQ(mode.count(), 1);
}

// A read-only file (newer schema): the mode applies for this run, writeFailed reports it.
TEST_F(UiModeTest, ReadOnlyFileKeepsTheModeForThisRun) {
    open([](QSettings& raw) {
        raw.setValue(QStringLiteral("meta/schemaVersion"), hwio::currentSettingsSchema + 1);
    });
    QSignalSpy failed(&controller(), &UiModeController::writeFailed);
    EXPECT_FALSE(controller().choose(UiMode::Expert));
    EXPECT_EQ(controller().mode(), UiMode::Expert);
    EXPECT_TRUE(controller().chosen());
    ASSERT_EQ(failed.count(), 1);
    EXPECT_FALSE(failed.at(0).at(0).toString().isEmpty());
}

} // namespace
