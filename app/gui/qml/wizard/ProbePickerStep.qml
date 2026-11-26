// The Embedded wizard step "probe-picker" (plan/features/0035 § A, ADR-0252), bound to the wizard's
// ProbePickerViewModel in wizard mode (`vm`, AppViewModel::makeProbePicker): the shared probe list
// (rows used by another tab or another program are disabled, no tab switch, no Connect) and the
// wizard's check rows below it (`probes`, `usb-access`, `probe-holders`, ... - the wizard decides).
// The J-Link Software error panel of the Choose device page is replaced by those checks.
// Presentation only.
pragma ComponentBehavior: Bound

import QtQuick
import HardwarioMonitor

// The root is a plain Item with a Column (no Layout): its implicit width stays 0, so the wrapped
// check texts (whose width follows the dialog's width) do not feed back into the dialog's layout
// ("Detected recursive rearrange").
Item {
    id: step

    property ProbePickerViewModel vm: null
    // Plan 0056 (embedded contract): the WizardViewModel showing this control (set by WizardView).
    property WizardViewModel host: null
    // Plan 0008 step 5: informational rows only (no selection).
    property bool readOnly: false
    readonly property CheckListModel checks: step.host !== null ? step.host.checks : null

    objectName: "probePickerStep"
    implicitHeight: column.implicitHeight

    Column {
        id: column

        width: step.width
        spacing: Theme.spacingM

        Loader {
            width: column.width
            active: step.vm !== null
            visible: active

            sourceComponent: ProbeList {
                picker: step.vm
                wizardMode: true
                readOnly: step.readOnly
            }
        }

        CheckList {
            objectName: "probePickerChecks"
            width: column.width
            visible: step.checks !== null && step.checks.count > 0
            model: step.checks
        }
    }
}
