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
        // Plan 0046 (ADR-0366): the customer confirms before the application is erased.
        {.id = "confirm",
         .text = QT_TRANSLATE_NOOP("hwio::remote", "waiting for the customer to continue")},
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
