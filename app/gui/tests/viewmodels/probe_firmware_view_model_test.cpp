// J-Link firmware update in the GUI (plan/features/0036-jlink-firmware-update-gui.md; ADR-0132,
// ADR-0246, ADR-0256, ADR-0257): ProbeFirmwareWorker, ProbeFirmwareViewModel, the
// ConnectionViewModel banner and ProbeScanner::suspend(), against the fake J-Link library.

#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/probe_tools.h"
#include "hwio/device/session_registry.h"
#include "hwio/hw/jlink/types.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/probe_firmware_view_model.h"
#include "viewmodels/probe_picker_view_model.h"
#include "workers/probe_firmware_worker.h"
#include "workers/probe_scanner.h"

#include <QByteArray>
#include <QList>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

namespace {

using hwio::ErrorCode;
using hwio::Result;
using hwio::gui::AppViewModel;
using hwio::gui::ConnectionViewModel;
using hwio::gui::DeviceTabViewModel;
using hwio::gui::ProbeFirmwareContext;
using hwio::gui::ProbeFirmwareViewModel;
using hwio::gui::ProbeFirmwareWorker;
using hwio::gui::ProbePickerViewModel;
using hwio::gui::test::spin;
using hwio::gui::test::waitFor;
using State = ProbeFirmwareViewModel::State;

constexpr std::uint32_t probeSn = 801000001;

// One probe with outdated firmware; `updateResult` as in tests/fake_jlink.
QByteArray outdatedScenario(const char* updateResult = "updated") {
    return QStringLiteral(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
        "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
        "libraryFirmware": "J-Link V11 compiled Oct  1 2024 09:00:00",
        "updateResult": "%1",
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}]}}]})")
        .arg(QString::fromLatin1(updateResult))
        .toUtf8();
}

class ProbeFirmwareTest : public hwio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        useScenario(outdatedScenario().constData());
        m_app = makeApp();
    }
    void TearDown() override {
        m_app.reset();
        GuiTest::TearDown();
    }
    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] DeviceTabViewModel& tab() { return *m_app->currentTabViewModel(); }
    [[nodiscard]] ConnectionViewModel& connection() { return *tab().connection(); }
    [[nodiscard]] ProbeFirmwareViewModel& update() { return *m_app->probeFirmware(); }
    void resetApp() { m_app.reset(); }

    // The tab connected to the outdated probe, with the banner shown.
    void connectWithBanner() {
        ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), probeSn,
                                    QStringLiteral("Test Probe")));
        tab().connectDevice();
        ASSERT_TRUE(waitFor([&] { return connection().firmwareOutdated(); }));
    }
    [[nodiscard]] bool toastShown(const QString& text) const {
        return m_app->toasts().contains(text);
    }

private:
    std::unique_ptr<AppViewModel> m_app;
};

TEST_F(ProbeFirmwareTest, BannerTextAndDates) {
    connectWithBanner();
    EXPECT_EQ(
        connection().firmwareOutdatedText(),
        QStringLiteral("The J-Link firmware is outdated (2023-09-21, available: 2024-10-01)."));
    EXPECT_TRUE(connection().firmwareBlockedReason().isEmpty());
    EXPECT_FALSE(connection().warning().valid()); // not the plain warning banner
}

