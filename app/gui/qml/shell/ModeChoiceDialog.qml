// Plan 0045 (ADR-0359, ADR-0362): the first-start choice of the user mode. Opened by Main.qml
// while the mode is not chosen (first start, upgrade from rc.3), before the first tab is used: the
// three modes as cards (name + description), the preselection selected (Advanced; Expert for users
// who had the expert maintenance actions on), [Continue]. Closing the dialog (Esc) keeps and stores
// the preselection. The first-run setup wizard (plan 0008) takes this over as its first step later.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Dialog {
    id: dialog

    readonly property UiModeController controller: AppViewModel.uiMode
    // The card selected in the dialog (UiMode value); the preselection when opened.
    property int selected: UiMode.Advanced

    objectName: "modeChoiceDialog"
    title: qsTr("How do you want to use HARDWARIO Monitor?")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, (parent ? parent.width : 560) - 2 * Theme.spacingXl)
    padding: Theme.spacingXl
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusL
        border.color: Theme.border
    }
    onAboutToShow: dialog.selected = dialog.controller.mode
    onOpened: continueButton.forceActiveFocus()
    onAccepted: dialog.controller.chooseMode(dialog.selected)
    // Esc: the preselection (the mode while it is not chosen) is kept and stored.
    onRejected: dialog.controller.chooseMode(dialog.controller.mode)

    header: Label {
        textFormat: Text.PlainText
        objectName: "dialogTitle"
        text: dialog.title
        font: Theme.fontTitle
        color: Theme.text
        wrapMode: Text.Wrap
        padding: Theme.spacingXl
        bottomPadding: 0
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Repeater {
            model: dialog.controller.modes

            delegate: AbstractButton {
                id: card

                required property var modelData
                readonly property bool isSelected: dialog.selected === card.modelData.mode

                objectName: "modeCard_" + card.modelData.mode
                Layout.fillWidth: true
                checkable: false
                padding: Theme.spacingM
                Accessible.role: Accessible.RadioButton
                Accessible.name: card.modelData.name
                Accessible.description: card.modelData.description
                Accessible.checked: card.isSelected
                onClicked: dialog.selected = card.modelData.mode
                Keys.onReturnPressed: dialog.accept()
                Keys.onEnterPressed: dialog.accept()

                background: Rectangle {
                    radius: Theme.radiusM
                    color: card.isSelected ? Theme.accentSoft : (card.hovered ? Theme.surface : Theme.bg)
                    border.width: card.isSelected ? 2 : 1
                    border.color: card.isSelected ? Theme.accent : Theme.border

                    FocusRing {
                        control: card
                    }
                }

                contentItem: ColumnLayout {
                    spacing: 2

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: card.modelData.name
                        font: Theme.fontUiStrong
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: card.modelData.description
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        Label {
            objectName: "modeChoiceHint"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingXs
            text: qsTr("You can change it at any time in Settings › General.")
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }

    footer: Flow {
        layoutDirection: Qt.RightToLeft
        spacing: Theme.spacingS
        padding: Theme.spacingXl
        topPadding: 0

        HButton {
            id: continueButton

            objectName: "modeChoiceContinue"
            kind: "primary"
            text: qsTr("Continue")
            onClicked: dialog.accept()
            Keys.onReturnPressed: dialog.accept()
            Keys.onEnterPressed: dialog.accept()
        }
    }
}
