// The host's questions of a remote support session (plan/features/0044, ADR-0335, ADR-0324), one
// instance per device tab: the join approval ([Allow] [End session], End session is the default
// and Esc) and the operation confirmation ([Allow] [Deny], Deny is the default and Esc). They open
// while RemoteShareViewModel has the question open (also for a tab in the background: the tab is
// brought to the front) and close when the view model withdraws it (timeout, session end).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import HardwarioMonitor

Item {
    id: dialogs

    objectName: "remoteSessionDialogs"
    property DeviceTabViewModel tab: null
    property int tabIndex: -1
    readonly property RemoteShareViewModel share: dialogs.tab ? dialogs.tab.remoteShare : null
    readonly property bool joinOpen: dialogs.share !== null && dialogs.share.joinQuestionOpen
    readonly property bool operationOpen: dialogs.share !== null && dialogs.share.operationQuestionOpen

    // Answers once per question (a closed dialog never answers twice).
    property bool answering: false

    onJoinOpenChanged: {
        if (dialogs.joinOpen) {
            if (dialogs.tabIndex >= 0)
                AppViewModel.currentTab = dialogs.tabIndex;
            joinDialog.open();
        } else {
            joinDialog.close();
        }
    }
    onOperationOpenChanged: {
        if (dialogs.operationOpen) {
            if (dialogs.tabIndex >= 0)
                AppViewModel.currentTab = dialogs.tabIndex;
            operationDialog.open();
        } else {
            operationDialog.close();
        }
    }

    ConfirmDialog {
        id: joinDialog

        objectName: "remoteJoinDialog"
        parent: Overlay.overlay
        title: qsTr("Someone joined your session")
        body: dialogs.share ? dialogs.share.joinQuestionText : ""
        consequence: qsTr("Ending the session disconnects everyone. You can share again with a new code.")
        acceptText: qsTr("Allow")
        rejectText: qsTr("End session")
        destructive: false
        onAccepted: {
            if (dialogs.share && dialogs.share.joinQuestionOpen)
                dialogs.share.answerJoin(true);
        }
        onRejected: {
            if (dialogs.share && dialogs.share.joinQuestionOpen)
                dialogs.share.answerJoin(false);
        }
    }

    ConfirmDialog {
        id: operationDialog

        objectName: "remoteOperationDialog"
        parent: Overlay.overlay
        title: qsTr("Support asks for an operation")
        body: dialogs.share ? dialogs.share.operationQuestionText : ""
        consequence: qsTr("It is denied automatically after 60 seconds.")
        acceptText: qsTr("Allow")
        rejectText: qsTr("Deny")
        destructive: true
        onAccepted: {
            if (dialogs.share && dialogs.share.operationQuestionOpen)
                dialogs.share.answerOperation(true);
        }
        onRejected: {
            if (dialogs.share && dialogs.share.operationQuestionOpen)
                dialogs.share.answerOperation(false);
        }
    }
}
