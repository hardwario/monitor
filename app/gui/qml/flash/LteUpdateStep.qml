// The Embedded step "lte-update" of the LTE firmware update wizard (plan/features/0033-rc2-feedback
// .md C, ADR-0234), bound to LteUpdateWizard (`vm`). Shows by `vm.stage`: the LTE firmware source
// (SourceCard, nRF Util prerequisite), a connector prompt (the dialog shows the picture), the
// running operation (prompt + PhaseStepper), the backup problem, the application choice (the copy
// or another firmware) or the failure. Presentation only.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: step

    property LteUpdateWizard vm: null
    readonly property int stage: step.vm ? step.vm.stage : LteUpdateWizard.Firmware

    objectName: "lteUpdateStep"
    spacing: Theme.spacingM

    // The prompt of the step: the intro, "Connect the J-Link cable to the APP connector.", ...
    Label {
        objectName: "ltePrompt"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: text !== "" && step.stage !== LteUpdateWizard.Failed
        text: step.vm ? step.vm.promptText : ""
        color: step.vm && step.vm.wrongConnector ? Theme.warning : Theme.text
        font: step.stage === LteUpdateWizard.Connector ? Theme.fontUiStrong : Theme.fontUi
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    // Plan 0034 (B4): the missing prerequisite above the firmware card, visible without scrolling
    // at 960 x 600 (it is why Next is disabled).
    Rectangle {
        objectName: "ltePrerequisite"
        Layout.fillWidth: true
        visible: step.stage === LteUpdateWizard.Firmware && step.vm !== null && step.vm.prerequisiteText !== ""
        implicitHeight: prerequisiteRow.implicitHeight + 2 * Theme.spacingS
        radius: Theme.radiusS
        color: Theme.errorPanelBg

        // Plan 0038 A0: the buttons wrap below the message (German, minimum window size).
        ColumnLayout {
            id: prerequisiteRow

            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Icon {
                    symbol: "error"
                    color: Theme.error
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: step.vm ? step.vm.prerequisiteText : ""
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                HButton {
                    objectName: "prerequisiteSettings"
                    text: qsTr("Set path in Settings")
                    onClicked: AppViewModel.openSettings("nrfutil")
                }

                // Plan 0038 A0 (ADR-0263): Nordic's direct download for this OS, then our guide.
                HButton {
                    objectName: "nrfUtilDownload"
                    kind: "ghost"
                    toolTipText: step.vm ? step.vm.nrfUtilDownloadUrl : ""
                    text: qsTr("Download nRF Util ↗")
                    onClicked: Qt.openUrlExternally(step.vm.nrfUtilDownloadUrl)
                }

                HButton {
                    objectName: "nrfUtilInstall"
                    kind: "ghost"
                    text: qsTr("How to install ↗")
                    onClicked: Qt.openUrlExternally(AppViewModel.docsLink("install-nrf-util"))
                }
            }
        }
    }

    // Step 1: the LTE firmware.
    SourceCard {
        objectName: "lteSourceCard"
        Layout.fillWidth: true
        number: 0 // plan 0034 (B4): the Flash page's step number, not the wizard's
        visible: step.stage === LteUpdateWizard.Firmware && step.vm !== null
        source: step.vm ? step.vm.lteSource : null
        onBrowseRequested: {
            fileDialog.target = step.vm.lteSource;
            fileDialog.open();
        }
    }

    // Steps 3, 5 and 7 while an operation runs (Cancel is the dialog's, disabled during a flash).
    PhaseStepper {
        objectName: "lteProgress"
        Layout.fillWidth: true
        visible: step.stage === LteUpdateWizard.Working
        phases: step.vm ? step.vm.phases : []
        progress: step.vm ? step.vm.progress : -1
        detailText: step.vm ? step.vm.stepText : ""
        cancelVisible: false
    }

    // Step 3: the copy could not be made; step 7 / failure: what went wrong.
    ErrorPanel {
        objectName: "lteError"
        Layout.fillWidth: true
        visible: (step.stage === LteUpdateWizard.BackupFailed || step.stage === LteUpdateWizard.Failed)
                 && step.vm !== null && step.vm.error.valid
        error: step.vm ? step.vm.error : noError
        primaryAction: ErrorAction.Dismiss
        property errorInfo noError
    }

    // Step 7: the copy or another firmware.
    ColumnLayout {
        objectName: "lteApplication"
        Layout.fillWidth: true
        visible: step.stage === LteUpdateWizard.Application
        spacing: Theme.spacingS

        RadioButton {
            objectName: "useBackup"
            visible: step.vm !== null && step.vm.hasBackup
            text: qsTr("The application that was in the device")
            checked: step.vm !== null && step.vm.useBackup
            onToggled: step.vm.useBackup = true
        }

        Label {
            objectName: "backupPath"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingXl
            visible: step.vm !== null && step.vm.hasBackup
            text: step.vm ? step.vm.backupText : ""
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.WrapAnywhere
        }

        RadioButton {
            objectName: "useOtherFirmware"
            visible: step.vm !== null && step.vm.hasBackup
            text: qsTr("Firmware from the catalog or a file")
            checked: step.vm !== null && !step.vm.useBackup
            onToggled: step.vm.useBackup = false
        }

        SourceCard {
            objectName: "appSourceCard"
            Layout.fillWidth: true
            number: 0 // plan 0034 (B4): no Flash page step number inside the wizard
            visible: step.vm !== null && !step.vm.useBackup
            source: step.vm ? step.vm.appSource : null
            onBrowseRequested: {
                fileDialog.target = step.vm.appSource;
                fileDialog.open();
            }
        }
    }

    FileDialog {
        id: fileDialog

        property FirmwareSourceViewModel target: null

        title: qsTr("Choose a firmware file")
        fileMode: FileDialog.OpenFile
        nameFilters: fileDialog.target ? fileDialog.target.nameFilters : []
        onAccepted: fileDialog.target.loadFile(fileDialog.selectedFile)
    }
}
