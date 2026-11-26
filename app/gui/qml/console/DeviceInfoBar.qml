// "Save device information…" (plan 0063, ADR-0440): one inline line under the console toolbar.
// Reading: "Reading device information… (2 of 4)" with Cancel. Read: [ ] Include secrets, Save…
// (the page opens the save dialog) and Discard. Saved: "Device information saved to <path>" with
// Show in folder and Dismiss. All state and texts come from the ConsoleViewModel.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: bar

    required property ConsoleViewModel viewModel
    // "Include secrets": off for every new read (never remembered, D3).
    property alias includeSecrets: secrets.checked

    signal saveRequested(bool withSecrets)

    readonly property bool reading: bar.viewModel.deviceInfoReading
    readonly property bool ready: !bar.reading && bar.viewModel.deviceInfoReady
    readonly property bool notice: !bar.reading && !bar.ready && bar.viewModel.deviceInfoNotice !== ""

    objectName: "deviceInfoBar"
    visible: bar.reading || bar.ready || bar.notice
    implicitHeight: row.implicitHeight + 2 * Theme.spacingS
    color: Theme.accentSoft
    Accessible.role: Accessible.AlertMessage
    Accessible.name: label.text

    onReadyChanged: {
        if (bar.ready) {
            secrets.checked = false;
            saveButton.forceActiveFocus();
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.topMargin: Theme.spacingS
        anchors.bottomMargin: Theme.spacingS
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS

        BusyIndicator {
            visible: bar.reading
            running: visible && !AppViewModel.reducedMotion
            implicitWidth: 18
            implicitHeight: 18
        }

        Icon {
            visible: !bar.reading
            symbol: "info"
            color: Theme.accent
        }

        Label {
            id: label

            objectName: "deviceInfoText"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: bar.reading ? bar.viewModel.deviceInfoProgress
                              : bar.ready ? qsTr("The device information was read. Save it to a file.")
                                          : bar.viewModel.deviceInfoNotice
            color: Theme.text
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        CheckBox {
            id: secrets

            objectName: "deviceInfoSecrets"
            visible: bar.ready
            text: qsTr("Include secrets")

            HToolTip {
                visible: secrets.hovered
                text: qsTr("Write the keys, passwords, claim token and passkey as the device prints them. Without this they are written as <redacted>. On Linux and macOS only your user can read the file either way.")
            }
        }

        HButton {
            id: saveButton

            objectName: "deviceInfoSave"
            visible: bar.ready
            kind: "primary"
            text: qsTr("Save…")
            onClicked: bar.saveRequested(secrets.checked)
        }

        HButton {
            objectName: "deviceInfoShowFolder"
            visible: bar.notice
            text: qsTr("Show in folder")
            onClicked: bar.viewModel.showDeviceInfoFolder()
        }

        HButton {
            objectName: "deviceInfoCancel"
            visible: bar.reading || bar.ready
            kind: "ghost"
            text: bar.reading ? qsTr("Cancel") : qsTr("Discard")
            onClicked: bar.viewModel.cancelDeviceInfo()
        }

        HToolButton {
            objectName: "deviceInfoDismiss"
            visible: bar.notice
            iconName: "close"
            text: qsTr("Dismiss")
            display: AbstractButton.IconOnly
            onClicked: bar.viewModel.dismissDeviceInfoNotice()
        }
    }
}