TEST_F(ProbeFirmwareTest, BannerDisconnectsUpdatesAndReconnects) {
    connectWithBanner();
    QSignalSpy finished(&update(), &ProbeFirmwareViewModel::finished);
    connection().updateProbeFirmware();
    // ADR-0246: the tab disconnected first; the probe is reserved and the scanner suspended.
    EXPECT_EQ(tab().session(), nullptr);
    EXPECT_FALSE(connection().firmwareOutdated());
    EXPECT_EQ(update().state(), State::Reading);
    EXPECT_TRUE(app().sessionRegistry().reserved(probeSn));
    EXPECT_TRUE(app().probeScanner().suspended());
    EXPECT_FALSE(app().sessionRegistry().checkAvailable(std::optional<std::uint32_t>(probeSn)));

    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    EXPECT_EQ(update().serial(), QStringLiteral("801000001"));
    EXPECT_EQ(update().product(), QStringLiteral("Test Probe"));
    EXPECT_EQ(update().currentString(), QStringLiteral("J-Link V11 compiled Sep 21 2023 10:38:43"));
    EXPECT_EQ(update().availableString(),
              QStringLiteral("J-Link V11 compiled Oct  1 2024 09:00:00"));

    update().confirm();
    EXPECT_EQ(update().state(), State::Updating);
    update().cancel(); // ignored: the update must not be interrupted
    EXPECT_EQ(update().state(), State::Updating);
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Idle; }));
    ASSERT_EQ(finished.count(), 1);
    EXPECT_TRUE(finished.at(0).at(0).toBool());
    EXPECT_TRUE(toastShown(QStringLiteral("J-Link firmware updated.")));
    EXPECT_FALSE(app().sessionRegistry().reserved(probeSn));
    EXPECT_FALSE(app().probeScanner().suspended());
    // ADR-0256: the tab connects again to the same probe.
    ASSERT_NE(tab().session(), nullptr);
    EXPECT_TRUE(waitFor([&] { return tab().state() == DeviceTabViewModel::Connected; }));
}

TEST_F(ProbeFirmwareTest, FailureShowsTheErrorAndDoesNotReconnect) {
    useScenario(outdatedScenario("unchanged").constData());
    connectWithBanner();
    connection().updateProbeFirmware();
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().confirm();
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Failed; }));
    EXPECT_EQ(update().error().code(), QStringLiteral("probe.firmware_update_failed"));
    EXPECT_FALSE(app().sessionRegistry().reserved(probeSn));
    EXPECT_FALSE(app().probeScanner().suspended());
    update().cancel(); // [Close]
    EXPECT_EQ(update().state(), State::Idle);
    spin(std::chrono::milliseconds(100));
    EXPECT_EQ(tab().session(), nullptr);
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
}

TEST_F(ProbeFirmwareTest, UnplugDuringTheUpdateIsAFailedUpdate) {
    useScenario(outdatedScenario("unplugged").constData());
    connectWithBanner();
    connection().updateProbeFirmware();
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().confirm();
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Failed; }));
    EXPECT_EQ(update().error().code(), QStringLiteral("probe.firmware_update_failed"));
}

TEST_F(ProbeFirmwareTest, CancelAtConfirmLeavesTheTabDisconnected) {
    connectWithBanner();
    QSignalSpy finished(&update(), &ProbeFirmwareViewModel::finished);
    connection().updateProbeFirmware();
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().cancel();
    EXPECT_EQ(update().state(), State::Idle);
    ASSERT_EQ(finished.count(), 1);
    EXPECT_FALSE(finished.at(0).at(0).toBool());
    EXPECT_FALSE(app().sessionRegistry().reserved(probeSn));
    EXPECT_FALSE(app().probeScanner().suspended());
    EXPECT_FALSE(connection().firmwareOutdated());
    spin(std::chrono::milliseconds(100));
    EXPECT_EQ(tab().session(), nullptr);
}

TEST_F(ProbeFirmwareTest, CancelWhileReadingDropsTheResult) {
    connectWithBanner();
    connection().updateProbeFirmware();
    ASSERT_EQ(update().state(), State::Reading);
    update().cancel();
    EXPECT_EQ(update().state(), State::Idle);
    EXPECT_TRUE(update().busy()); // the inspect job still runs
    EXPECT_EQ(update().blockedReason(probeSn), QStringLiteral("Another J-Link update is running."));
    ASSERT_TRUE(waitFor([&] { return !update().busy(); }));
    EXPECT_EQ(update().state(), State::Idle);
    EXPECT_FALSE(app().sessionRegistry().reserved(probeSn));
    EXPECT_FALSE(app().probeScanner().suspended());
}

