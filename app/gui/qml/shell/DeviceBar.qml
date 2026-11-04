// Device bar of a tab (ADR-0043, ADR-0052, ADR-0053; ADR-0233: no target box): probe, state text
// ("Connected", "Connecting…", "Reconnecting… (target not powered)", the busy page's text),
// Connect / Cancel / Disconnect and the ⋯ Advanced popover. Plan 0012: a Bluetooth tab shows the
// device chip (name · RSSI) with the Bluetooth icon. Plan 0014: Cancel of a running AI operation.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: bar

    required property DeviceTabViewModel tab
    readonly property ConnectionViewModel connection: bar.tab.connection
    readonly property bool choosing: bar.tab.state === DeviceTabViewModel.ChoosingDevice

    implicitHeight: Theme.deviceBarHeight
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingM

        Label {
            objectName: "chooseDeviceLabel"
            visible: bar.choosing
            text: qsTr("Choose device")
            color: Theme.textMuted
        }

        // Probe chip.
        Rectangle {
            objectName: "probeChip"
            visible: !bar.choosing
            Layout.maximumWidth: 280
            Layout.minimumWidth: 120
            Layout.preferredWidth: probeRow.implicitWidth + 2 * Theme.spacingM
            implicitHeight: 30
            radius: Theme.radiusM
            color: Theme.bg
            border.color: Theme.border
            Accessible.role: Accessible.StaticText
            Accessible.name: bar.connection.isBle ? qsTr("Bluetooth device %1").arg(bar.connection.probeLabel)
                                                  : qsTr("Probe %1").arg(bar.connection.probeLabel)

            RowLayout {
                id: probeRow

                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingM
                spacing: Theme.spacingS

                Label {
                    visible: !bar.connection.isBle
                    text: qsTr("Probe")
                    color: Theme.textMuted
                }

                Icon {
                    visible: bar.connection.isBle
                    symbol: "bluetooth"
                    color: Theme.textMuted
                    size: 18
                }

                Label {
                    textFormat: Text.PlainText
                    objectName: "probeLabel"
                    Layout.fillWidth: true
                    text: bar.connection.probeLabel
                    color: Theme.text
                    elide: Text.ElideMiddle
                }
            }
        }

        RowLayout {
            visible: !bar.choosing
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            spacing: Theme.spacingS

            StateDot {
                kind: bar.connection.stateKind
            }

            BusyIndicator {
                visible: bar.connection.stateKind === "connecting" || bar.connection.stateKind === "reconnecting"
                running: visible && !AppViewModel.reducedMotion
                implicitWidth: 18
                implicitHeight: 18
            }

            Label {
                id: stateLabel

                textFormat: Text.PlainText
                objectName: "stateText"
                Layout.fillWidth: true
                text: bar.connection.stateText
                color: bar.connection.stateKind === "error" ? Theme.error : Theme.textMuted
                // Two lines rather than an elided cause (Czech at 960 px, review S13).
                wrapMode: Text.Wrap
                maximumLineCount: 2
                lineHeight: 0.95
                elide: Text.ElideRight
                Accessible.role: Accessible.StatusBar
                Accessible.name: stateLabel.text
                onTextChanged: if (bar.tab === AppViewModel.currentTabViewModel) Accessible.announce(stateLabel.text)
                Accessible.description: bar.connection.pairingPending ? bar.connection.passkeyHint : ""
                HToolTip {
                    // Plan 0012: while the system asks for the passkey, the profile's hint.
                    visible: stateHover.hovered && (stateLabel.truncated || bar.connection.pairingPending)
                    text: bar.connection.pairingPending ? stateLabel.text + "\n" + bar.connection.passkeyHint : stateLabel.text
                }

                HoverHandler {
                    id: stateHover
                }
            }
        }

        Item {
            visible: bar.choosing
            Layout.fillWidth: true
        }

        HButton {
            objectName: "chooseAnotherProbe"
            visible: !bar.choosing && bar.connection.canChooseAnotherProbe
            text: bar.connection.isBle ? qsTr("Choose another device") : qsTr("Choose another probe")
            onClicked: bar.connection.chooseAnotherProbe()
        }

        // Plan 0014 (ADR-0105, ADR-0244): cancels a running AI operation (the tool call ends with
        // `cancelled`).
        HButton {
            objectName: "aiCancel"
            visible: bar.tab.busyPage === "ai"
            text: qsTr("Cancel")
            Accessible.name: qsTr("Cancel the AI operation")
            onClicked: McpController.cancelOperation(bar.tab)
        }

        HButton {
            objectName: "connectionAction"
            visible: !bar.choosing
            kind: bar.connection.action === "connect" ? "primary" : "normal"
            enabled: bar.connection.actionEnabled
            text: bar.connection.action === "connect" ? qsTr("Connect")
                : bar.connection.action === "cancel" ? qsTr("Cancel") : qsTr("Disconnect")
            toolTipText: AppViewModel.shortcuts.tooltip(text, "device.toggle")
            onClicked: bar.connection.triggerAction()
        }

        HToolButton {
            id: moreButton

            objectName: "advancedButton"
            visible: !bar.choosing
            iconName: "more"
            text: qsTr("Advanced")
            display: AbstractButton.IconOnly
            checked: popover.visible
            onClicked: popover.visible ? popover.close() : popover.open()
        }
    }

    AdvancedPopover {
        id: popover

        connection: bar.connection
        x: bar.width - popover.width - Theme.spacingM
        y: bar.height - Theme.spacingXs
    }
}
