// "Join support session" (plan/features/0044 § Joiner, ADR-0330): the session code (digits, spaces
// allowed) or a link; a link to another server than the configured one needs the checkbox
// "Join on <server>". [Join] opens a remote tab (AppViewModel.joinRemoteSession).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Dialog {
    id: dialog

    readonly property var check: AppViewModel.checkJoinInput(codeField.text)
    readonly property bool foreign: dialog.check.valid && dialog.check.foreignRelay !== ""
    readonly property bool canJoin: dialog.check.valid && (!dialog.foreign || foreignBox.checked)

    objectName: "remoteJoinInputDialog"
    parent: Overlay.overlay
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, (parent ? parent.width : 480) - 2 * Theme.spacingXl)
    padding: Theme.spacingXl
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Join a support session")
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusL
        border.color: Theme.border
    }
    onOpened: {
        codeField.text = "";
        foreignBox.checked = false;
        codeField.forceActiveFocus();
    }

    header: Label {
        textFormat: Text.PlainText
        text: dialog.title
        font: Theme.fontTitle
        color: Theme.text
        wrapMode: Text.Wrap
        padding: Theme.spacingXl
        bottomPadding: 0
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: qsTr("Type the session code the customer reads to you, or paste the link.")
            color: Theme.text
            wrapMode: Text.Wrap
        }

        TextField {
            id: codeField

            objectName: "remoteCodeField"
            Layout.fillWidth: true
            placeholderText: qsTr("482 913")
            font: Theme.fontMono
            Accessible.name: qsTr("Session code or link")
            onAccepted: {
                if (dialog.canJoin)
                    dialog.accept();
            }
        }

        Label {
            objectName: "remoteCodeError"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: codeField.text.trim() !== "" && !dialog.check.valid
            text: dialog.check.error ? dialog.check.error : ""
            color: Theme.error
            wrapMode: Text.Wrap
        }

        CheckBox {
            id: foreignBox

            objectName: "remoteForeignRelay"
            Layout.fillWidth: true
            visible: dialog.foreign
            text: qsTr("This link uses another server: %1. Join on it anyway").arg(dialog.foreign ? dialog.check.foreignRelay : "")
        }
    }

    footer: Flow {
        layoutDirection: Qt.RightToLeft
        spacing: Theme.spacingS
        padding: Theme.spacingXl
        topPadding: 0

        HButton {
            objectName: "remoteJoinButton"
            kind: "primary"
            text: qsTr("Join")
            enabled: dialog.canJoin
            onClicked: dialog.accept()
        }

        HButton {
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        }
    }

    onAccepted: AppViewModel.joinRemoteSession(codeField.text, foreignBox.checked)
}