TEST_F(ProbeFirmwareTest, BlockedReasons) {
    connectWithBanner();
    // A busy tab (flash, LTE update, Diagnose).
    tab().setBusy(QStringLiteral("flash"), QStringLiteral("Flashing"), QStringLiteral("Cancel?"),
                  [] {});
    EXPECT_EQ(connection().firmwareBlockedReason(),
              QStringLiteral("Wait for the current operation to finish."));
    connection().updateProbeFirmware(); // ignored
    EXPECT_EQ(update().state(), State::Idle);
    EXPECT_NE(tab().session(), nullptr);
    tab().clearBusy(QStringLiteral("flash"));
    EXPECT_TRUE(connection().firmwareBlockedReason().isEmpty());

    // The wizards: a tab uses the probe.
    EXPECT_EQ(update().blockedReasonFor(QStringLiteral("801000001")),
              QStringLiteral("Disconnect tab 1 first."));
    update().requestForSerial(QStringLiteral("801000001"));
    EXPECT_EQ(update().state(), State::Idle);
    EXPECT_TRUE(toastShown(QStringLiteral("Disconnect tab 1 first.")));

    // Plan 0014 hook: an AI assistant holds the probe.
    update().setAiHoldsProbe([](std::uint32_t serial) { return serial == probeSn; });
    EXPECT_EQ(connection().firmwareBlockedReason(), QStringLiteral("Used by an AI assistant"));
    update().setAiHoldsProbe({});

    // Another update runs.
    tab().disconnectDevice();
    update().requestForSerial(QStringLiteral("801000001"));
    EXPECT_EQ(update().state(), State::Reading);
    EXPECT_EQ(update().blockedReason(801000002),
              QStringLiteral("Another J-Link update is running."));
    // The reserved probe cannot be connected meanwhile.
    tab().connectDevice();
    EXPECT_EQ(tab().session(), nullptr);
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().cancel();
}

TEST_F(ProbeFirmwareTest, WizardRequestDoesNotConnect) {
    update().requestForSerial(QStringLiteral("801000001"));
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().confirm();
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Idle; }));
    EXPECT_TRUE(toastShown(QStringLiteral("J-Link firmware updated.")));
    EXPECT_EQ(tab().session(), nullptr);
}

TEST_F(ProbeFirmwareTest, ProbeNotFoundBeforeTheUpdate) {
    update().requestForSerial(QStringLiteral("801000009"));
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Failed; }));
    EXPECT_EQ(update().error().code(), QStringLiteral("probe.not_found"));
    EXPECT_FALSE(app().sessionRegistry().reserved(801000009));
    update().cancel();
    EXPECT_EQ(update().state(), State::Idle);
}

TEST_F(ProbeFirmwareTest, NotNowHidesTheBannerForThisProbeAndIsBounded) {
    connectWithBanner();
    connection().notNowProbeFirmware();
    EXPECT_FALSE(connection().firmwareOutdated());
    EXPECT_TRUE(update().isNotNow(probeSn));
    tab().disconnectDevice();
    tab().connectDevice();
    ASSERT_TRUE(waitFor([&] { return tab().state() == DeviceTabViewModel::Connected; }));
    spin(std::chrono::milliseconds(200));
    EXPECT_FALSE(connection().firmwareOutdated());
    EXPECT_FALSE(connection().warning().valid());

    for (std::uint32_t serial = 1; serial <= ProbeFirmwareViewModel::maxNotNow; ++serial) {
        update().notNow(serial);
    }
    EXPECT_FALSE(update().isNotNow(probeSn)); // the oldest was dropped
    EXPECT_TRUE(update().isNotNow(1));
    EXPECT_TRUE(update().isNotNow(ProbeFirmwareViewModel::maxNotNow));
}

TEST_F(ProbeFirmwareTest, PickersShowTheReservedProbe) {
    app().newTab();
    DeviceTabViewModel& other = tab();
    ProbePickerViewModel& picker = *other.picker();
    picker.setActive(true);
    ASSERT_TRUE(waitFor([&] { return picker.scanned() && picker.count() == 1; }));
    update().requestForSerial(QStringLiteral("801000001"));
    EXPECT_TRUE(picker.data(picker.index(0), ProbePickerViewModel::UpdatingFirmwareRole).toBool());
    EXPECT_FALSE(picker.data(picker.index(0), ProbePickerViewModel::SelectableRole).toBool());
    EXPECT_FALSE(picker.canConnect());
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().cancel();
    EXPECT_FALSE(picker.data(picker.index(0), ProbePickerViewModel::UpdatingFirmwareRole).toBool());
    picker.setActive(false);
}

