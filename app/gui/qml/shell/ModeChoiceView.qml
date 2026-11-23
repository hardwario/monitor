// Plan 0045 (ADR-0359, ADR-0362): the first-start choice of the user mode, a content view of the
// page area (plan 0052, ADR-0387). Shown by AppViewModel while the mode is not chosen (first start,
// upgrade from rc.3), before the first tab is used: the three modes as cards (name + description),
// the preselection selected (Advanced; Expert for users who had the expert maintenance actions on),
// [Continue]. Closing the view (× / Esc, a tab switch) keeps and stores the preselection. The
// first-run setup wizard (plan 0008) takes this over as its first step later.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ViewFrame {
    id: view

    readonly property UiModeController controller: AppViewModel.uiMode
    // The card selected in the view (UiMode value); the preselection when shown.
    property int selected: view.controller.mode

    objectName: "modeChoiceDialog"
    namePrefix: "modeChoice"
    maxContentWidth: 560
    title: qsTr("How do you want to use HARDWARIO Monitor?")
    onCloseRequested: AppViewModel.closeView() // stores the preselection
    Component.onCompleted: continueButton.forceActiveFocus()

    function choose() {
        view.controller.chooseMode(view.selected); // the view closes when the mode is chosen
    }

    Repeater {
        model: view.controller.modes

        delegate: AbstractButton {
            id: card

            required property var modelData
            readonly property bool isSelected: view.selected === card.modelData.mode

            objectName: "modeCard_" + card.modelData.mode
            Layout.fillWidth: true
            checkable: false
            padding: Theme.spacingM
            Accessible.role: Accessible.RadioButton
            Accessible.name: card.modelData.name
            Accessible.description: card.modelData.description
            Accessible.checked: card.isSelected
            onClicked: view.selected = card.modelData.mode
            Keys.onReturnPressed: view.choose()
            Keys.onEnterPressed: view.choose()

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

    footerData: Flow {
        Layout.fillWidth: true
        layoutDirection: Qt.RightToLeft
        spacing: Theme.spacingS

        HButton {
            id: continueButton

            objectName: "modeChoiceContinue"
            kind: "primary"
            text: qsTr("Continue")
            onClicked: view.choose()
            Keys.onReturnPressed: view.choose()
            Keys.onEnterPressed: view.choose()
        }
    }
}
