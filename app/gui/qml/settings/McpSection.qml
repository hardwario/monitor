// Settings → AI assistants (MCP) (plan/features/0014, ADR-0101, ADR-0102, ADR-0105, ADR-0106,
// ADR-0241, ADR-0244): enable with the server state, the port, the token (masked, Show, Copy,
// Regenerate…), the client snippets, the consent rules per category, the remembered grants and the
// activity list (the last 1000 calls, newest first). Presentation only: McpController.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: section

    property bool tokenShown: false
    readonly property int activityRowHeight: 24
    readonly property McpController mcp: McpController
    readonly property string stateKind: {
        switch (section.mcp.state) {
        case McpController.Listening:
            return "connected";
        case McpController.Error:
            return "error";
        case McpController.Starting:
        case McpController.Stopping:
            return "connecting";
        default:
            return "idle";
        }
    }

    objectName: "mcpSection"
    spacing: Theme.spacingM

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Allow AI assistants to use HARDWARIO Monitor")
        description: qsTr("AI tools such as Claude Code can read the console, send commands and, with your permission, flash devices.")

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM

            Switch {
                objectName: "mcpEnabled"
                checked: section.mcp.enabled
                Accessible.name: qsTr("Allow AI assistants to use HARDWARIO Monitor")
                onToggled: section.mcp.enabled = checked
            }

            StateDot {
                kind: section.stateKind
            }

            Label {
                id: stateLabel

                textFormat: Text.PlainText
                objectName: "mcpState"
                Layout.fillWidth: true
                text: section.mcp.stateText
                color: section.mcp.state === McpController.Error ? Theme.error : Theme.textMuted
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StatusBar
                Accessible.name: stateLabel.text
            }

            HButton {
                objectName: "mcpRetry"
                visible: section.mcp.state === McpController.Error && !section.mcp.tokenError
                text: qsTr("Retry")
                onClicked: section.mcp.retry()
            }
        }

        ErrorPanel {
            objectName: "mcpTokenError"
            Layout.fillWidth: true
            visible: section.mcp.tokenError
            error: section.mcp.error
            customActionText: qsTr("Regenerate token")
            onCustomActionTriggered: regenerateDialog.open()
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Address")
        description: qsTr("After a port change, AI clients have to be configured again.")

        RowLayout {
            spacing: Theme.spacingXs

            Label {
                textFormat: Text.PlainText
                text: "http://127.0.0.1:"
                font: Theme.fontMono
                color: Theme.text
            }

            SpinBox {
                id: portBox

                objectName: "mcpPort"
                from: section.mcp.minPort
                to: section.mcp.maxPort
                value: section.mcp.port
                editable: true
                textFromValue: (value, locale) => String(value)
                valueFromText: (text, locale) => parseInt(text)
                Accessible.name: qsTr("Port")
                onValueModified: section.mcp.port = portBox.value
            }

            Label {
                textFormat: Text.PlainText
                text: "/mcp"
                font: Theme.fontMono
                color: Theme.text
            }

            HButton {
                objectName: "mcpCopyAddress"
                text: qsTr("Copy")
                onClicked: AppViewModel.copyToClipboard(section.mcp.address, qsTr("Copied."))
            }
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Token")
        description: qsTr("AI clients send it with every request. Keep it private.")

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Label {
                id: tokenText

                textFormat: Text.PlainText
                objectName: "mcpToken"
                Layout.fillWidth: true
                text: section.tokenShown ? section.mcp.token() : (section.mcp.maskedToken !== "" ? section.mcp.maskedToken : qsTr("Created when the server starts"))
                font: Theme.fontMonoSmall
                color: Theme.text
                elide: Text.ElideMiddle
                Accessible.name: section.tokenShown ? qsTr("Token") : qsTr("Token, hidden")
            }

            HButton {
                objectName: "mcpShowToken"
                text: section.tokenShown ? qsTr("Hide") : qsTr("Show")
                enabled: section.mcp.maskedToken !== ""
                onClicked: section.tokenShown = !section.tokenShown
            }

            HButton {
                objectName: "mcpCopyToken"
                text: qsTr("Copy")
                enabled: section.mcp.maskedToken !== ""
                onClicked: section.mcp.copyToken()
            }

            HButton {
                objectName: "mcpRegenerate"
                text: qsTr("Regenerate…")
                onClicked: regenerateDialog.open()
            }
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Connect a client")

        Repeater {
            model: section.mcp.snippets

            delegate: ColumnLayout {
                id: snippet

                required property var modelData
                required property int index

                objectName: "mcpSnippet"
                Layout.fillWidth: true
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: snippet.modelData.title
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }

                    HButton {
                        objectName: "mcpCopySnippet"
                        text: qsTr("Copy")
                        Accessible.name: qsTr("Copy %1").arg(snippet.modelData.title)
                        onClicked: section.mcp.copySnippet(snippet.index)
                    }
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: snippet.modelData.note !== ""
                    text: snippet.modelData.note
                    font: Theme.fontUiSmall
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: snippetText.implicitHeight + 2 * Theme.spacingS
                    radius: Theme.radiusM
                    color: Theme.bg
                    border.color: Theme.border

                    Label {
                        id: snippetText

                        textFormat: Text.PlainText
                        anchors.fill: parent
                        anchors.margins: Theme.spacingS
                        text: snippet.modelData.text
                        font: Theme.fontMonoSmall
                        color: Theme.text
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Permissions")
        description: qsTr("What an AI assistant may do without asking you first.")

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Item {
                Layout.fillWidth: true
            }

            Label {
                textFormat: Text.PlainText
                Layout.preferredWidth: 90
                text: qsTr("Ask")
                font: Theme.fontUiSmall
                color: Theme.textMuted
            }

            Label {
                textFormat: Text.PlainText
                Layout.preferredWidth: 110
                text: qsTr("Always allow")
                font: Theme.fontUiSmall
                color: Theme.textMuted
            }
        }

        Repeater {
            model: section.mcp.rules

            delegate: RowLayout {
                id: rule

                required property var modelData

                objectName: "mcpRule_" + rule.modelData.id
                Layout.fillWidth: true
                spacing: Theme.spacingS

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: rule.modelData.title
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: rule.modelData.hint !== ""
                        text: rule.modelData.hint
                        font: Theme.fontUiSmall
                        color: Theme.warning
                        wrapMode: Text.Wrap
                    }
                }

                RadioButton {
                    objectName: "mcpAsk_" + rule.modelData.id
                    Layout.preferredWidth: 90
                    checked: !rule.modelData.allow
                    autoExclusive: false
                    Accessible.name: qsTr("%1: ask").arg(rule.modelData.title)
                    onClicked: section.mcp.setConsentRule(rule.modelData.id, false)
                }

                RadioButton {
                    objectName: "mcpAllow_" + rule.modelData.id
                    Layout.preferredWidth: 110
                    checked: rule.modelData.allow
                    autoExclusive: false
                    Accessible.name: qsTr("%1: always allow").arg(rule.modelData.title)
                    onClicked: section.mcp.setConsentRule(rule.modelData.id, true)
                }
            }
        }

        Label {
            textFormat: Text.PlainText
            objectName: "mcpNoGrants"
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingS
            visible: section.mcp.grants.length === 0
            text: qsTr("No answers are remembered for a client.")
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }

        Repeater {
            model: section.mcp.grants

            delegate: RowLayout {
                id: grant

                required property var modelData
                required property int index

                objectName: "mcpGrant"
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: grant.modelData.text
                    color: Theme.text
                    wrapMode: Text.Wrap
                }

                HButton {
                    objectName: "mcpRevoke"
                    text: qsTr("Revoke")
                    Accessible.name: qsTr("Revoke the permissions of %1").arg(grant.modelData.client)
                    onClicked: section.mcp.revokeGrant(grant.index)
                }
            }
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Activity")
        description: qsTr("The last %1 calls, kept until the application quits.").arg(1000)

        RowLayout {
            spacing: Theme.spacingS

            HButton {
                objectName: "mcpCopyActivity"
                text: qsTr("Copy")
                enabled: section.mcp.activity.count > 0
                onClicked: AppViewModel.copyToClipboard(section.mcp.activity.text())
            }

            HButton {
                objectName: "mcpClearActivity"
                text: qsTr("Clear")
                enabled: section.mcp.activity.count > 0
                onClicked: section.mcp.activity.clear()
            }
        }

        Label {
            textFormat: Text.PlainText
            objectName: "mcpNoActivity"
            Layout.fillWidth: true
            visible: section.mcp.activity.count === 0
            text: qsTr("No calls yet.")
            font: Theme.fontUiSmall
            color: Theme.textMuted
        }

        ListView {
            id: activityList

            objectName: "mcpActivity"
            Layout.fillWidth: true
            // At most 10 rows high; then it scrolls.
            Layout.preferredHeight: Math.min(activityList.count, 10) * section.activityRowHeight
            visible: section.mcp.activity.count > 0
            clip: true
            model: section.mcp.activity
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Activity")
            ScrollBar.vertical: ScrollBar {}

            delegate: RowLayout {
                id: entry

                required property string time
                required property string client
                required property string tool
                required property string device
                required property string arguments
                required property string result
                required property bool isError
                required property string duration

                width: ListView.view.width
                height: section.activityRowHeight
                spacing: Theme.spacingS
                Accessible.role: Accessible.ListItem
                Accessible.name: [entry.time, entry.client, entry.tool, entry.device, entry.result, entry.duration].join(" ")

                Label {
                    textFormat: Text.PlainText
                    text: entry.time
                    font: Theme.fontMonoSmall
                    color: Theme.textMuted
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.preferredWidth: 90
                    text: entry.client
                    font: Theme.fontUiSmall
                    color: Theme.text
                    elide: Text.ElideRight
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.preferredWidth: 100
                    text: entry.tool
                    font: Theme.fontMonoSmall
                    color: Theme.text
                    elide: Text.ElideRight
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.preferredWidth: 110
                    text: entry.device
                    font: Theme.fontMonoSmall
                    color: Theme.textMuted
                    elide: Text.ElideMiddle
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: entry.arguments
                    font: Theme.fontMonoSmall
                    color: Theme.textMuted
                    elide: Text.ElideRight
                }

                Label {
                    id: resultLabel

                    textFormat: Text.PlainText
                    objectName: "mcpActivityResult"
                    Layout.preferredWidth: 120
                    text: entry.result
                    font: Theme.fontUiSmall
                    color: entry.isError ? Theme.error : Theme.success
                    elide: Text.ElideRight

                    HoverHandler {
                        id: resultHover
                    }

                    HToolTip {
                        visible: resultHover.hovered && entry.isError
                        text: entry.result
                    }
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.preferredWidth: 50
                    horizontalAlignment: Text.AlignRight
                    text: entry.duration
                    font: Theme.fontUiSmall
                    color: Theme.textMuted
                }
            }
        }
    }

    ConfirmDialog {
        id: regenerateDialog

        objectName: "mcpRegenerateDialog"
        title: qsTr("Regenerate the token?")
        body: qsTr("Connected AI clients stop working until you update their configuration.")
        acceptText: qsTr("Regenerate")
        onAccepted: {
            section.tokenShown = false;
            section.mcp.regenerateToken();
        }
    }
}