// TSan: the reservation outlives a tab closed during the update; nothing reconnects.
TEST_F(ProbeFirmwareTest, TabClosedDuringTheUpdate) {
    connectWithBanner();
    app().newTab(); // the closed tab is not the last one
    app().setCurrentTab(0);
    connection().updateProbeFirmware();
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().confirm();
    app().closeTab(0, true);
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Idle; }));
    EXPECT_FALSE(app().sessionRegistry().reserved(probeSn));
    EXPECT_EQ(app().sessionRegistry().sessionCount(), 0U);
}

TEST_F(ProbeFirmwareTest, QuitWaitsForTheUpdate) {
    connectWithBanner();
    QSignalSpy quit(&app(), &AppViewModel::quitRequested);
    connection().updateProbeFirmware();
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().confirm();
    EXPECT_FALSE(app().canQuit());
    EXPECT_TRUE(update().quitPending());
    ASSERT_TRUE(waitFor([&] { return quit.count() == 1; }));
    EXPECT_EQ(update().state(), State::Idle);
    EXPECT_EQ(tab().session(), nullptr); // no reconnect while quitting
}

// Review M1: the banner belongs to the session and probe that reported it.
TEST_F(ProbeFirmwareTest, BannerDoesNotFollowTheTabToAnotherProbe) {
    useScenario(R"({"probes": [
        {"serialNumber": 801000001, "product": "Test Probe",
         "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
         "libraryFirmware": "J-Link V11 compiled Oct  1 2024 09:00:00"},
        {"serialNumber": 801000002, "product": "Test Probe 2",
         "firmware": "J-Link V11 compiled Oct  1 2024 09:00:00"}]})");
    connectWithBanner();
    tab().chooseAnotherProbe();
    EXPECT_FALSE(connection().firmwareOutdated());
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000002,
                                QStringLiteral("Test Probe 2")));
    tab().connectDevice();
    ASSERT_TRUE(waitFor([&] { return tab().state() == DeviceTabViewModel::Connected; }));
    spin(std::chrono::milliseconds(200));
    EXPECT_FALSE(connection().firmwareOutdated());
    EXPECT_TRUE(connection().firmwareOutdatedText().isEmpty());
    connection().updateProbeFirmware(); // never updates probe B
    EXPECT_EQ(update().state(), State::Idle);
    EXPECT_NE(tab().session(), nullptr);
    EXPECT_FALSE(app().sessionRegistry().reserved(801000002));
}

// ADR-0261: a failed update cancels a pending quit and shows the error.
TEST_F(ProbeFirmwareTest, FailedUpdateWhileQuittingKeepsTheAppOpen) {
    useScenario(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
        "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
        "libraryFirmware": "J-Link V11 compiled Oct  1 2024 09:00:00",
        "updateResult": "unchanged", "updateDelayMs": 300}]})");
    QSignalSpy quit(&app(), &AppViewModel::quitRequested);
    update().requestForSerial(QStringLiteral("801000001"));
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    update().confirm();
    EXPECT_FALSE(app().canQuit());
    EXPECT_TRUE(update().quitPending());
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Failed; }));
    spin(std::chrono::milliseconds(100));
    EXPECT_EQ(quit.count(), 0);
    EXPECT_FALSE(update().quitPending());
    EXPECT_EQ(update().error().code(), QStringLiteral("probe.firmware_update_failed"));
    update().cancel(); // [Close]
    spin(std::chrono::milliseconds(100));
    EXPECT_EQ(quit.count(), 0); // the earlier quit request stays cancelled
}

// ADR-0261: an unknown status asks explicitly; the update succeeds by the vendor call.
TEST_F(ProbeFirmwareTest, UnknownStatusAsksAndUpdates) {
    useScenario(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
        "firmware": "J-Link V11 custom A", "libraryFirmware": "J-Link V11 custom B"}]})");
    update().requestForSerial(QStringLiteral("801000001"));
    ASSERT_TRUE(waitFor([&] { return update().state() == State::Confirm; }));
    EXPECT_TRUE(update().statusUnknown());
    update().confirm();
    ASSERT_TRUE(waitFor([&] { return update().state() != State::Updating; }));
    EXPECT_EQ(update().state(), State::Idle);
    EXPECT_TRUE(toastShown(QStringLiteral("J-Link firmware updated.")));
}

