// The MCP consent prompt (plan/features/0014, ADR-0101, ADR-0108): its own frameless, always-on-top
// window without a transient parent (shown while the main window is minimised), centred on the
// screen under the mouse cursor, raised with requestActivate() and a taskbar / dock alert of the
// main window. One prompt at a time (the queue is GuiConsentBroker's); the main window does not
// switch tabs (Show device does). Esc = Deny, no default button; the title and the action are
// announced to screen readers. Presentation only: McpController.consent (ConsentViewModel).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Window {
    id: prompt

    // The main window (its screen is the fallback; it gets the taskbar / dock alert).
    property Window mainWindow: null
    readonly property ConsentViewModel vm: McpController.consent
    // Esc = Deny (tests trigger it here).
    readonly property alias escapeShortcut: escapeKey
    // The shown prompt's id (another prompt -> present() again).
    readonly property real shownPrompt: prompt.vm !== null ? prompt.vm.promptId : 0

    objectName: "consentWindow"
    flags: Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    transientParent: null
    modality: Qt.NonModal
    title: qsTr("HARDWARIO Monitor — Allow AI assistant?")
    width: 480
    height: content.implicitHeight + 2 * Theme.spacingL
    color: Theme.surfaceRaised
    // Hidden with the main window: closing it quits (ADR-0108; this window must not keep the
    // application alive), and quitting denies the pending prompts (McpController::shutdown).
    visible: prompt.vm !== null && prompt.vm.active && (prompt.mainWindow === null || prompt.mainWindow.visible)

    // Review H1: every newly shown prompt (not only the first) is placed, raised and announced,
    // and the focus moves to Deny; the Allow buttons wait for vm.armed.
    function present() {
        if (!prompt.visible)
            return;
        prompt.vm.place(prompt, prompt.mainWindow);
        prompt.raise();
        prompt.requestActivate();
        denyButton.forceActiveFocus();
        heading.announce();
    }

    onVisibleChanged: prompt.present()
    onShownPromptChanged: prompt.present()

    Shortcut {
        id: escapeKey

        sequence: "Esc"
        context: Qt.WindowShortcut
        enabled: prompt.visible
        onActivated: prompt.vm.deny(prompt.vm.promptId)
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.border
        radius: Theme.radiusM
    }

    ColumnLayout {
        id: content

        anchors.fill: parent
        anchors.margins: Theme.spacingL
        spacing: Theme.spacingS

        // Own title row (frameless window).
        Label {
            textFormat: Text.PlainText
            objectName: "consentWindowTitle"
            Layout.fillWidth: true
            text: prompt.title
            font: Theme.fontUiSmall
            color: Theme.textMuted
            elide: Text.ElideRight
        }

        Label {
            id: heading

            // The window's title and the action for screen readers when the prompt is shown.
            function announce() {
                Accessible.announce(prompt.title + ". " + heading.text);
            }

            textFormat: Text.PlainText
            objectName: "consentTitle"
            Layout.fillWidth: true
            text: prompt.vm ? prompt.vm.title : ""
            font: Theme.fontTitle
            color: Theme.text
            wrapMode: Text.Wrap
            Accessible.role: Accessible.Heading
            Accessible.name: heading.text
        }

        // The detail rows: label and value, warnings without a label.
        Repeater {
            model: prompt.vm ? prompt.vm.details : []

            delegate: RowLayout {
                id: detail

                required property var modelData

                objectName: "consentDetail"
                Layout.fillWidth: true
                spacing: Theme.spacingM

                Label {
                    textFormat: Text.PlainText
                    Layout.preferredWidth: 110
                    Layout.alignment: Qt.AlignTop
                    visible: detail.modelData.label !== ""
                    text: detail.modelData.label
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: detail.modelData.value
                    color: detail.modelData.warning ? Theme.warning : Theme.text
                    font: detail.modelData.warning ? Theme.fontUiStrong : Theme.fontUi
                    wrapMode: Text.WrapAnywhere
                }
            }
        }

        Label {
            id: showDevice

            textFormat: Text.PlainText
            objectName: "consentShowDevice"
            Layout.topMargin: Theme.spacingS
            visible: prompt.vm !== null && prompt.vm.canShowDevice
            text: qsTr("Show device ↗")
            color: Theme.accent
            font.underline: showDeviceHover.hovered
            Accessible.role: Accessible.Link
            Accessible.name: qsTr("Show device")

            HoverHandler {
                id: showDeviceHover

                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: prompt.vm.showDevice()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingM
            spacing: Theme.spacingS

            HButton {
                id: denyButton

                objectName: "consentDeny"
                text: qsTr("Deny")
                onClicked: prompt.vm.deny(prompt.vm.promptId)
            }

            Item {
                Layout.fillWidth: true
            }

            HButton {
                objectName: "consentAllowOnce"
                text: qsTr("Allow once")
                enabled: prompt.vm !== null && prompt.vm.armed
                onClicked: prompt.vm.allowOnce(prompt.vm.promptId)
            }

            HButton {
                objectName: "consentAllowForClient"
                Layout.maximumWidth: 240
                text: prompt.vm ? prompt.vm.rememberLabel : ""
                enabled: prompt.vm !== null && prompt.vm.armed
                onClicked: prompt.vm.allowForClient(prompt.vm.promptId)
            }
        }

        Label {
            textFormat: Text.PlainText
            objectName: "consentCountdown"
            Layout.fillWidth: true
            text: prompt.vm ? prompt.vm.countdownText : ""
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }

        Label {
            textFormat: Text.PlainText
            objectName: "consentQueued"
            Layout.fillWidth: true
            visible: prompt.vm !== null && prompt.vm.queued > 0
            text: prompt.vm ? qsTr("%n more request(s) waiting.", "", prompt.vm.queued) : ""
            font: Theme.fontUiSmall
            color: Theme.textMuted
        }
    }
}
