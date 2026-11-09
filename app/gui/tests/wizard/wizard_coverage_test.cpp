// Error-code coverage of the wizards (plan/features/0035 § D): every ErrorCode whose default texts
// name a wizard in libs/core/src/error.cpp has a start step or topic in that wizard. The test
// landed with the first wizard (0008) and grew with 0010 and 0009.

#include "hwio/core/error.h"
#include "wizard/connect_wizard.h"
#include "wizard/flash_wizard.h"
#include "wizard/setup_wizard.h"
#include "wizard/troubleshooting_topics.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hwio::ErrorCode;
using hwio::WizardId;

TEST(WizardCoverage, EveryCodeNamingAWizardHasAStartStep) {
    std::size_t firstRunSetup = 0;
    std::size_t flashFirmware = 0;
    std::size_t connectDevice = 0;
    std::size_t troubleshooting = 0;
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        const hwio::Error error = hwio::makeError(info.code);
        if (!error.wizard) {
            continue;
        }
        const std::string id(info.id);
        switch (*error.wizard) {
        case WizardId::FirstRunSetup:
            ++firstRunSetup;
            EXPECT_FALSE(hwio::gui::SetupWizard::startStepFor(info.code).isEmpty())
                << id << " names the setup wizard but has no start step";
            break;
        case WizardId::FlashFirmware:
            // Plan 0010: every FlashFirmware code starts on the Firmware step.
            ++flashFirmware;
            EXPECT_EQ(hwio::gui::FlashWizard::startStepFor(info.code),
                      hwio::gui::FlashWizard::Firmware)
                << id << " names the flash wizard but does not start on the Firmware step";
            break;
        case WizardId::ConnectDevice:
            // Plan 0009 (0035 § D): the Connect wizard's start step.
            ++connectDevice;
            EXPECT_TRUE(hwio::gui::ConnectWizard::startStepFor(info.code).has_value())
                << id << " names the Connect wizard but has no start step";
            break;
        case WizardId::Troubleshooting:
            // Plan 0009 (0035 § C): exactly one topic.
            ++troubleshooting;
            EXPECT_NE(hwio::gui::topicFor(info.code), nullptr)
                << id << " names troubleshooting but has no topic";
            break;
        default:
            // BlePairing: not part of 2.0.0 (ADR-0250).
            // LteUpdate: registered by plan 0033 (target.other_connector).
            break;
        }
    }
    EXPECT_GT(firstRunSetup, 0U);
    EXPECT_EQ(flashFirmware, 4U);
    EXPECT_EQ(connectDevice, 5U);
    EXPECT_GT(troubleshooting, 0U);
    EXPECT_FALSE(hwio::gui::FlashWizard::startStepFor(ErrorCode::ProbeNotFound).has_value());
}

// Plan 0009: the codes of the topic table are exactly the codes naming ConnectDevice or
// Troubleshooting that the Connect wizard does not start from, plus the ConnectDevice codes that
// also have a topic (reached from the topic list); each code is in at most one topic.
TEST(WizardCoverage, TopicCodesNameConnectOrTroubleshooting) {
    std::vector<ErrorCode> seen;
    for (const hwio::gui::Topic& topic : hwio::gui::troubleshootingTopics()) {
        EXPECT_LE(topic.steps.size(), hwio::gui::maxTopicSteps) << topic.id;
        for (const ErrorCode code : topic.codes) {
            EXPECT_EQ(std::ranges::count(seen, code), 0) << topic.id;
            seen.push_back(code);
            const auto wizard = hwio::makeError(code).wizard;
            EXPECT_TRUE(wizard == WizardId::Troubleshooting || wizard == WizardId::ConnectDevice)
                << topic.id;
        }
    }
    EXPECT_LE(hwio::gui::troubleshootingTopics().size(), hwio::gui::maxTopics);
    // The topic list (0035 § C), in this order.
    std::vector<std::string_view> listed;
    for (const hwio::gui::Topic& topic : hwio::gui::troubleshootingTopics()) {
        if (topic.listed) {
            listed.push_back(topic.id);
        }
    }
    const std::vector<std::string_view> expected{
        "no-probe",       "probe-busy",     "no-power",     "other-connector", "no-console",
        "program-failed", "probe-firmware", "modem-failed", "bluetooth"};
    EXPECT_EQ(listed, expected);
}

// ADR-0250: ble.permission_denied names no wizard until the setup wizard's Bluetooth step (0032).
TEST(WizardCoverage, BlePermissionDeniedHasNoWizard) {
    EXPECT_FALSE(hwio::makeError(ErrorCode::BlePermissionDenied).wizard.has_value());
}

} // namespace
