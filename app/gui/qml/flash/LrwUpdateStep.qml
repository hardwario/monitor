// The Embedded step "lrw-update" of the LoRaWAN module wizard (plan/features/0040-chester-lrw-module
// .md B1, B2; ADR-0352), bound to LrwUpdateWizard (`vm`). Shows by `vm.stage`: a connector prompt
// (the dialog shows the picture), the running operation (prompt + PhaseStepper), the unlock
// confirmation of a read-out protected module or the failure. Presentation only.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: step

    property LrwUpdateWizard vm: null
    readonly property int stage: step.vm ? step.vm.stage : LrwUpdateWizard.Connector

    objectName: "lrwUpdateStep"
    spacing: Theme.spacingM

    // The prompt of the step: "Connect the J-Link cable to the APP connector.", ...
    Label {
        objectName: "lrwPrompt"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: text !== "" && step.stage !== LrwUpdateWizard.Failed
                 && step.stage !== LrwUpdateWizard.UnlockConfirmation
        text: step.vm ? step.vm.promptText : ""
        font: step.stage === LrwUpdateWizard.Connector ? Theme.fontUiStrong : Theme.fontUi
        color: Theme.text
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    // A read-out protected module (RDP 1, e.g. the factory Murata firmware): the unlock erases it.
    Rectangle {
        objectName: "lrwUnlockWarning"
        Layout.fillWidth: true
        visible: step.stage === LrwUpdateWizard.UnlockConfirmation
        implicitHeight: warningRow.implicitHeight + 2 * Theme.spacingS
        radius: Theme.radiusS
        color: Theme.warningPanelBg
        Accessible.role: Accessible.AlertMessage
        Accessible.name: step.vm ? step.vm.promptText : ""

        RowLayout {
            id: warningRow

            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            Icon {
                Layout.alignment: Qt.AlignTop
                symbol: "warning"
                color: Theme.warning
            }

            Label {
                objectName: "lrwUnlockText"
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: step.vm ? step.vm.promptText : ""
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }
    }

    // While an operation runs (Cancel is the dialog's, disabled meanwhile).
    PhaseStepper {
        objectName: "lrwProgress"
        Layout.fillWidth: true
        visible: step.stage === LrwUpdateWizard.Working
        phases: step.vm ? step.vm.phases : []
        progress: step.vm ? step.vm.progress : -1
        detailText: ""
        cancelVisible: false
    }

    // What went wrong ([Try again] is the dialog's primary button when the procedure can retry).
    ErrorPanel {
        objectName: "lrwError"
        Layout.fillWidth: true
        visible: step.stage === LrwUpdateWizard.Failed && step.vm !== null && step.vm.error.valid
        error: step.vm ? step.vm.error : noError
        primaryAction: ErrorAction.Dismiss
        property errorInfo noError
    }
}
