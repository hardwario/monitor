// "Choose device" page of a new tab (ADR-0050): the live probe list (refreshed every 2 s while
// visible), the device picker (ADR-0233: no target choice) and Connect. Empty state when no probe is found, an
// ErrorPanel when the J-Link Software is missing. Plan 0012 (ADR-0081): a "Connect via J-Link |
// Bluetooth" switch on top; the Bluetooth view is BleDeviceList.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: page

    required property DeviceTabViewModel tab
    required property ProbePickerViewModel picker
    property bool active: false
    readonly property bool bluetooth: page.tab.transport === "ble"
    readonly property bool libraryMissing: page.picker.libraryError.valid
    readonly property bool noProbes: page.picker.scanned && page.picker.count === 0 && !page.libraryMissing

    Binding {
        target: page.picker
        property: "active"
        value: page.active && !page.bluetooth
    }

    Binding {
        target: page.tab.blePicker
        property: "active"
        value: page.active && page.bluetooth
    }

    // Connect via [J-Link | Bluetooth] (shown when the application has Bluetooth).
    Rectangle {
        id: transportBar

        objectName: "transportBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        visible: page.tab.bluetoothAvailable
        height: transportBar.visible ? transportRow.implicitHeight + 2 * Theme.spacingM : 0
        color: "transparent"

        RowLayout {
            id: transportRow

            x: Math.max(Theme.spacingXl, (transportBar.width - width) / 2)
            width: Math.min(820, transportBar.width - 2 * Theme.spacingXl)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingM

            Label {
                text: qsTr("Connect via")
                color: Theme.textMuted
            }

            Row {
                objectName: "transportSwitch"
                spacing: 0
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Connect via")

                HButton {
                    objectName: "transportJLink"
                    kind: !page.bluetooth ? "primary" : "normal"
                    iconName: "probe"
                    text: qsTr("J-Link")
                    checkable: true
                    checked: !page.bluetooth
                    Accessible.role: Accessible.RadioButton
                    onClicked: page.tab.transport = "jlink"
                }

                HButton {
                    objectName: "transportBluetooth"
                    kind: page.bluetooth ? "primary" : "normal"
                    iconName: "bluetooth"
                    text: qsTr("Bluetooth")
                    checkable: true
                    checked: page.bluetooth
                    Accessible.role: Accessible.RadioButton
                    onClicked: page.tab.transport = "ble"
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    BleDeviceList {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: transportBar.bottom
        anchors.bottom: parent.bottom
        visible: page.bluetooth
        picker: page.tab.blePicker
    }

    Item {
        id: jlinkView

        objectName: "jlinkView"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: transportBar.bottom
        anchors.bottom: parent.bottom
        visible: !page.bluetooth

        // Empty state: no probe found.
        EmptyState {
            objectName: "noProbesState"
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.spacingXl, 520)
            visible: page.noProbes
            iconName: "probe"
            headline: qsTr("No J-Link probe found")
            text: qsTr("Connect the J-Link to USB and to the device.")
            actionText: qsTr("Refresh")
            onActionTriggered: page.picker.refresh()

            RowLayout {
                spacing: Theme.spacingS

                Icon {
                    symbol: page.picker.libraryVersion !== "" ? "success" : "error"
                    color: page.picker.libraryVersion !== "" ? Theme.success : Theme.error
                    size: 18
                }

                Label {
                    textFormat: Text.PlainText
                    objectName: "checkLibrary"
                    text: page.picker.libraryVersion !== ""
                          ? qsTr("J-Link Software found (%1)").arg(page.picker.libraryVersion)
                          : qsTr("J-Link Software not found")
                    color: Theme.text
                }
            }

            RowLayout {
                spacing: Theme.spacingS

                Icon {
                    symbol: "pending"
                    color: Theme.textMuted
                    size: 18
                }

                Label {
                    text: qsTr("Is the USB cable connected?")
                    color: Theme.textMuted
                }
            }

            // Disabled until the M4 Connect-device wizard exists.
            Item {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: helpButton.implicitWidth
                implicitHeight: helpButton.implicitHeight

                HButton {
                    id: helpButton

                    objectName: "noProbesHelp"
                    enabled: false
                    text: qsTr("Help me fix this")
                    Accessible.description: qsTr("Coming soon")
                }

                HoverHandler {
                    id: helpHover
                }

                HToolTip {
                    visible: helpHover.hovered
                    text: qsTr("Coming soon")
                }
            }
        }

        ScrollView {
            id: scroll

            anchors.fill: parent
            visible: !page.noProbes
            contentWidth: availableWidth

            ColumnLayout {
                x: Math.max(Theme.spacingXl, (scroll.availableWidth - width) / 2)
                width: Math.min(820, scroll.availableWidth - 2 * Theme.spacingXl)
                spacing: Theme.spacingL

                Item {
                    implicitHeight: Theme.spacingS
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Label {
                        objectName: "chooseTitle"
                        Layout.fillWidth: true
                        text: qsTr("Choose a device to connect")
                        font: Theme.fontTitle
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("J-Link probes connected to this computer. The list refreshes automatically.")
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                    }
                }

                ErrorPanel {
                    objectName: "libraryError"
                    Layout.fillWidth: true
                    error: page.picker.libraryError
                    primaryAction: ErrorAction.OpenSettings
                    onActionTriggered: AppViewModel.openSettings("jlink")
                }

                // Probe list card.
                Rectangle {
                    Layout.fillWidth: true
                    visible: !page.libraryMissing
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
                                onClicked: page.picker.refresh()
                            }
                        }

                        RowLayout {
                            visible: !page.picker.scanned
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

                        Rectangle {
                            Layout.fillWidth: true
                            visible: page.picker.count > 0
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
                                    model: page.picker

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
                                        readonly property bool selected: row.index === page.picker.selectedRow

                                        objectName: "probeRow"
                                        Layout.fillWidth: true
                                        enabled: row.selectable
                                        highlighted: row.selected
                                        focusPolicy: Qt.StrongFocus
                                        Accessible.role: Accessible.RadioButton
                                        Accessible.checked: row.selected
                                        Accessible.name: row.product + " " + row.serial
                                        Accessible.description: tag.text
                                        onClicked: page.picker.choose(row.index)

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

                // Device card (ADR-0233: no target choice).
                Rectangle {
                    Layout.fillWidth: true
                    visible: !page.libraryMissing
                    implicitHeight: deviceCard.implicitHeight + 2 * Theme.spacingL
                    radius: Theme.radiusM + 2
                    color: Theme.bg
                    border.color: Theme.border

                    ColumnLayout {
                        id: deviceCard

                        anchors.fill: parent
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingM

                        Label {
                            text: qsTr("Device")
                            font: Theme.fontUiStrong
                            color: Theme.text
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingM

                            ComboBox {
                                id: profileBox

                                objectName: "profileBox"
                                visible: page.picker.profileChoice
                                implicitHeight: Theme.controlHeight
                                model: page.picker.profiles
                                textRole: "name"
                                valueRole: "id"
                                currentIndex: profileBox.indexOfValue(page.picker.profileId)
                                Accessible.name: qsTr("Device")
                                onActivated: page.picker.profileId = profileBox.currentValue

                                FocusRing {
                                    control: profileBox
                                    radiusOffset: Theme.radiusS
                                }
                            }

                            // Plan 0033 (D1): one J-Link profile - its name instead of the picker.
                            Label {
                                objectName: "profileName"
                                textFormat: Text.PlainText
                                visible: !page.picker.profileChoice
                                height: Theme.controlHeight
                                verticalAlignment: Text.AlignVCenter
                                text: page.picker.profileName
                                color: Theme.text
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                Layout.fillWidth: true
                                visible: page.picker.count > 0 && !page.picker.canConnect
                                text: qsTr("Choose a free probe in the list above.")
                                color: Theme.textMuted
                                wrapMode: Text.Wrap
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            HButton {
                                objectName: "connectButton"
                                kind: "primary"
                                enabled: page.picker.canConnect
                                text: qsTr("Connect")
                                onClicked: page.picker.connectSelected()
                            }
                        }
                    }
                }

                Item {
                    implicitHeight: Theme.spacingS
                }
            }
        }
    }
}
