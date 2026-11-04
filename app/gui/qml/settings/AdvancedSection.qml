// Settings → Advanced (plan/features/0033, ADR-0236): "Show expert maintenance actions" - Erase
// everything, Recover and the LTE MCU maintenance on the Flash page. Off by default.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: section

    readonly property SettingsViewModel vm: AppViewModel.globalPageViewModel("settings") as SettingsViewModel

    spacing: Theme.spacingM

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Show expert maintenance actions")
        description: qsTr("Erase everything, Recover and the LTE MCU maintenance on the Flash page. They can erase the serial number and the configuration of the device.")

        Switch {
            objectName: "expertActionsSwitch"
            checked: section.vm.expertActions
            Accessible.name: qsTr("Show expert maintenance actions")
            onToggled: section.vm.expertActions = checked
        }
    }
}
