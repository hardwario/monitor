// Error-code coverage of the wizards (plan/features/0035 § D): every ErrorCode whose default texts
// name a wizard in libs/core/src/error.cpp has a start step or topic in that wizard. The test lands
// with the first wizard (0008) and grows with 0010 and 0009.

#include "hwio/core/error.h"
#include "wizard/setup_wizard.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace {

using hwio::ErrorCode;
using hwio::WizardId;

TEST(WizardCoverage, EveryCodeNamingAWizardHasAStartStep) {
    std::size_t firstRunSetup = 0;
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
        default:
            // BlePairing: not part of 2.0.0 (ADR-0250). ConnectDevice, Troubleshooting,
            // FlashFirmware: plans 0009 and 0010 add their checks here when their wizards land.
            // LteUpdate: registered by plan 0033 (target.other_connector).
            break;
        }
    }
    EXPECT_GT(firstRunSetup, 0U);
}

// ADR-0250: ble.permission_denied names no wizard until the setup wizard's Bluetooth step (0032).
TEST(WizardCoverage, BlePermissionDeniedHasNoWizard) {
    EXPECT_FALSE(hwio::makeError(ErrorCode::BlePermissionDenied).wizard.has_value());
}

} // namespace
