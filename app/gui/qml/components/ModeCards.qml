// The user modes Basic / Advanced / Expert as cards (plan 0045, ADR-0359; plan 0056, ADR-0392: the
// question on the Welcome step of the setup wizard): name + description, the selected card with the
// accent border, the hint below. Click or Space selects a card, Up / Down move the selection; a
// screen reader reads the cards as radio buttons. Presentation only: `modes` is
// UiModeController.modes ([{mode, name, description}]), `selected` a UiMode value; a choice is
// reported by modeSelected(mode).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: cards

    property var modes: []
    property int selected: UiMode.Advanced

    signal modeSelected(int mode)

    function select(index: int) {
        if (index < 0 || index >= cards.modes.length)
            return;
        cards.modeSelected(cards.modes[index].mode);
        const card = repeater.itemAt(index);
        if (card)
            card.forceActiveFocus();
    }

    spacing: Theme.spacingS

    Label {
        objectName: "modeChoiceQuestion"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        text: qsTr("How do you want to use HARDWARIO Monitor?")
        font: Theme.fontUiStrong
        color: Theme.text
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }

    Repeater {
        id: repeater

        model: cards.modes

        delegate: AbstractButton {
            id: card

            required property var modelData
            required property int index
            readonly property bool isSelected: cards.selected === card.modelData.mode

            objectName: "modeCard_" + card.modelData.mode
            Layout.fillWidth: true
            checkable: false
            focusPolicy: Qt.StrongFocus
            padding: Theme.spacingM
            Accessible.role: Accessible.RadioButton
            Accessible.name: card.modelData.name
            Accessible.description: card.modelData.description
            Accessible.checked: card.isSelected
            onClicked: cards.modeSelected(card.modelData.mode)
            Keys.onUpPressed: cards.select(card.index - 1)
            Keys.onDownPressed: cards.select(card.index + 1)

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
