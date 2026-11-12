// Settings → General (0006): the user mode (plan 0045, ADR-0359: Basic / Advanced / Expert, first
// row in every mode), language (live), theme (live), reopen tabs at start (ADR-0061).
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
        label: qsTr("Mode")

        // A segmented control: one button per mode, the selected one highlighted.
        Row {
            objectName: "modeSelector"
            spacing: Theme.spacingXs
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Mode")

            Repeater {
                model: section.vm.modes

                delegate: HButton {
                    id: modeButton

                    required property var modelData

                    objectName: "modeButton_" + modeButton.modelData.mode
                    kind: section.vm.mode === modeButton.modelData.mode ? "primary" : "normal"
                    text: modeButton.modelData.name
                    Accessible.role: Accessible.RadioButton
                    Accessible.checked: section.vm.mode === modeButton.modelData.mode
                    onClicked: section.vm.selectMode(modeButton.modelData.mode)
                }
            }
        }

        Label {
            objectName: "modeDescription"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: section.vm.modeDescription
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Language")

        ComboBox {
            id: languageBox

            objectName: "languageSelector"
            Layout.preferredWidth: 240
            textRole: "name"
            valueRole: "code"
            model: section.vm.languages
            currentIndex: section.vm.languageIndex
            Accessible.name: qsTr("Language")
            onActivated: index => section.vm.selectLanguage(index)
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Theme")

        ComboBox {
            id: themeBox

            objectName: "themeSelector"
            Layout.preferredWidth: 240
            textRole: "name"
            valueRole: "code"
            model: section.vm.themes
            currentIndex: section.vm.themeIndex
            Accessible.name: qsTr("Theme")
            onActivated: index => section.vm.selectTheme(index)
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Reopen tabs at start")
        description: qsTr("Tabs are restored disconnected.")

        Switch {
            objectName: "restoreTabsSwitch"
            checked: section.vm.restoreTabs
            Accessible.name: qsTr("Reopen tabs at start")
            onToggled: section.vm.restoreTabs = checked
        }
    }
}
