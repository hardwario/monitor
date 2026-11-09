#include "wizard/troubleshooting_topics.h"

#include "hwio/core/error.h"

#include <QtGlobal>
#include <QtTranslation>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace hwio::gui {

namespace {

using K = TopicStepKind;
using hwio::ErrorCode;

// Check id lists (environment checks of libs/diag, Diagnose steps of ADR-0069).
constexpr std::array<std::string_view, 3> envNoProbe{"probes", "usb-access", "probe-holders"};
constexpr std::array<std::string_view, 1> envProbes{"probes"};
constexpr std::array<std::string_view, 2> envProbeBusy{"usb-access", "probe-holders"};
constexpr std::array<std::string_view, 1> envProbeFirmware{"probe-firmware"};
constexpr std::array<std::string_view, 1> envNrfUtil{"nrfutil"};
constexpr std::array<std::string_view, 1> envBluetooth{"bluetooth"};
constexpr std::array<std::string_view, 1> dgProbe{"probe"};
constexpr std::array<std::string_view, 2> dgProbePower{"probe", "power"};
constexpr std::array<std::string_view, 1> dgPower{"power"};
constexpr std::array<std::string_view, 3> dgToConnect{"probe", "power", "connect"};
constexpr std::array<std::string_view, 4> dgToIdentify{"probe", "power", "connect", "identify"};
constexpr std::array<std::string_view, 1> dgRtt{"rtt"};

// ---- no-probe: No J-Link found ----
constexpr std::array noProbeCodes{ErrorCode::ProbeNotFound, ErrorCode::ProbeIoFailed};
constexpr std::array noProbeSteps{
    TopicStep{.kind = K::AutoEnvironment,
              .checkIds = envNoProbe,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Looking for the J-Link"),
              .bodySourceText = "",
              .illustrationSlot = "jlink-usb",
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "USB"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Reconnect the J-Link"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "Unplug the J-Link and connect it again. Try another USB "
                                     "port and another cable. The LED of the J-Link should light "
                                     "up."),
              .illustrationSlot = "jlink-usb",
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::AutoEnvironment,
              .checkIds = envProbes,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check again"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Looking for the J-Link again"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = actionConnect,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Connect"),
              .learnMore = false},
};

// ---- probe-busy: The J-Link is used by another program ----
constexpr std::array probeBusyCodes{ErrorCode::ProbeOpenFailed, ErrorCode::ProbeInUseByOtherProcess,
                                    ErrorCode::ProbeHeldByProgram};
constexpr std::array probeBusySteps{
    TopicStep{.kind = K::AutoEnvironment,
              .checkIds = envProbeBusy,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Who uses the J-Link?"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Other programs"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Close the other program"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting",
                  "Only one program can use a J-Link at a time. Close the other HARDWARIO Monitor "
                  "window, the other <b>hwio</b> command, or debugging tools such as the SEGGER "
                  "J-Link Commander."),
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "USB"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Reconnect the J-Link"),
              .bodySourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Unplug the J-Link and connect it again."),
              .illustrationSlot = "jlink-usb",
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgProbe,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check again"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Opening the J-Link"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
};

// ---- no-power: The device has no power or does not respond ----
constexpr std::array noPowerCodes{ErrorCode::TargetNoPower};
constexpr std::array noPowerSteps{
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Power"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Power the device"),
              .bodySourceText = "",
              .illustrationSlot = guidePower,
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgProbePower,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Measuring the power"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Cable"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check the flat cable"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "Check that the flat cable is fully inserted at both ends."),
              .illustrationSlot = guideCable,
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgPower,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check again"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Measuring the power again"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
};

// ---- no-connect: (same docs section) ----
constexpr std::array noConnectCodes{ErrorCode::TargetConnectFailed, ErrorCode::TargetWrongFamily};
constexpr std::array noConnectSteps{
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgToIdentify,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Checking the connection"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Cable"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check the flat cable"),
              .bodySourceText = "",
              .illustrationSlot = guideCable,
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Speed"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Lower the J-Link speed"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "A long or poor cable may need a lower speed. Use 1000 kHz "
                                     "for this J-Link, then check again."),
              .illustrationSlot = {},
              .action = actionSpeed1000,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Use 1000 kHz"),
              .learnMore = false},
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgToIdentify,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check again"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Checking the connection again"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
};

// ---- other-connector: The J-Link is on the LTE connector (topic list only) ----
constexpr std::array otherConnectorSteps{
    TopicStep{.kind = K::HandOff,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Connector"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "The J-Link is on the LTE connector"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting",
                  "A tab works with the application MCU. Move the cable to the APP connector and "
                  "press <b>Retry</b> on the tab, or update the LTE firmware."),
              .illustrationSlot = guideCable,
              .action = handOffLteUpdate,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Update LTE firmware…"),
              .learnMore = false},
};

// ---- no-console: No console (RTT) found ----
constexpr std::array noConsoleCodes{ErrorCode::TargetRttNotFound,
                                    ErrorCode::TargetRttBufferNotFound};
