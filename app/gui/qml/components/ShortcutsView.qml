// F1 / Ctrl+/: every registered shortcut, grouped (Tabs, General, then per page) (ADR-0051), a
// content view of the page area (plan 0052, ADR-0387). Shown while AppViewModel.overlayView is
// "shortcuts" (AppViewModel.shortcutOverlayVisible); × / Esc / Close close it.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ViewFrame {
    id: view

    objectName: "shortcutOverlay"
    namePrefix: "shortcuts"
    maxContentWidth: 720
    title: qsTr("Keyboard shortcuts")
    onCloseRequested: AppViewModel.closeView()
    Component.onCompleted: view.focusTitle()

    Repeater {
        model: AppViewModel.shortcuts.groups

        delegate: ColumnLayout {
            id: group

            required property var modelData

            Layout.fillWidth: true
            spacing: Theme.spacingS

            Label {
                textFormat: Text.PlainText
                text: group.modelData.title
                font: Theme.fontUiStrong
                color: Theme.textMuted
            }

            Repeater {
                model: group.modelData.items

                delegate: RowLayout {
                    id: item

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Theme.spacingL

                    Label {
                        textFormat: Text.PlainText
                        objectName: "shortcutKeys"
                        Layout.preferredWidth: 180
                        text: item.modelData.keys
                        font: Theme.fontMonoSmall
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: item.modelData.description
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    footerData: Flow {
        Layout.fillWidth: true
        layoutDirection: Qt.RightToLeft

        HButton {
            id: closeButton

            objectName: "shortcutsClose"
            kind: "primary"
            text: qsTr("Close")
            onClicked: AppViewModel.closeView()
        }
    }
}
