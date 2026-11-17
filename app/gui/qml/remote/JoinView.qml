// "Join support session" (plan/features/0044 § Joiner, ADR-0330), a content view of the page area
// (plan 0052, ADR-0387): the session code (digits, spaces allowed) or a link; a link to another
// server than the configured one needs the checkbox "Join on <server>". [Join] closes the view and
// opens a remote tab (AppViewModel.joinRemoteSession); × / Esc / Cancel close it.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ViewFrame {
    id: view

    readonly property var check: AppViewModel.checkJoinInput(codeField.text)
    readonly property bool foreign: view.check.valid && view.check.foreignRelay !== ""
    readonly property bool canJoin: view.check.valid && (!view.foreign || foreignBox.checked)

    objectName: "remoteJoinInputDialog"
    namePrefix: "remoteJoin"
    maxContentWidth: 560
    title: qsTr("Join a support session")
    onCloseRequested: AppViewModel.closeView()
    // The code is what the user came to type.
    Component.onCompleted: codeField.forceActiveFocus()

    function join() {
        if (!view.canJoin)
            return;
        const text = codeField.text;
        const allowForeign = foreignBox.checked;
        AppViewModel.closeView();
        AppViewModel.joinRemoteSession(text, allowForeign);
    }

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
        onAccepted: view.join()
    }

    Label {
        objectName: "remoteCodeError"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: codeField.text.trim() !== "" && !view.check.valid
        text: view.check.error ? view.check.error : ""
        color: Theme.error
        wrapMode: Text.Wrap
    }

    CheckBox {
        id: foreignBox

        objectName: "remoteForeignRelay"
        Layout.fillWidth: true
        visible: view.foreign
        text: qsTr("This link uses another server: %1. Join on it anyway").arg(view.foreign ? view.check.foreignRelay : "")
    }

    footerData: Flow {
        Layout.fillWidth: true
        layoutDirection: Qt.RightToLeft
        spacing: Theme.spacingS

        HButton {
            objectName: "remoteJoinButton"
            kind: "primary"
            text: qsTr("Join")
            enabled: view.canJoin
            onClicked: view.join()
        }

        HButton {
            objectName: "remoteJoinCancel"
            text: qsTr("Cancel")
            onClicked: AppViewModel.closeView()
        }
    }
}
