// The remote support session banner of a device tab (plan/features/0044 § UX): the host's
// "Shared with support — code 482 913" with [Copy link] [End sharing], the idle warning with
// [Keep sharing], a remote operation in progress, and the joiner's "Waiting for the customer…" /
// "The session ended." states. Presentation only: RemoteShareViewModel / RemoteJoinViewModel.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: banner

    property DeviceTabViewModel tab: null
    readonly property RemoteShareViewModel share: banner.tab ? banner.tab.remoteShare : null
    readonly property RemoteJoinViewModel join: banner.tab ? banner.tab.remoteJoin : null

    spacing: 0

    Rectangle {
        id: hostBanner

        objectName: "remoteShareBanner"
        Layout.fillWidth: true
        visible: banner.share !== null && banner.share.sharing
        implicitHeight: hostRow.implicitHeight + 2 * Theme.spacingS
        color: Theme.accentSoft
        Accessible.role: Accessible.AlertMessage
        Accessible.name: hostText.text

        RowLayout {
            id: hostRow

            anchors.fill: parent
            anchors.leftMargin: Theme.spacingM
            anchors.rightMargin: Theme.spacingS
            spacing: Theme.spacingS

            Icon {
                symbol: "share"
                color: Theme.accent
            }

            Label {
                id: hostText

                objectName: "remoteShareText"
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: {
                    if (banner.share === null)
                        return "";
                    if (banner.share.state === "connecting")
                        return qsTr("Connecting to the support server %1…").arg(banner.share.relayHost);
                    if (banner.share.state === "reconnecting")
                        return qsTr("Shared with support — code %1. The connection to the support server was lost, reconnecting…").arg(banner.share.code);
                    if (banner.share.operationRunning)
                        return qsTr("Shared with support — code %1. %2").arg(banner.share.code).arg(banner.share.operationText);
                    return banner.share.participantAllowed
                            ? qsTr("Shared with support — code %1. Support can send commands.").arg(banner.share.code)
                            : qsTr("Shared with support — code %1").arg(banner.share.code);
                }
            }

            Label {
                objectName: "remoteShareCode"
                textFormat: Text.PlainText
                visible: banner.share !== null && banner.share.code !== ""
                text: banner.share ? banner.share.code : ""
                font: Theme.fontTitle
                color: Theme.text
                Accessible.name: qsTr("Session code %1").arg(text)
            }

            HButton {
                objectName: "remoteCopyLink"
                kind: "ghost"
                visible: banner.share !== null && banner.share.link !== ""
                text: qsTr("Copy link")
                onClicked: banner.share.copyLink()
            }

            HButton {
                objectName: "remoteEndSharing"
                kind: "danger"
                text: qsTr("End sharing")
                onClicked: banner.share.endSharing()
            }
        }
    }

    WarningBanner {
        id: idleBanner

        objectName: "remoteIdleBanner"
        Layout.fillWidth: true
        visible: banner.share !== null && banner.share.sharing && banner.share.idleWarning
        text: qsTr("Sharing ends in 2 minutes because there was no activity.")
        actionText: qsTr("Keep sharing")
        onActionTriggered: banner.share.keepSharing()
        onDismissed: banner.share.keepSharing()
    }

    WarningBanner {
        id: joinBanner

        objectName: "remoteJoinBanner"
        Layout.fillWidth: true
        visible: banner.join !== null && banner.join.active && banner.join.bannerText !== ""
        text: banner.join ? banner.join.bannerText : ""
        actionText: banner.join && (banner.join.state === "idle" || banner.join.state === "ended") ? qsTr("Leave") : ""
        onActionTriggered: AppViewModel.closeTabObject(banner.tab, false)
        onDismissed: {}
    }
}
