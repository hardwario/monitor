#include "wizard/probe_picker_step.h"

#include "viewmodels/app_view_model.h"
#include "wizard/wizard_registry.h"

#include <QStringLiteral>
#include <QUrl>

namespace hwio::gui {

void registerProbePickerStep(AppViewModel& app) {
    (void)app.wizardRegistry().addEmbedded(
        probePickerStepName,
        QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/wizard/ProbePickerStep.qml")));
}

} // namespace hwio::gui