// A standalone view model with replaced probe tools.
class ProbeFirmwareToolsTest : public ProbeFirmwareTest {
protected:
    [[nodiscard]] std::unique_ptr<ProbeFirmwareViewModel>
    makeViewModel(ProbeFirmwareWorker::Tools tools, QStringList* toasts) {
        return std::make_unique<ProbeFirmwareViewModel>(ProbeFirmwareContext{
            .registry = &app().sessionRegistry(),
            .scanner = &app().probeScanner(),
            .locatorOptions = [this] { return app().locatorOptions(); },
            .tabs = [] { return QList<DeviceTabViewModel*>(); },
            .showToast = [toasts](const QString& text) { toasts->append(text); },
            .tools = std::move(tools)});
    }
    static Result<hwio::device::ProbeDetails> outdatedDetails(std::uint32_t serial) {
        hwio::device::ProbeDetails details;
        details.probe.serialNumber = serial;
        details.probe.firmware = "J-Link V11 compiled Sep 21 2023 10:38:43";
        details.libraryFirmware = "J-Link V11 compiled Oct  1 2024 09:00:00";
        details.firmwareStatus = hwio::jlink::FirmwareStatus::Outdated;
        return details;
    }
};

TEST_F(ProbeFirmwareToolsTest, AlreadyUpToDateWhenUpdatedMeanwhile) {
    QStringList toasts;
    auto vm = makeViewModel(
        ProbeFirmwareWorker::Tools{
            .inspect = [](const auto&, const auto&,
                          std::uint32_t serial) { return outdatedDetails(serial); },
            .update =
                [](const auto&, const auto&, std::uint32_t serial) {
                    return Result<hwio::device::FirmwareUpdate>(hwio::device::FirmwareUpdate{
                        .serialNumber = serial, .before = hwio::jlink::FirmwareStatus::Current});
                }},
        &toasts);
    vm->requestForSerial(QStringLiteral("801000001"));
    ASSERT_TRUE(waitFor([&] { return vm->state() == State::Confirm; }));
    vm->confirm();
    ASSERT_TRUE(waitFor([&] { return vm->state() == State::Idle; }));
    EXPECT_EQ(toasts, QStringList{QStringLiteral("The J-Link firmware is already up to date.")});
}

TEST_F(ProbeFirmwareToolsTest, NewerFirmwareIsNotDowngraded) {
    QStringList toasts;
    std::atomic<int> updates{0};
    auto vm = makeViewModel(
        ProbeFirmwareWorker::Tools{.inspect =
                                       [](const auto&, const auto&, std::uint32_t serial) {
                                           auto details = outdatedDetails(serial);
                                           details->firmwareStatus =
                                               hwio::jlink::FirmwareStatus::Newer;
                                           return details;
                                       },
                                   .update =
                                       [&updates](const auto&, const auto&, std::uint32_t) {
                                           ++updates;
                                           return Result<hwio::device::FirmwareUpdate>(
                                               hwio::fail(ErrorCode::InternalUnexpected));
                                       }},
        &toasts);
    vm->requestForSerial(QStringLiteral("801000001"));
    ASSERT_TRUE(waitFor([&] { return vm->state() == State::Idle && !vm->busy(); }));
    EXPECT_EQ(updates.load(), 0);
    EXPECT_EQ(toasts,
              QStringList{QStringLiteral("The J-Link firmware is newer than the J-Link library.")});
}

