// Settings → Expert (plan/features/0033, renamed "Advanced" → "Expert" by plan 0045, ADR-0362;
// shown in Expert mode only). The switch "Show expert maintenance actions" of ADR-0236 became
// Expert mode (ADR-0360).
// Plan 0044 (ADR-0330): "Support server" - the relay of remote support sessions, empty = default.
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
        label: qsTr("Support server")
        description: qsTr("The server used for remote support sessions. Empty = the HARDWARIO server (%1). Only https addresses.").arg(section.vm.defaultRelayUrl)

        ColumnLayout {
            spacing: Theme.spacingXs

            TextField {
                id: relayField

                // Set when the view model rejects the address; cleared on the next accepted one.
                property bool rejected: false

                function apply() {
                    rejected = section.vm.setRelayUrl(text) !== "";
                    if (!rejected)
                        text = Qt.binding(() => section.vm.relayUrl);
                }

                objectName: "relayUrlField"
                Layout.preferredWidth: 320
                text: section.vm.relayUrl
                placeholderText: section.vm.defaultRelayUrl
                Accessible.name: qsTr("Support server")
                onEditingFinished: apply()
                onAccepted: apply()
            }

            Label {
                objectName: "relayUrlError"
                visible: relayField.rejected
                textFormat: Text.PlainText
                text: qsTr("Only https addresses are accepted.")
                color: Theme.error
                font: Theme.fontUiSmall
                wrapMode: Text.Wrap
                Layout.maximumWidth: 320
            }
        }
    }
}
