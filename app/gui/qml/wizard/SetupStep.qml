// The control "setup-step" of the setup wizard (plan/features/0008, SetupWizard): shown by
// WizardView below the step's own content. Welcome: the language (ADR-0254), the user mode cards
// while the mode is not chosen (plan 0056, ADR-0392) and "Don't show at start" (ADR-0071).
// J-Link / USB access / nRF Util: the result with versions and paths (plain text), the commands
// with [Copy], "It is installed elsewhere…" (file dialog) and "Use automatic detection". Done:
// the summary. A read-only banner while the settings are read-only.
// Presentation only: `vm` is the SetupWizard, `host` its WizardViewModel.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HardwarioMonitor

// A plain Item with a Column (as ProbePickerStep): the wrapped texts follow the dialog's width
// without feeding back into its layout.
Item {
    id: step

    property SetupWizard vm: null
    // Plan 0056 (embedded contract): the WizardViewModel showing this control (set by WizardView).
    property WizardViewModel host: null
    readonly property string stepId: step.vm !== null ? step.vm.stepId : ""

    objectName: "setupStep"
    implicitHeight: column.implicitHeight

    Column {
        id: column

        width: step.width
        spacing: Theme.spacingM

        WarningBanner {
            id: readOnlyBanner

            property bool dismissedByUser: false

            objectName: "setupReadOnlyBanner"
            width: column.width
            visible: step.vm !== null && step.vm.settingsReadOnly && !readOnlyBanner.dismissedByUser
            text: qsTr("Settings were created by a newer version and are read-only. The setup cannot save anything.")
            onDismissed: readOnlyBanner.dismissedByUser = true
        }

        RowLayout {
            objectName: "setupLanguageRow"
            width: column.width
            visible: step.stepId === "welcome"
            spacing: Theme.spacingM

            Label {
                textFormat: Text.PlainText
                text: qsTr("Language")
                color: Theme.text
            }

            ComboBox {
                objectName: "setupLanguage"
                Layout.preferredWidth: 240
                textRole: "name"
                valueRole: "code"
                model: step.vm !== null ? step.vm.languages : []
                currentIndex: step.vm !== null ? step.vm.languageIndex : 0
                Accessible.name: qsTr("Language")
                onActivated: index => step.vm.selectLanguage(index)
            }
        }

        // Plan 0056 (ADR-0392): the user mode on Welcome while it is not chosen.
        ModeCards {
            objectName: "setupModeCards"
            width: column.width
            visible: step.vm !== null && step.vm.modeChoice
            modes: step.vm !== null ? step.vm.modes : []
            selected: step.vm !== null ? step.vm.selectedMode : UiMode.Advanced
            onModeSelected: mode => step.vm.selectMode(mode)
        }

        CheckBox {
            objectName: "setupDontShow"
            visible: step.vm !== null && step.vm.showAtStartChoice
            enabled: step.vm !== null && !step.vm.settingsReadOnly
            text: qsTr("Don't show at start")
            checked: step.vm !== null && !step.vm.showAtStart
            onToggled: step.vm.showAtStart = !checked
        }

        Label {
            objectName: "setupStatus"
            width: column.width
            visible: text !== ""
            textFormat: Text.PlainText
            text: step.vm !== null ? step.vm.statusText : ""
            color: Theme.text
            wrapMode: Text.Wrap
        }

        Label {
            objectName: "setupPathError"
            width: column.width
            visible: text !== ""
            textFormat: Text.PlainText
            text: step.vm !== null ? step.vm.pathError : ""
            color: Theme.error
            wrapMode: Text.Wrap
        }

        CommandBlock {
            objectName: "setupCommands"
            width: column.width
            visible: step.vm !== null && step.vm.commands.length > 0
            commands: step.vm !== null ? step.vm.commands : []
            onCopyRequested: text => step.host.copyText(text)
        }

        Flow {
            width: column.width
            visible: step.vm !== null && step.vm.buttons.length > 0
            spacing: Theme.spacingS

            Repeater {
                model: step.vm !== null ? step.vm.buttons : []

                delegate: HButton {
                    required property var modelData

                    objectName: "setupButton"
                    text: modelData.text
                    onClicked: {
                        if (modelData.file)
                            fileDialog.open();
                        else
                            step.host.action(modelData.id);
                    }
                }
            }
        }

        CheckList {
            objectName: "setupSummary"
            width: column.width
            visible: step.stepId === "done" && step.vm !== null
            model: step.vm !== null ? step.vm.summary : null
        }
    }

    FileDialog {
        id: fileDialog

        title: step.vm !== null ? step.vm.fileDialogTitle : ""
        nameFilters: step.vm !== null ? step.vm.fileNameFilters : []
        fileMode: FileDialog.OpenFile
        onAccepted: step.vm.choosePath(fileDialog.selectedFile)
    }
}
