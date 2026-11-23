// The Bluetooth view of the "Choose device" page (plan/features/0012-m5-ble-gui-pairing.md,
// ADR-0081, ADR-0229): nearby devices of a profile with Bluetooth from a continuous scan, a name
// filter, "Show all devices", the device type for unknown devices and Connect. An ErrorPanel replaces
// the list while the adapter cannot be used; an empty state after the first scan finds nothing.
// Plan 0033 (ADR-0235): while the pointer is over the list or a row has keyboard focus the order is
// frozen (picker.freezeOrder); new devices are appended until it is released.
// The arrow keys move the selection between the rows that can be chosen (skipping devices in use);
// Down in the filter enters the list and Up from its first row returns to the filter.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: view

    required property BlePickerViewModel picker
    readonly property bool adapterProblem: view.picker.adapterError.valid
    readonly property bool noDevices: view.picker.scanned && view.picker.count === 0 && !view.adapterProblem
    // Rows that have the keyboard focus (0 or 1; counted by the delegates).
    property int focusedRows: 0

    Binding {
        target: view.picker
        property: "freezeOrder"
        value: listHover.hovered || view.focusedRows > 0
    }

    objectName: "bleDeviceList"

    // Chooses and focuses the next row in direction step (+1 / -1) after row from that can be
    // chosen without switching tabs. Returns false when there is none.
    function moveSelection(from, step) {
        for (let i = from + step; i >= 0 && i < rows.count; i += step) {
            const item = rows.itemAt(i);
            if (item && item.enabled && view.picker.isSelectable(i)) {
                view.picker.choose(i);
                item.forceActiveFocus(Qt.TabFocusReason);
                return true;
            }
        }
        return false;
    }

    // Moves the keyboard focus from the filter into the list: to the selected row, else the first
    // row that can be chosen.
    function enterList() {
        const selected = rows.itemAt(view.picker.selectedRow);
        if (selected && selected.enabled)
            selected.forceActiveFocus(Qt.TabFocusReason);
        else
            view.moveSelection(-1, 1);
    }

    // Scrolls the page so that item (a row) is fully visible.
    function ensureVisible(item) {
        const flick = scroll.contentItem as Flickable;
        const top = item.mapToItem(flick.contentItem, 0, 0).y;
        if (top < flick.contentY)
            flick.contentY = top;
        else if (top + item.height > flick.contentY + flick.height)
            flick.contentY = top + item.height - flick.height;
    }

    Connections {
        target: view.picker

        function onAnnounce(text) {
            if (view.visible)
                view.Accessible.announce(text);
        }
    }

    ScrollView {
        id: scroll

        anchors.fill: parent
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
                    objectName: "bleTitle"
                    Layout.fillWidth: true
                    text: qsTr("Choose a device to connect")
                    font: Theme.fontTitle
                    color: Theme.text
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Bluetooth devices nearby. The list refreshes automatically.")
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                }
            }

            ErrorPanel {
                objectName: "bleAdapterError"
                Layout.fillWidth: true
                error: view.picker.adapterError
                primaryAction: ErrorAction.Retry
                secondaryActionText: view.picker.canOpenPrivacySettings ? qsTr("Open Privacy settings") : ""
                onActionTriggered: view.picker.refresh()
                onSecondaryActionTriggered: view.picker.openPrivacySettings()
            }

            // Device list card.
            Rectangle {
                Layout.fillWidth: true
                visible: !view.adapterProblem
                implicitHeight: listCard.implicitHeight + 2 * Theme.spacingL
                radius: Theme.radiusM + 2
                color: Theme.bg
                border.color: Theme.border

                ColumnLayout {
                    id: listCard

                    anchors.fill: parent
                    anchors.margins: Theme.spacingL
                    spacing: Theme.spacingM

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingM

                        TextField {
                            id: filterField

                            objectName: "bleFilter"
                            Layout.fillWidth: true
                            implicitHeight: Theme.controlHeight
                            placeholderText: qsTr("Filter by name…")
                            text: view.picker.filterText
                            selectByMouse: true
                            Accessible.name: qsTr("Filter by name")
                            onTextEdited: view.picker.filterText = filterField.text
                            Keys.onReturnPressed: view.picker.connectSelected()
                            Keys.onEnterPressed: view.picker.connectSelected()
                            Keys.onDownPressed: view.enterList()
                        }

                        CheckBox {
                            objectName: "bleShowAll"
                            text: qsTr("Show all devices")
                            checked: view.picker.showAll
                            onToggled: view.picker.showAll = checked
                        }
                    }

                    // Empty state after the first scan.
                    EmptyState {
                        objectName: "noBleDevicesState"
                        Layout.fillWidth: true
                        visible: view.noDevices
                        iconName: "bluetooth"
                        headline: qsTr("No %1 found nearby").arg(view.picker.profileName)
                        text: qsTr("Make sure the device is powered and its Bluetooth is on.")
                              + (view.picker.powerHint !== "" ? " " + view.picker.powerHint : "")
                        actionText: view.picker.showAll ? "" : qsTr("Show all devices")
                        onActionTriggered: view.picker.showAll = true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        visible: view.picker.count > 0
                        implicitHeight: deviceColumn.implicitHeight + 2
                        radius: Theme.radiusM
                        color: "transparent"
                        border.color: Theme.border
                        clip: true

                        HoverHandler {
                            id: listHover

                            objectName: "bleListHover"
                        }

                        ColumnLayout {
                            id: deviceColumn

                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0

                            Repeater {
                                id: rows

                                model: view.picker

                                delegate: ItemDelegate {
                                    id: row

                                    required property int index
                                    required property string deviceId
                                    required property string name
                                    required property int rssi
                                    required property int signalLevel
                                    required property string profileName
                                    required property int inUseTab
                                    required property string inUseOther
                                    required property bool stale
                                    required property bool connectable
                                    readonly property bool selected: row.index === view.picker.selectedRow
                                    readonly property string displayName: row.name !== "" ? row.name : qsTr("(no name)")

                                    objectName: "bleRow"
                                    Layout.fillWidth: true
                                    enabled: row.connectable
                                    highlighted: row.selected
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.role: Accessible.RadioButton
                                    Accessible.checked: row.selected
                                    Accessible.name: row.displayName + ", " + row.profileName
                                    Accessible.description: [signalText.text, tag.text, staleTag.visible ? staleTag.text : ""]
                                                            .filter(part => part !== "").join(", ")
                                    onActiveFocusChanged: {
                                        view.focusedRows = Math.max(0, view.focusedRows + (row.activeFocus ? 1 : -1));
                                        if (row.activeFocus)
                                            view.ensureVisible(row);
                                    }
                                    Component.onDestruction: {
                                        if (row.activeFocus)
                                            view.focusedRows = Math.max(0, view.focusedRows - 1);
                                    }
                                    onClicked: view.picker.choose(row.index)
                                    onDoubleClicked: {
                                        view.picker.choose(row.index);
                                        view.picker.connectSelected();
                                    }
                                    Keys.onReturnPressed: {
                                        view.picker.choose(row.index);
                                        view.picker.connectSelected();
                                    }
                                    Keys.onEnterPressed: {
                                        view.picker.choose(row.index);
                                        view.picker.connectSelected();
                                    }
                                    Keys.onDownPressed: view.moveSelection(row.index, 1)
                                    Keys.onUpPressed: {
                                        if (!view.moveSelection(row.index, -1))
                                            filterField.forceActiveFocus(Qt.BacktabFocusReason);
                                    }
                                    HToolTip {
                                        // macOS: the id (a UUID) only here (ADR-0229).
                                        visible: row.hovered && view.picker.hidesIds
                                        text: row.deviceId
                                        delay: 600
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
                                        // "not seen recently": dimmed (ADR-0229).
                                        opacity: !row.enabled ? 0.6 : row.stale ? 0.5 : 1

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

                                        Icon {
                                            symbol: "signal_" + row.signalLevel
                                            color: Theme.textMuted
                                            size: 18
                                        }

                                        Label {
                                            textFormat: Text.PlainText
                                            objectName: "bleName"
                                            text: row.displayName
                                            font: Theme.fontUiStrong
                                            color: Theme.text
                                            elide: Text.ElideRight
                                            Layout.maximumWidth: 220
                                        }

                                        Label {
                                            textFormat: Text.PlainText
                                            objectName: "bleId"
                                            visible: !view.picker.hidesIds
                                            text: row.deviceId
                                            font: Theme.fontMono
                                            color: Theme.textMuted
                                            elide: Text.ElideRight
                                            Layout.maximumWidth: 180
                                        }

                                        Label {
                                            id: signalText

                                            textFormat: Text.PlainText
                                            objectName: "bleRssi"
                                            visible: row.rssi !== 0
                                            text: row.rssi !== 0 ? qsTr("%1 dBm").arg(row.rssi) : ""
                                            font: Theme.fontUiSmall
                                            color: Theme.textMuted
                                        }

                                        Label {
                                            textFormat: Text.PlainText
                                            visible: row.profileName !== "" && view.picker.showAll
                                            text: row.profileName
                                            font: Theme.fontUiSmall
                                            color: Theme.textMuted
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            id: staleTag

                                            objectName: "bleStale"
                                            visible: row.stale
                                            text: qsTr("not seen recently")
                                            font: Theme.fontUiSmall
                                            color: Theme.textMuted
                                        }

                                        Label {
                                            id: tag

                                            textFormat: Text.PlainText
                                            objectName: "bleInUse"
                                            visible: tag.text !== ""
                                            text: row.inUseOther !== "" ? qsTr("In use by another program (%1)").arg(row.inUseOther)
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

                    RowLayout {
                        objectName: "bleScanning"
                        visible: view.picker.scanning || !view.picker.scanned
                        spacing: Theme.spacingS

                        BusyIndicator {
                            running: visible && !AppViewModel.reducedMotion
                            implicitWidth: 20
                            implicitHeight: 20
                        }

                        Label {
                            text: qsTr("Scanning for Bluetooth devices…")
                            color: Theme.textMuted
                        }
                    }
                }
            }

            // Device type and Connect.
            Rectangle {
                Layout.fillWidth: true
                visible: !view.adapterProblem
                implicitHeight: typeCard.implicitHeight + 2 * Theme.spacingL
                radius: Theme.radiusM + 2
                color: Theme.bg
                border.color: Theme.border

                RowLayout {
                    id: typeCard

                    anchors.fill: parent
                    anchors.margins: Theme.spacingL
                    spacing: Theme.spacingM

                    Label {
                        text: qsTr("Device type")
                        font: Theme.fontUiStrong
                        color: Theme.text
                    }

                    ComboBox {
                        id: typeBox

                        objectName: "bleProfileBox"
                        implicitHeight: Theme.controlHeight
                        model: view.picker.profiles
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: typeBox.indexOfValue(view.picker.profileId)
                        Accessible.name: qsTr("Device type")
                        Accessible.description: qsTr("Used for devices of an unknown type")
                        onActivated: view.picker.profileId = typeBox.currentValue

                        FocusRing {
                            control: typeBox
                            radiusOffset: Theme.radiusS
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: view.picker.count > 0 && !view.picker.canConnect
                        text: qsTr("Choose a free device in the list above.")
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                    }

                    Item {
                        Layout.fillWidth: true
                        visible: view.picker.count === 0 || view.picker.canConnect
                    }

                    HButton {
                        objectName: "bleConnectButton"
                        kind: "primary"
                        enabled: view.picker.canConnect
                        text: qsTr("Connect")
                        onClicked: view.picker.connectSelected()
                    }
                }
            }

            Item {
                implicitHeight: Theme.spacingS
            }
        }
    }
}
