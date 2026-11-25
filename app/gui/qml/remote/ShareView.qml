// "Share this console" (plan/features/0044 § Host, step 2), a content view of the page area
// (plan 0052, ADR-0387): what support can do, that the session passes through the HARDWARIO
// server, that anyone with the code sees the output, the limits; [Start sharing] (primary) and
// [Cancel] (the focus starts on it; Esc). Shares the console of the current tab (the view belongs
// to it).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ViewFrame {
    id: view

    readonly property RemoteShareViewModel share: AppViewModel.currentTabViewModel
                                                  ? AppViewModel.currentTabViewModel.remoteShare : null

    objectName: "remoteShareDialog"
    namePrefix: "remoteShare"
    maxContentWidth: 560
    title: qsTr("Share this console with HARDWARIO Support")
    onCloseRequested: AppViewModel.closeView()
    Component.onCompleted: cancelButton.forceActiveFocus()

    Label {
        textFormat: Text.PlainText
        objectName: "remoteShareBody"
        Layout.fillWidth: true
        text: view.share ? view.share.shareExplanation : ""
        color: Theme.text
        wrapMode: Text.Wrap
    }

    Label {
        textFormat: Text.PlainText
        objectName: "remoteShareConsequence"
        Layout.fillWidth: true
        text: qsTr("Read the code only to HARDWARIO Support.")
        color: Theme.text
        font: Theme.fontUiStrong
        wrapMode: Text.Wrap
    }

    footerData: Flow {
        Layout.fillWidth: true
        layoutDirection: Qt.RightToLeft
        spacing: Theme.spacingS

        HButton {
            objectName: "remoteShareStart"
            kind: "primary"
            text: qsTr("Start sharing")
            enabled: view.share !== null && view.share.available && !view.share.sharing
            onClicked: {
                const share = view.share;
                AppViewModel.closeView();
                share.startSharing();
            }
        }

        HButton {
            id: cancelButton

            objectName: "remoteShareCancel"
            text: qsTr("Cancel")
            onClicked: AppViewModel.closeView()
        }
    }
}
