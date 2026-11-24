// Windows identity (plan/features/0015 A3): the AppUserModelID of the process, the same as the
// Start-menu shortcut's System.AppUserModel.ID (packaging/windows/wix).

// NOMINMAX and WIN32_LEAN_AND_MEAN are set by app/gui/CMakeLists.txt.
#include <windows.h>
// <windows.h> must come first: <shobjidl_core.h> needs its types. This comment ends the include
// block, so clang-format (IncludeBlocks: Regroup) does not sort it below.
#include "gui_log.h"
#include "platform.h"

#include <QLoggingCategory>
#include <QtLogging>

#include <cstdio>
#include <shobjidl_core.h>

namespace hwio::gui {

void applyPlatformIdentity() {
    const HRESULT result = SetCurrentProcessExplicitAppUserModelID(L"com.hardwario.monitor");
    if (FAILED(result)) {
        qCWarning(lcGui) << "SetCurrentProcessExplicitAppUserModelID failed:"
                         << static_cast<long>(result);
    }
}

void attachParentConsole() {
    // Redirected stdout (a pipe or a file, for example `hwio-monitor --version > v.txt`) is
    // inherited and used as it is.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        return; // no parent console (started from Explorer): nothing is shown
    }
    FILE* stream = nullptr;
    if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0) {
        qCWarning(lcGui) << "stdout could not be attached to the parent console";
    }
}

} // namespace hwio::gui
