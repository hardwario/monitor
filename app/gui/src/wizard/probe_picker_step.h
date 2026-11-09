#pragma once

// The Embedded `probe-picker` wizard step (plan/features/0035 § A, ADR-0252): ProbePickerStep.qml
// shows the probe list of a ProbePickerViewModel in wizard mode (components/ProbeList.qml, shared
// with the Choose device page) and the wizard's check rows. A wizard creates its picker with
// AppViewModel::makeProbePicker(this), returns it from Wizard::embeddedViewModel() while the step
// is current and keeps it `active` only then (the scanner runs while a picker is visible,
// ADR-0093).

#include <QString>
#include <QStringLiteral>

namespace hwio::gui {

class AppViewModel;

// The name of the Embedded step (WizardStep::embedded).
inline const QString probePickerStepName = QStringLiteral("probe-picker");

// Registers the step's QML control with the WizardRegistry (called once at start-up from
// registerBuiltInPages; a second call does nothing).
void registerProbePickerStep(AppViewModel& app);

} // namespace hwio::gui
