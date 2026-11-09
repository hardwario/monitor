// The Probe card (plan/features/0035 § A, ADR-0252): title, Refresh, "Looking for J-Link probes…"
// and the probe rows with serial, product, firmware date and the "In use" / "Updating firmware…"
// tags. Used by the Choose device page and by the wizards' Embedded `probe-picker` step.
//   wizardMode: the picker is in wizard mode (rows used by another tab are disabled by the view
//               model); an empty list shows "No J-Link found…" inside the card.
//   readOnly:   every row is informational (no selection mark, not clickable).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: list

    required property ProbePickerViewModel picker
    property bool wizardMode: false
    property bool readOnly: false

    objectName: "probeList"
    implicitHeight: probeCard.implicitHeight + 2 * Theme.spacingL
    radius: Theme.radiusM + 2
    color: Theme.bg
    border.color: Theme.border

    ColumnLayout {
        id: probeCard

        anchors.fill: parent
        anchors.margins: Theme.spacingL
        spacing: Theme.spacingM

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: qsTr("Probe")
                font: Theme.fontUiStrong
                color: Theme.text
            }

            HToolButton {
                objectName: "refreshButton"
                iconName: "refresh"
                text: qsTr("Refresh")
                onClicked: list.picker.refresh()
            }
        }

        RowLayout {
            visible: !list.picker.scanned
            spacing: Theme.spacingS

            BusyIndicator {
                running: visible && !AppViewModel.reducedMotion
                implicitWidth: 20
                implicitHeight: 20
            }

            Label {
                text: qsTr("Looking for J-Link probes…")
                color: Theme.textMuted
            }
        }

        // Wizard mode: the empty list (the Choose device page shows its own empty state).
        Label {
            objectName: "probeListEmpty"
            Layout.fillWidth: true
            visible: list.wizardMode && list.picker.scanned && list.picker.count === 0
            text: qsTr("No J-Link found. Connect the J-Link to a USB port of this computer.")
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }

        Rectangle {
            Layout.fillWidth: true
            visible: list.picker.count > 0
            implicitHeight: probeColumn.implicitHeight + 2
            radius: Theme.radiusM
            color: "transparent"
            border.color: Theme.border
            clip: true

            ColumnLayout {
                id: probeColumn

                anchors.fill: parent
                anchors.margins: 1
                spacing: 0

                Repeater {
                    model: list.picker

                    delegate: ItemDelegate {
                        id: row

                        required property int index
                        required property string serial
                        required property string product
                        required property string firmware
                        required property int inUseTab
                        required property string inUseOther
                        required property bool selectable
                        required property bool updatingFirmware
                        readonly property bool selected: !list.readOnly && row.index === list.picker.selectedRow

                        objectName: "probeRow"
                        Layout.fillWidth: true
                        // Read-only: informational rows, not dimmed and not interactive.
                        enabled: list.readOnly || row.selectable
                        hoverEnabled: !list.readOnly
                        highlighted: row.selected
                        focusPolicy: list.readOnly ? Qt.NoFocus : Qt.StrongFocus
                        Accessible.role: list.readOnly ? Accessible.ListItem : Accessible.RadioButton
                        Accessible.checked: row.selected
                        Accessible.name: row.product + " " + row.serial
                        Accessible.description: tag.text
                        onClicked: {
                            if (!list.readOnly)
                                list.picker.choose(row.index);
                        }

                        background: Rectangle {
                            color: row.selected ? Theme.accentSoft : (row.hovered ? Theme.surface : "transparent")

                            Rectangle {
                                visible: row.index > 0
                                anchors.top: parent.top
                                width: parent.width
                                height: 1
                                color: Theme.border
                            }

                            FocusRing {
                                control: row
                                radiusOffset: 0
                            }
                        }

                        contentItem: RowLayout {
                            spacing: Theme.spacingM
                            opacity: row.enabled ? 1 : 0.6

                            Rectangle {
                                visible: !list.readOnly
                                implicitWidth: 16
                                implicitHeight: 16
                                radius: 8
                                color: row.selected ? Theme.accent : "transparent"
                                border.width: row.selected ? 4 : 2
                                border.color: row.selected ? Theme.accentSoft : Theme.textMuted

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 8
                                    color: "transparent"
                                    border.width: row.selected ? 1 : 0
                                    border.color: Theme.accent
                                }
                            }

                            Label {
                                textFormat: Text.PlainText
                                text: row.product
                                font: Theme.fontUiStrong
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.maximumWidth: 220
                            }

                            Label {
                                textFormat: Text.PlainText
                                objectName: "probeSerial"
                                text: row.serial
                                font: Theme.fontMono
                                color: Theme.text
                            }

                            Label {
                                textFormat: Text.PlainText
                                visible: row.firmware !== ""
                                text: qsTr("FW %1").arg(row.firmware)
                                font: Theme.fontUiSmall
                                color: Theme.textMuted
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                id: tag

                                textFormat: Text.PlainText
                                objectName: "probeInUse"
                                visible: tag.text !== ""
                                // Plan 0036: reserved for a J-Link firmware update.
                                text: row.updatingFirmware ? qsTr("Updating firmware…")
                                    : row.inUseOther !== "" ? qsTr("In use by another program (%1)").arg(row.inUseOther)
                                    : row.inUseTab > 0 ? qsTr("In use (tab %1)").arg(row.inUseTab) : ""
                                font: Theme.fontUiSmall
                                color: Theme.textMuted
                                elide: Text.ElideRight
                                Layout.maximumWidth: 320
                                leftPadding: 7
                                rightPadding: 7
                                background: Rectangle {
                                    radius: 10
                                    color: Theme.surface
                                    border.color: Theme.border
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
