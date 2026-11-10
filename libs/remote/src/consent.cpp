#include "hwio/remote/consent.h"

#include "hwio/core/json.h"
#include "hwio/remote/tunnel.h"

#include <QCoreApplication>
#include <QString>

#include <string>

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

} // namespace

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
    }
    return {};
}

} // namespace hwio::remote
