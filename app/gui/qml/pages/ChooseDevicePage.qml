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

            // Plan 0009 (0035): the Connect wizard; hidden while it is not registered.
            HButton {
                objectName: "noProbesHelp"
                Layout.alignment: Qt.AlignHCenter
                visible: AppViewModel.launcher.canOpen("connect-device")
                text: qsTr("Guide me")
                onClicked: page.tab.openWizard("connect-device")
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

                ProbeList {
                    Layout.fillWidth: true
                    visible: !page.libraryMissing
                    picker: page.picker
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

                        // Plan 0009 (0035): the Connect wizard as a ghost link; hidden until the
                        // wizard is registered.
                        HButton {
                            objectName: "connectGuideLink"
                            Layout.alignment: Qt.AlignRight
                            visible: AppViewModel.launcher.canOpen("connect-device")
                            kind: "ghost"
                            text: qsTr("Not sure how to connect? Guide me")
                            onClicked: page.tab.openWizard("connect-device")
                        }

                        // Plan 0010 (0035): the guided flash, in the J-Link view only; hidden until
                        // the wizard is registered.
                        HButton {
                            objectName: "flashGuideLink"
                            Layout.alignment: Qt.AlignRight
                            visible: AppViewModel.launcher.canOpen("flash-firmware")
                            kind: "ghost"
                            text: qsTr("Flash firmware with a guide")
                            onClicked: page.tab.openWizard("flash-firmware")
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
