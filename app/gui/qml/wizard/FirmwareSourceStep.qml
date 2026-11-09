// The Embedded step "firmware-source" of the flash wizard (plan/features/0010 step 4, amended by
// 0035), bound to FlashWizard (`vm`): the explanation of the error the wizard was opened for (or
// of an image that does not fit the application MCU) with its buttons, the Flash page's source
// card bound to the wizard's own FirmwareSourceViewModel (application firmware), "Allow writing
// UICR" only when the image has UICR records (ADR-0036) and the link to the LTE firmware update
// (ADR-0234, ADR-0253). Presentation only.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: step

    property FlashWizard vm: null

    objectName: "firmwareSourceStep"
    spacing: Theme.spacingM

    // The start-point explanation (plan 0010 § FlashFirmware errors).
    Rectangle {
        objectName: "flashExplanation"
        Layout.fillWidth: true
        visible: step.vm !== null && step.vm.explanation !== ""
        implicitHeight: explanationColumn.implicitHeight + 2 * Theme.spacingS
        radius: Theme.radiusS
        color: Theme.errorPanelBg

        ColumnLayout {
            id: explanationColumn

            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Icon {
                    Layout.alignment: Qt.AlignTop
                    symbol: "warning"
                    color: Theme.warning
                }

                Label {
                    objectName: "flashExplanationText"
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: step.vm ? step.vm.explanation : ""
                    color: Theme.text
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: text
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                visible: explanationLte.visible || explanationCatalog.visible

                HButton {
                    id: explanationLte

                    objectName: "explanationLteUpdate"
                    visible: step.vm !== null && step.vm.explanationOffersLteUpdate
                    text: qsTr("Update LTE firmware…")
                    onClicked: step.vm.updateLteFirmware()
                }

                HButton {
                    id: explanationCatalog

                    objectName: "explanationCatalog"
                    visible: step.vm !== null && step.vm.explanationOffersCatalog
                    kind: "ghost"
                    text: qsTr("Open firmware catalog ↗")
                    Accessible.name: qsTr("Open firmware catalog (opens browser)")
                    onClicked: step.vm.openCatalog()
                }
            }
        }
    }

    SourceCard {
        objectName: "wizardSourceCard"
        Layout.fillWidth: true
        number: 0 // no Flash page step number inside the wizard
        source: step.vm ? step.vm.source : null
        onBrowseRequested: fileDialog.open()
    }

    // ADR-0036: only when the image has UICR records ("Halt after flashing" is a Flash page option).
    ColumnLayout {
        objectName: "wizardUicr"
        Layout.fillWidth: true
        visible: step.vm !== null && step.vm.source.hasUicr
        spacing: Theme.spacingXs

        CheckBox {
            objectName: "wizardUicrOption"
            text: qsTr("Allow writing UICR")
            checked: step.vm !== null && step.vm.source.allowUicr
            onToggled: step.vm.source.allowUicr = checked
        }

        Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingXl
            text: qsTr("The firmware contains UICR configuration. Writing it changes the device configuration; the Product Information Block is kept.")
            font: Theme.fontUiSmall
            color: Theme.warning
            wrapMode: Text.Wrap
        }
    }

    // ADR-0253: LTE firmware goes through the LTE firmware update wizard.
    Flow {
        objectName: "wizardLteLink"
        Layout.fillWidth: true
        visible: step.vm !== null && step.vm.lteUpdateAvailable
        spacing: Theme.spacingS

        Label {
            textFormat: Text.PlainText
            height: lteLinkButton.height
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Updating the LTE firmware?")
            color: Theme.textMuted
        }

        HButton {
            id: lteLinkButton

            objectName: "wizardLteUpdate"
            kind: "ghost"
            text: qsTr("Update LTE firmware…")
            onClicked: step.vm.updateLteFirmware()
        }
    }

    FileDialog {
        id: fileDialog

        title: qsTr("Choose a firmware file")
        fileMode: FileDialog.OpenFile
        nameFilters: step.vm ? step.vm.source.nameFilters : []
        onAccepted: step.vm.source.loadFile(fileDialog.selectedFile)
    }
}
