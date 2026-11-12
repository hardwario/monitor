#include "hwio/remote/consent.h"

#include "hwio/core/json.h"
#include "hwio/remote/tunnel.h"

#include <QCoreApplication>
#include <QString>
#include <QtCore/qttranslation.h>

#include <array>
#include <string>
#include <string_view>

namespace hwio::remote {

ConsentProvider::Withdraw AutoDenyConsent::askJoin(const JoinQuestion& /*question*/,
                                                   Answer answer) {
    answer(false);
    return {};
}

ConsentProvider::Withdraw AutoDenyConsent::askOperation(const OperationQuestion& /*question*/,
                                                        Answer answer) {
    answer(false);
    return {};
}

QString joinQuestionText(const JoinQuestion& question) {
    return QCoreApplication::translate(
               "hwio::remote",
               "Someone joined session %1. Allow them to send commands to the device? Allow only "
               "if HARDWARIO Support has just told you they are joining.")
        .arg(toQString(question.code.grouped()));
}

namespace {

QString targetName(tunnel::OpTarget target) {
    return target == tunnel::OpTarget::Lte
               ? QCoreApplication::translate("hwio::remote", "the LTE modem core")
               : QCoreApplication::translate("hwio::remote", "the application core");
}

QString firmwareName(const OperationQuestion& question) {
    if (question.firmwareName.empty()) {
        return QCoreApplication::translate("hwio::remote", "firmware %1")
            .arg(toQString(question.firmwareId));
    }
    return QCoreApplication::translate("hwio::remote", "firmware %1 %2 (ID %3)")
        .arg(toQString(question.firmwareName), toQString(question.firmwareVersion),
             toQString(question.firmwareId));
}

// "HARDWARIO LTE firmware 1.7.0" (ADR-0363: the host's recommended LTE firmware).
QString lteFirmwareName(const OperationQuestion& question) {
    if (question.firmwareName.empty()) {
        return QCoreApplication::translate("hwio::remote", "the recommended version");
    }
    return question.firmwareVersion.empty()
               ? toQString(question.firmwareName)
               : toQString(question.firmwareName + " " + question.firmwareVersion);
}

} // namespace

QString operationPhaseText(std::string_view phase) {
    // ADR-0363: the steps of the guided LTE update (device::LteUpdateProcedure::stepId).
    struct PhaseText {
        std::string_view id;
        const char* text;
    };
    static constexpr std::array<PhaseText, 11> texts{{
        {.id = "download", .text = QT_TRANSLATE_NOOP("hwio::remote", "downloading the firmware")},
        {.id = "connect-app",
         .text = QT_TRANSLATE_NOOP("hwio::remote", "waiting for the J-Link on the APP connector")},
        // Plan 0046: confirmed before the application is erased (the customer, ADR-0366, or
        // support, ADR-0368).
        {.id = "confirm",
         .text =
             QT_TRANSLATE_NOOP("hwio::remote", "waiting for the confirmation before the erase")},
        {.id = "backup",
         .text = QT_TRANSLATE_NOOP("hwio::remote", "saving a copy of the application")},
        {.id = "backup-failed",
         .text = QT_TRANSLATE_NOOP("hwio::remote",
                                   "no copy of the application, waiting for the customer")},
        {.id = "erase", .text = QT_TRANSLATE_NOOP("hwio::remote", "stopping the application")},
        {.id = "connect-lte",
         .text = QT_TRANSLATE_NOOP("hwio::remote", "waiting for the J-Link on the LTE connector")},
        {.id = "update-lte", .text = QT_TRANSLATE_NOOP("hwio::remote", "updating the LTE MCU")},
        {.id = "connect-app-again",
         .text = QT_TRANSLATE_NOOP("hwio::remote",
                                   "waiting for the J-Link on the APP connector again")},
        {.id = "choose-application",
         .text = QT_TRANSLATE_NOOP("hwio::remote",
                                   "the customer chooses the application to write back")},
        {.id = "restore",
         .text = QT_TRANSLATE_NOOP("hwio::remote", "writing the application back")},
    }};
    for (const PhaseText& entry : texts) {
        if (entry.id == phase) {
            return QCoreApplication::translate("hwio::remote", entry.text);
        }
    }
    return toQString(phase);
}

QString operationQuestionKindText(tunnel::OpQuestionKind kind) {
    switch (kind) {
    case tunnel::OpQuestionKind::Continue:
        return QCoreApplication::translate(
            "hwio::remote",
            "The application on the customer's CHESTER will be erased. Then the customer moves the "
            "J-Link cable to the LTE connector, and at the end back to APP; the application is "
            "written back. Continue?");
    case tunnel::OpQuestionKind::ContinueWithoutCopy:
        return QCoreApplication::translate(
            "hwio::remote", "The application could not be copied. Continue without a copy?");
    }
    return {};
}

QString operationResultText(tunnel::OpKind kind, tunnel::OpTarget target, const QString& time) {
    const bool app = target == tunnel::OpTarget::App;
    const char* text = nullptr;
    switch (kind) {
    case tunnel::OpKind::Flash:
        text =
            app ? QT_TRANSLATE_NOOP("hwio::remote",
                                    "Application firmware flashed on the customer's device (%1).")
                : QT_TRANSLATE_NOOP("hwio::remote",
                                    "LTE MCU firmware flashed on the customer's device (%1).");
        break;
    case tunnel::OpKind::Erase:
        text = app ? QT_TRANSLATE_NOOP("hwio::remote",
                                       "Application MCU erased on the customer's device (%1).")
                   : QT_TRANSLATE_NOOP("hwio::remote",
                                       "LTE MCU erased on the customer's device (%1).");
        break;
    case tunnel::OpKind::Recover:
        text = app ? QT_TRANSLATE_NOOP("hwio::remote",
                                       "Application MCU recovered on the customer's device (%1).")
                   : QT_TRANSLATE_NOOP("hwio::remote",
                                       "LTE MCU recovered on the customer's device (%1).");
        break;
    case tunnel::OpKind::Reset:
        text =
            app ? QT_TRANSLATE_NOOP("hwio::remote", "Customer's device reset (%1).")
                : QT_TRANSLATE_NOOP("hwio::remote", "LTE MCU of the customer's device reset (%1).");
        break;
    case tunnel::OpKind::LteUpdate:
        text = QT_TRANSLATE_NOOP("hwio::remote",
                                 "LTE firmware updated, application written back (%1).");
        break;
    }
    return text == nullptr ? QString()
                           : QCoreApplication::translate("hwio::remote", text).arg(time);
}

QString operationFailedText(tunnel::OpKind kind, tunnel::OpTarget target, const QString& time,
                            const QString& message) {
    const bool app = target == tunnel::OpTarget::App;
    const char* text = nullptr;
    switch (kind) {
    case tunnel::OpKind::Flash:
        text =
            app ? QT_TRANSLATE_NOOP(
                      "hwio::remote",
                      "Flashing the application firmware on the customer's device failed (%1): %2")
                : QT_TRANSLATE_NOOP(
                      "hwio::remote",
                      "Flashing the LTE MCU firmware on the customer's device failed (%1): %2");
        break;
    case tunnel::OpKind::Erase:
        text =
            app ? QT_TRANSLATE_NOOP(
                      "hwio::remote",
                      "Erasing the application MCU on the customer's device failed (%1): %2")
                : QT_TRANSLATE_NOOP("hwio::remote",
                                    "Erasing the LTE MCU on the customer's device failed (%1): %2");
        break;
    case tunnel::OpKind::Recover:
        text = app ? QT_TRANSLATE_NOOP(
                         "hwio::remote",
                         "Recovering the application MCU on the customer's device failed (%1): %2")
                   : QT_TRANSLATE_NOOP(
                         "hwio::remote",
                         "Recovering the LTE MCU on the customer's device failed (%1): %2");
        break;
    case tunnel::OpKind::Reset:
        text = app ? QT_TRANSLATE_NOOP("hwio::remote",
                                       "Resetting the customer's device failed (%1): %2")
                   : QT_TRANSLATE_NOOP(
                         "hwio::remote",
                         "Resetting the LTE MCU of the customer's device failed (%1): %2");
        break;
    case tunnel::OpKind::LteUpdate:
        text = QT_TRANSLATE_NOOP(
            "hwio::remote", "The LTE firmware update on the customer's device failed (%1): %2");
        break;
    }
    return text == nullptr ? message
                           : QCoreApplication::translate("hwio::remote", text).arg(time, message);
}

QString operationQuestionText(const OperationQuestion& question) {
    switch (question.kind) {
    case tunnel::OpKind::Flash:
        return QCoreApplication::translate("hwio::remote",
                                           "Support asks to flash %1 to %2. The device restarts.")
            .arg(firmwareName(question), targetName(question.target));
    case tunnel::OpKind::Erase:
        return QCoreApplication::translate("hwio::remote",
                                           "Support asks to erase the firmware of %1. The device "
                                           "stops working until new firmware is flashed.")
            .arg(targetName(question.target));
    case tunnel::OpKind::Recover:
        return QCoreApplication::translate("hwio::remote",
                                           "Support asks to recover %1. This erases its flash "
                                           "completely, including the protection settings.")
            .arg(targetName(question.target));
    case tunnel::OpKind::Reset:
        return QCoreApplication::translate("hwio::remote",
                                           "Support asks to reset %1. The device restarts.")
            .arg(targetName(question.target));
    case tunnel::OpKind::LteUpdate:
        return QCoreApplication::translate("hwio::remote",
                                           "Support asks to update the LTE firmware to %1. You "
                                           "will have to move the J-Link cable between the APP "
                                           "and LTE connectors.")
            .arg(lteFirmwareName(question));
    }
    return {};
}

QString operationSummary(const OperationQuestion& question) {
    switch (question.kind) {
    case tunnel::OpKind::Flash:
        return QCoreApplication::translate("hwio::remote", "Flash %1 to %2")
            .arg(firmwareName(question), targetName(question.target));
    case tunnel::OpKind::Erase:
        return QCoreApplication::translate("hwio::remote", "Erase %1")
            .arg(targetName(question.target));
    case tunnel::OpKind::Recover:
        return QCoreApplication::translate("hwio::remote", "Recover %1")
            .arg(targetName(question.target));
    case tunnel::OpKind::Reset:
        return QCoreApplication::translate("hwio::remote", "Reset %1")
            .arg(targetName(question.target));
    case tunnel::OpKind::LteUpdate:
        return QCoreApplication::translate("hwio::remote", "Update the LTE firmware to %1")
            .arg(lteFirmwareName(question));
    }
    return {};
}

} // namespace hwio::remote