// The destructor (application quit) waits for a running update; it is never interrupted.
TEST_F(ProbeFirmwareToolsTest, DestructionWaitsForTheRunningUpdate) {
    QStringList toasts;
    std::atomic<bool> updateDone{false};
    auto vm = makeViewModel(
        ProbeFirmwareWorker::Tools{
            .inspect = [](const auto&, const auto&,
                          std::uint32_t serial) { return outdatedDetails(serial); },
            .update =
                [&updateDone](const auto&, const auto&, std::uint32_t serial) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    updateDone = true;
                    return Result<hwio::device::FirmwareUpdate>(hwio::device::FirmwareUpdate{
                        .serialNumber = serial, .before = hwio::jlink::FirmwareStatus::Outdated});
                }},
        &toasts);
    vm->requestForSerial(QStringLiteral("801000001"));
    ASSERT_TRUE(waitFor([&] { return vm->state() == State::Confirm; }));
    vm->confirm();
    EXPECT_TRUE(app().sessionRegistry().reserved(probeSn));
    vm.reset();
    EXPECT_TRUE(updateDone.load());
    EXPECT_FALSE(app().sessionRegistry().reserved(probeSn));
    EXPECT_FALSE(app().probeScanner().suspended());
}

TEST(ProbeFirmwareWorkerTest, DeliversExactlyOneResultPerJob) {
    std::atomic<int> calls{0};
    int delivered = 0;
    std::optional<ProbeFirmwareWorker::Job> lastJob;
    ProbeFirmwareWorker worker(
        ProbeFirmwareWorker::Tools{
            .inspect =
                [&calls](const auto&, const auto&, std::uint32_t) {
                    ++calls;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    return Result<hwio::device::ProbeDetails>(hwio::fail(ErrorCode::ProbeNotFound));
                },
            .update = {}},
        [&](ProbeFirmwareWorker::Job job, const ProbeFirmwareWorker::Outcome& outcome) {
            ++delivered;
            lastJob = job;
            EXPECT_EQ(std::holds_alternative<Result<hwio::device::ProbeDetails>>(outcome),
                      job == ProbeFirmwareWorker::Job::Inspect);
        });
    ASSERT_TRUE(worker.start({.job = ProbeFirmwareWorker::Job::Inspect,
                              .options = {},
                              .location = {},
                              .serialNumber = 1}));
    EXPECT_TRUE(worker.busy());
    EXPECT_FALSE(worker.start({.job = ProbeFirmwareWorker::Job::Inspect,
                               .options = {},
                               .location = {},
                               .serialNumber = 2}));
    ASSERT_TRUE(waitFor([&] { return delivered == 1; }));
    spin(std::chrono::milliseconds(100));
    EXPECT_EQ(delivered, 1);
    EXPECT_EQ(calls.load(), 1);
    EXPECT_EQ(lastJob, ProbeFirmwareWorker::Job::Inspect);
    EXPECT_FALSE(worker.busy());

    // A missing tool is an error result, not a crash; the next job may start.
    ASSERT_TRUE(worker.start({.job = ProbeFirmwareWorker::Job::Update,
                              .options = {},
                              .location = {},
                              .serialNumber = 3}));
    ASSERT_TRUE(waitFor([&] { return delivered == 2; }));
    EXPECT_EQ(lastJob, ProbeFirmwareWorker::Job::Update);
}

// ProbeScanner::suspend(): no scan while suspended; releasing scans at once (TSan: suspension
// racing a running scan).
TEST_F(ProbeFirmwareTest, ScannerSuspension) {
    hwio::gui::ProbeScanner& scanner = app().probeScanner();
    const auto sequence = [&scanner] {
        const auto& last = scanner.last();
        return last ? last->sequence : 0;
    };
    scanner.acquire();
    ASSERT_TRUE(waitFor([&] { return scanner.last().has_value(); }));
    {
        hwio::gui::ScanSuspension suspension = scanner.suspend();
        EXPECT_TRUE(suspension.active());
        EXPECT_TRUE(scanner.suspended());
        spin(std::chrono::milliseconds(300)); // a scan already running is delivered
        const std::uint64_t suspendedAt = sequence();
        scanner.scanNow();
        spin(std::chrono::milliseconds(600)); // 3 intervals of 200 ms
        EXPECT_EQ(sequence(), suspendedAt);

        hwio::gui::ScanSuspension moved = std::move(suspension);
        EXPECT_TRUE(moved.active());
        EXPECT_TRUE(scanner.suspended());
        const std::uint64_t before = sequence();
        moved.reset();
        EXPECT_FALSE(scanner.suspended());
        EXPECT_TRUE(waitFor([&] { return sequence() > before; }));
    }
    scanner.release();
}

} // namespace
