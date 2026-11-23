// Plan 0045 (ADR-0359): a group of Settings items that are not SettingsRows (the Console section's
// command options) with the lowest user mode that shows them; with a `rowId` that an error or
// guide opened it is shown for this visit with a note (ADR-0361), like SettingsRow.
import QtQuick
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: group

    property int minimumMode: UiMode.Basic
    property string rowId: ""
    default property alias content: contentArea.data

    readonly property SettingsViewModel settings: AppViewModel.globalPageViewModel("settings") as SettingsViewModel
    readonly property bool revealed: group.rowId !== "" && group.settings !== null && group.settings.revealedRow === group.rowId

    visible: group.settings === null || group.settings.mode >= group.minimumMode || group.revealed
    spacing: Theme.spacingS

    RevealNote {
        Layout.fillWidth: true
        visible: group.revealed && group.settings.mode < group.minimumMode
        mode: group.minimumMode
    }

    ColumnLayout {
        id: contentArea

        Layout.fillWidth: true
        spacing: Theme.spacingS
    }
}
