// One row of a Settings section: a label (with an optional muted description below it) in a fixed
// column on the left, the control(s) on the right. Labels wrap (Czech texts are longer).
// Plan 0045 (ADR-0359): `minimumMode` is the lowest user mode that shows the row (the section's
// own minimum is registered in C++); a row with a `rowId` that an error or guide opened is shown
// for this visit with a note (ADR-0361).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: row

    property string label: ""
    property string description: ""
    property int minimumMode: UiMode.Basic
    property string rowId: ""
    default property alias controls: controlArea.data

    readonly property SettingsViewModel settings: AppViewModel.globalPageViewModel("settings") as SettingsViewModel
    readonly property bool revealed: row.rowId !== "" && row.settings !== null && row.settings.revealedRow === row.rowId

    visible: row.settings === null || row.settings.mode >= row.minimumMode || row.revealed
    spacing: Theme.spacingXs

    RevealNote {
        Layout.fillWidth: true
        visible: row.revealed && row.settings.mode < row.minimumMode
        mode: row.minimumMode
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingL

        ColumnLayout {
            Layout.preferredWidth: 200
            Layout.minimumWidth: 200
            Layout.maximumWidth: 200
            Layout.alignment: Qt.AlignTop
            spacing: 2

            Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: row.label
                color: Theme.text
                wrapMode: Text.Wrap
            }

            Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: row.description !== ""
                text: row.description
                font: Theme.fontUiSmall
                color: Theme.textMuted
                wrapMode: Text.Wrap
            }
        }

        ColumnLayout {
            id: controlArea

            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.spacingXs
        }
    }
}
