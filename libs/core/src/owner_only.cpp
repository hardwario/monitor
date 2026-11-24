#include "hwio/core/owner_only.h"

#include "hwio/core/log.h"

#include <QFileDevice>
#include <QLoggingCategory>
#include <QtLogging>
#include <QtSystemDetection>

namespace hwio {

void restrictToOwner([[maybe_unused]] QFileDevice& file) {
#ifndef Q_OS_WIN
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        qCWarning(log::lcCore).noquote()
            << "Could not make the file owner-only:" << file.fileName() << file.errorString();
    }
#endif
}

} // namespace hwio
