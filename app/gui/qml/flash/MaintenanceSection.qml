// Maintenance (collapsible, collapsed by default; ADR-0049; amended by plan 0033, ADR-0236):
// [Reset] [Reset and halt] [Erase application] on the application MCU - no confirmation. With
// Settings → Advanced → "Show expert maintenance actions" also [Erase everything…] and [Recover…]
// (ConfirmDialog, ADR-0038, texts of the view model) and the choice Application MCU | LTE MCU
// above the buttons. [Erase application] exists only where the profile erases code only by
// default (not on the LTE MCU). The troubleshooting hand-off of target.protected shows [Recover…]
// once (vm.recoverAvailable) and unfolds the section (vm.maintenanceExpanded). Plan 0039: on an
// STM32 chip the two are [Erase chip…] and [Unlock (erase)…], always shown. Plan 0040: the expert
// choice includes the CHESTER LoRaWAN module, whose actions open the LoRaWAN module wizard (no
// [Reset and halt]: the guided flow restarts the module).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FlashCard {
    id: card

    required property FlashViewModel vm
    readonly property bool expanded: card.vm ? card.vm.maintenanceExpanded : false

    signal confirmRequested(bool recover)

    RowLayout {
        Layout.fillWidth: true

        AbstractButton {
            id: header

            objectName: "maintenanceHeader"
            Layout.fillWidth: true
            focusPolicy: Qt.StrongFocus
            checkable: true
            checked: card.expanded
            Accessible.role: Accessible.Button
            Accessible.name: card.vm ? card.vm.maintenanceTitle : ""
            Accessible.description: card.expanded ? qsTr("Collapse") : qsTr("Expand")
            onToggled: card.vm.maintenanceExpanded = header.checked

            contentItem: RowLayout {
                spacing: Theme.spacingS

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: card.vm ? card.vm.maintenanceTitle : ""
                    font: Theme.fontUiStrong
                    color: Theme.text
                    wrapMode: Text.Wrap
                }

                Icon {
                    symbol: card.expanded ? "collapse" : "expand"
                    color: Theme.text
                }
            }

            background: Item {
                FocusRing {
                    control: header
                    radiusOffset: Theme.radiusS
                }
            }
        }
    }

    // Expert actions only: the MCU of the actions below.
    Flow {
        objectName: "maintenanceTargets"
        Layout.fillWidth: true
        visible: card.expanded && card.vm !== null && card.vm.maintenanceTargets.length > 1
        spacing: Theme.spacingXs
        Accessible.role: Accessible.Grouping
        Accessible.name: qsTr("MCU")

        Repeater {
            model: card.vm ? card.vm.maintenanceTargets : []

            delegate: HButton {
                id: targetButton

                required property var modelData

                objectName: "maintenanceTarget_" + targetButton.modelData.targetId
                kind: card.vm.maintenanceTarget === targetButton.modelData.targetId ? "primary" : "normal"
                text: targetButton.modelData.title
                enabled: card.vm.canRunMaintenance
                Accessible.role: Accessible.RadioButton
                Accessible.checked: card.vm.maintenanceTarget === targetButton.modelData.targetId
                onClicked: card.vm.maintenanceTarget = targetButton.modelData.targetId
            }
        }
    }

    Flow {
        objectName: "maintenanceActions"
        Layout.fillWidth: true
        visible: card.expanded
        spacing: Theme.spacingS

        HButton {
            objectName: "resetButton"
            text: qsTr("Reset")
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.vm.reset(false)
        }

        HButton {
            objectName: "resetHaltButton"
            visible: card.vm ? card.vm.resetHaltAvailable : true
            text: qsTr("Reset and halt")
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.vm.reset(true)
        }

        HButton {
            objectName: "eraseCodeButton"
            visible: card.vm ? card.vm.eraseCodeAvailable : false
            text: qsTr("Erase application")
            toolTipText: qsTr("Erases the firmware only; the UICR configuration and the Product Information Block are kept.")
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.vm.eraseCode()
        }

        HButton {
            objectName: "eraseAllButton"
            visible: card.vm ? card.vm.eraseAllAvailable : false
            text: card.vm ? card.vm.eraseAllText : ""
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.confirmRequested(false)
        }

        HButton {
            objectName: "recoverButton"
            visible: card.vm ? card.vm.recoverAvailable : false
            text: card.vm ? card.vm.recoverText : ""
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.confirmRequested(true)
        }
    }
}
