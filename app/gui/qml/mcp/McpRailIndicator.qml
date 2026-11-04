// The rail indicator of MCP (plan/features/0014, ADR-0105, ADR-0244): above Settings at the bottom
// of the rail; hidden while the server is off, a green dot while listening, a count badge while
// AI clients are active (a request within the last 60 s), red on an error. Its tooltip says the
// state ("AI assistants: listening on port 47800, 1 client"); a click opens Settings → AI
// assistants. Presentation only: McpController.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

AbstractButton {
    id: indicator

    readonly property McpController mcp: McpController
    readonly property bool compact: AppViewModel.railCompact

    objectName: "mcpRailIndicator"
    Layout.alignment: Qt.AlignHCenter
    visible: indicator.mcp.state !== McpController.Off
    implicitWidth: indicator.compact ? 44 : 64
    implicitHeight: indicator.compact ? 40 : Theme.railItemSize
    focusPolicy: Qt.StrongFocus
    text: qsTr("MCP")
    Accessible.role: Accessible.Button
    Accessible.name: indicator.mcp.summary
    onClicked: AppViewModel.openSettings("mcp")

    HToolTip {
        visible: indicator.hovered
        text: indicator.mcp.summary
        delay: 400
    }

    background: Item {
        FocusRing {
            control: indicator
        }
    }

    contentItem: ColumnLayout {
        spacing: 2

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 40
            implicitHeight: 26
            radius: 13
            color: indicator.hovered ? Theme.bg : "transparent"

            Label {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                text: indicator.text
                font: Theme.fontUiStrong
                color: indicator.mcp.state === McpController.Error ? Theme.error : Theme.textMuted
            }

            StateDot {
                objectName: "mcpRailDot"
                anchors.right: parent.right
                anchors.top: parent.top
                visible: indicator.mcp.activeClients === 0
                kind: indicator.mcp.state === McpController.Listening ? "connected"
                    : indicator.mcp.state === McpController.Error ? "error" : "connecting"
            }

            Rectangle {
                objectName: "mcpRailBadge"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: -4
                visible: indicator.mcp.activeClients > 0
                implicitWidth: Math.max(16, badgeText.implicitWidth + 6)
                implicitHeight: 16
                radius: 8
                color: Theme.success

                Label {
                    id: badgeText

                    textFormat: Text.PlainText
                    anchors.centerIn: parent
                    text: indicator.mcp.activeClients
                    font: Theme.fontUiSmall
                    color: Theme.surface
                }
            }
        }

        Label {
            textFormat: Text.PlainText
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 64
            visible: !indicator.compact
            text: qsTr("AI")
            font: Theme.fontUiSmall
            color: Theme.textMuted
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