constexpr std::array noConsoleSteps{
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Checking the connection"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Reset"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Reset the device"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "A reset starts the firmware again, and with it the console."),
              .illustrationSlot = {},
              .action = actionReset,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Reset"),
              .learnMore = false},
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgRtt,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Console"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Looking for the console"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::HandOff,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Firmware"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Flash firmware with the console"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "The console needs firmware with the CHESTER console (RTT "
                                     "shell). Flash such firmware."),
              .illustrationSlot = {},
              .action = handOffFlashWizard,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Flash firmware…"),
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Speed"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Use Automatic speed"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "A very low J-Link speed can also hide the console. Choose "
                                     "<b>Automatic speed</b> in the ⋯ menu of the device bar."),
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
};

// ---- console-stuck: (same docs section) ----
constexpr std::array consoleStuckCodes{ErrorCode::TargetRttWriteTimeout,
                                       ErrorCode::TargetRttReadFailed,
                                       ErrorCode::TargetRttWriteFailed};
constexpr std::array consoleStuckSteps{
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgToConnect,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Checking the connection"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Reset"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Reset the device"),
              .bodySourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "The device may be busy or hung."),
              .illustrationSlot = {},
              .action = actionReset,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Reset"),
              .learnMore = false},
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgRtt,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Console"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Looking for the console"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Cable"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check the cables"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "Check the USB cable and the flat cable. When auto-reconnect "
                                     "is on, the console reconnects by itself."),
              .illustrationSlot = guideCable,
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
};

// ---- probe-firmware ----
constexpr std::array probeFirmwareCodes{ErrorCode::ProbeFirmwareOutdated,
                                        ErrorCode::ProbeFirmwareUpdateFailed};
constexpr std::array probeFirmwareSteps{
    TopicStep{.kind = K::AutoEnvironment,
              .checkIds = envProbeFirmware,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Checking the J-Link firmware"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Commands,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Update"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Update the J-Link firmware"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting",
                  "Update the firmware here, or run the command below. The J-Link must not be "
                  "used by a tab. Do not unplug it during the update."),
              .illustrationSlot = {},
              .action = actionProbeFirmware,
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::AutoEnvironment,
              .checkIds = envProbeFirmware,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check again"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Checking the J-Link firmware again"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
};

// ---- protected: the MCU is read-back protected ----
constexpr std::array protectedCodes{ErrorCode::TargetProtected};
constexpr std::array protectedSteps{
    TopicStep{.kind = K::HandOff,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Recover"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "The MCU is read-back protected"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting",
                  "Recover removes the protection, but it erases the whole MCU, including the UICR "
                  "and the Product Information Block. Recover is on the Flash page."),
              .illustrationSlot = {},
              .action = handOffRecover,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Open Flash page"),
              .learnMore = false},
};

// ---- program-failed: Flashing fails ----
constexpr std::array programFailedCodes{
    ErrorCode::TargetProgramFailed, ErrorCode::TargetEraseFailed, ErrorCode::FirmwareVerifyFailed};
constexpr std::array programFailedSteps{
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgToIdentify,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Checking the connection"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Speed"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Lower the J-Link speed"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "If verification fails again and again, use 1000 kHz for "
                                     "this J-Link."),
              .illustrationSlot = {},
              .action = actionSpeed1000,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Use 1000 kHz"),
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Power"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Keep the power stable"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "Make sure the power is stable during flashing: charged "
                                     "batteries or the USB-C power input."),
              .illustrationSlot = guidePower,
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::HandOff,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Flash"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Flash again"),
              .bodySourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Check the cables and the power, then flash "
                                                       "the firmware again."),
              .illustrationSlot = {},
              .action = handOffFlashWizard,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Try again"),
              .learnMore = false},
};

// ---- recover-failed ----
constexpr std::array recoverFailedCodes{ErrorCode::TargetRecoverFailed};
constexpr std::array recoverFailedSteps{
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Power"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Restart the power"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "Disconnect the power for 10 seconds, then connect it again."),
              .illustrationSlot = guidePower,
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::AutoDiagnose,
              .checkIds = dgToConnect,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Checking the connection"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::HandOff,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Recover"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Recover again"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting",
                  "Run Recover again on the Flash page. It erases the whole MCU, including the "
                  "UICR and the Product Information Block."),
              .illustrationSlot = {},
              .action = handOffRecover,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Open Flash page"),
              .learnMore = false},
};

// ---- modem-failed: LTE modem firmware ----
constexpr std::array modemFailedCodes{ErrorCode::FirmwareModemUpdateFailed};
constexpr std::array modemFailedSteps{
    TopicStep{.kind = K::AutoEnvironment,
              .checkIds = envNrfUtil,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Checking nRF Util"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Cable"),
              .headingSourceText =
                  QT_TRANSLATE_NOOP("Troubleshooting", "Check the cable and the power"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "The modem update takes 1–3 minutes and must not be "
                                     "interrupted. Check the cable and the power before you start "
                                     "it again."),
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::HandOff,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Update"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Update the LTE firmware"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "The guided LTE firmware update moves the J-Link between the "
                                     "connectors and updates the modem."),
              .illustrationSlot = {},
              .action = handOffLteUpdate,
              .actionSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Update LTE firmware…"),
              .learnMore = false},
};

