// "Share this console" (plan/features/0044 § Host, step 2): what support can do, that the session
// passes through the HARDWARIO server, that anyone with the code sees the output, the limits;
// [Start sharing] (primary) and [Cancel] (default, Esc).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ConfirmDialog {
    id: dialog

    property RemoteShareViewModel share: null

    objectName: "remoteShareDialog"
    parent: Overlay.overlay
    title: qsTr("Share this console with HARDWARIO Support")
    body: dialog.share ? dialog.share.shareExplanation : ""
    consequence: qsTr("Read the code only to HARDWARIO Support.")
    acceptText: qsTr("Start sharing")
    rejectText: qsTr("Cancel")
    destructive: false
    onAccepted: {
        if (dialog.share)
            dialog.share.startSharing();
    }
}
