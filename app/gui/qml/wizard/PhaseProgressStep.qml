// The Embedded step "phase-progress" of the flash wizard (plan/features/0010 steps 6 and 7), bound
// to FlashWizard (`vm`): while flashing the operation title and the PhaseStepper (phases of
// OperationProgress::plannedPhases, bytes, percent, elapsed, Cancel); after a failure the
// interrupted-flash note (ADR-0109) and the ErrorPanel of the error. The wizard's footer has
// [Try again] and [Help me fix this]. Presentation only.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: step

    property FlashWizard vm: null
    property errorInfo noError // default-constructed: invalid (no error)
    readonly property bool flashing: step.vm !== null && step.vm.stage === FlashWizard.Flashing
    readonly property bool failed: step.vm !== null && step.vm.stage === FlashWizard.Failed

    objectName: "phaseProgressStep"
    spacing: Theme.spacingM

    Label {
        objectName: "flashOperationTitle"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: step.flashing
        text: step.vm ? step.vm.operationTitle : ""
        font: Theme.fontUiStrong
        color: Theme.text
        wrapMode: Text.Wrap
    }

    PhaseStepper {
        objectName: "flashPhaseStepper"
        Layout.fillWidth: true
        visible: step.flashing
        phases: step.vm ? step.vm.phases : []
        progress: step.vm ? step.vm.progress : -1
        detailText: step.vm ? step.vm.progressText : ""
        cancelVisible: step.vm !== null && (step.vm.cancelAvailable || step.vm.cancelling)
        cancelEnabled: step.vm !== null && step.vm.cancelAvailable
        onCancelRequested: step.vm.cancelFlash()
    }

    Label {
        objectName: "flashResultText"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: step.failed && text !== ""
        text: step.vm ? step.vm.resultText : ""
        font: Theme.fontUiStrong
        color: Theme.text
        wrapMode: Text.Wrap
    }

    ErrorPanel {
        objectName: "flashError"
        Layout.fillWidth: true
        visible: step.failed && step.vm.error.valid
        error: step.vm ? step.vm.error : step.noError
        onActionTriggered: action => step.vm.triggerErrorAction(action)
    }
}