// ---- bluetooth (manual, no wizard codes: ADR-0250) ----
constexpr std::array bluetoothSteps{
    TopicStep{.kind = K::AutoEnvironment,
              .checkIds = envBluetooth,
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Check"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Checking Bluetooth"),
              .bodySourceText = "",
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Device"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Prepare the device"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting", "Turn the device on and bring it closer. Disconnect it from "
                                     "phones and other computers."),
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = false},
    TopicStep{.kind = K::Manual,
              .checkIds = {},
              .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Pairing"),
              .headingSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Pair again"),
              .bodySourceText = QT_TRANSLATE_NOOP(
                  "Troubleshooting",
                  "If pairing fails, remove the device in the system Bluetooth settings and pair "
                  "again. On Linux, a pairing agent must run to ask for the passkey."),
              .illustrationSlot = {},
              .action = {},
              .actionSourceText = "",
              .learnMore = true},
};

constexpr std::array topics{
    Topic{.id = "no-probe",
          .codes = noProbeCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "No J-Link found"),
          .steps = noProbeSteps,
          .docsTopic = "trouble-no-probe",
          .listed = true},
    Topic{.id = "probe-busy",
          .codes = probeBusyCodes,
          .titleSourceText =
              QT_TRANSLATE_NOOP("Troubleshooting", "The J-Link is used by another program"),
          .steps = probeBusySteps,
          .docsTopic = "trouble-probe-busy",
          .listed = true},
    Topic{.id = "no-power",
          .codes = noPowerCodes,
          .titleSourceText =
              QT_TRANSLATE_NOOP("Troubleshooting", "The device has no power or does not respond"),
          .steps = noPowerSteps,
          .docsTopic = "trouble-no-power",
          .listed = true},
    Topic{.id = "no-connect",
          .codes = noConnectCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "The device does not respond"),
          .steps = noConnectSteps,
          .docsTopic = "trouble-no-power",
          .listed = false},
    Topic{.id = "other-connector",
          .codes = {},
          .titleSourceText =
              QT_TRANSLATE_NOOP("Troubleshooting", "The J-Link is on the LTE connector"),
          .steps = otherConnectorSteps,
          .docsTopic = "trouble-other-connector",
          .listed = true},
    Topic{.id = "no-console",
          .codes = noConsoleCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "The console stays empty"),
          .steps = noConsoleSteps,
          .docsTopic = "trouble-no-console",
          .listed = true},
    Topic{.id = "console-stuck",
          .codes = consoleStuckCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "The console does not answer"),
          .steps = consoleStuckSteps,
          .docsTopic = "trouble-no-console",
          .listed = false},
    Topic{.id = "program-failed",
          .codes = programFailedCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Flashing fails"),
          .steps = programFailedSteps,
          .docsTopic = "trouble-flashing",
          .listed = true},
    Topic{.id = "protected",
          .codes = protectedCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "The MCU is protected"),
          .steps = protectedSteps,
          .docsTopic = "trouble-flashing",
          .listed = false},
    Topic{.id = "recover-failed",
          .codes = recoverFailedCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Recover fails"),
          .steps = recoverFailedSteps,
          .docsTopic = "trouble-flashing",
          .listed = false},
    Topic{.id = "probe-firmware",
          .codes = probeFirmwareCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "J-Link firmware"),
          .steps = probeFirmwareSteps,
          .docsTopic = {},
          .listed = true},
    Topic{.id = "modem-failed",
          .codes = modemFailedCodes,
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "LTE modem firmware"),
          .steps = modemFailedSteps,
          .docsTopic = "trouble-nrf-util",
          .listed = true},
    Topic{.id = "bluetooth",
          .codes = {},
          .titleSourceText = QT_TRANSLATE_NOOP("Troubleshooting", "Bluetooth"),
          .steps = bluetoothSteps,
          .docsTopic = "trouble-bluetooth",
          .listed = true},
};

static_assert(topics.size() <= maxTopics);
static_assert(std::ranges::all_of(topics, [](const Topic& topic) {
    return !topic.steps.empty() && topic.steps.size() <= maxTopicSteps;
}));

} // namespace

std::span<const Topic> troubleshootingTopics() { return topics; }

const Topic* topicFor(ErrorCode code) {
    const auto* const it = std::ranges::find_if(topics, [code](const Topic& topic) {
        return std::ranges::find(topic.codes, code) != topic.codes.end();
    });
    return it != topics.end() ? &*it : nullptr;
}

const Topic* topicById(std::string_view id) {
    const auto* const it =
        std::ranges::find_if(topics, [id](const Topic& topic) { return topic.id == id; });
    return it != topics.end() ? &*it : nullptr;
}

} // namespace hwio::gui
