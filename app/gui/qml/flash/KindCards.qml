// Flash step 1 (ADR-0055; amended by 0021, ADR-0174): "What do you want to flash?" - two radio
// cards (application firmware, LTE firmware incl. the modem), stacked below 1000 px window width;
// prerequisite problems (nRF Util) appear inline below the cards (plan 0033: no connector warning,
// the LTE kind starts the LTE update wizard).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FlashCard {
    id: card

    required property FlashViewModel vm

    number: 1
    title: qsTr("What do you want to flash?")

    GridLayout {
        Layout.fillWidth: true
        columns: AppViewModel.windowWidth < 1000 ? 1 : 2
        columnSpacing: Theme.spacingS
        rowSpacing: Theme.spacingS
        Accessible.role: Accessible.Grouping
        Accessible.name: card.title

        Repeater {
            model: card.vm ? card.vm.kinds : []

            delegate: AbstractButton {
                id: kindCard

                required property var modelData

                objectName: "kindCard" + kindCard.modelData.kind
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                // Not checkable: `checked` follows the view model only (a click on the selected
                // card must not un-check it).
                checkable: false
                checked: card.vm ? card.vm.kind === kindCard.modelData.kind : false
                focusPolicy: Qt.StrongFocus
                padding: Theme.spacingM
                Accessible.role: Accessible.RadioButton
                Accessible.name: kindCard.modelData.title
                Accessible.description: kindCard.modelData.mcu + ", " + kindCard.modelData.accepts
                Accessible.checked: kindCard.checked
                onClicked: card.vm.kind = kindCard.modelData.kind

                background: Rectangle {
                    radius: Theme.radiusM
                    color: kindCard.checked ? Theme.accentSoft : Theme.bg
                    border.width: kindCard.checked ? 2 : 1
                    border.color: kindCard.checked ? Theme.accent : Theme.border

                    FocusRing {
                        control: kindCard
                    }
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingS

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: 8
                        color: Theme.bg
                        border.width: 2
                        border.color: kindCard.checked ? Theme.accent : Theme.textMuted

                        Rectangle {
                            anchors.centerIn: parent
                            visible: kindCard.checked
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.accent
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: kindCard.modelData.title
                            font: Theme.fontUiStrong
                            color: Theme.text
                            wrapMode: Text.Wrap
                        }

                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: kindCard.modelData.mcu
                            font: Theme.fontUiSmall
                            color: Theme.textMuted
                            wrapMode: Text.Wrap
                        }

                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: kindCard.modelData.accepts
                            font: Theme.fontMonoSmall
                            color: Theme.textMuted
                            wrapMode: Text.Wrap
                        }

                        // 0021 (ADR-0166): the firmware in the device (application MCU only).
                        Label {
                            textFormat: Text.PlainText
                            objectName: "deviceFirmware"
                            Layout.fillWidth: true
                            visible: kindCard.modelData.kind === FirmwareSourceViewModel.AppFirmware
                                     && card.vm.deviceFirmwareText !== ""
                            text: card.vm.deviceFirmwareText
                            font: Theme.fontUiSmall
                            color: Theme.text
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }

    // Prerequisite (nRF Util), shown as soon as the kind is chosen.
    Rectangle {
        objectName: "prerequisite"
        Layout.fillWidth: true
        visible: card.vm ? card.vm.prerequisiteText !== "" : false
        implicitHeight: prerequisiteRow.implicitHeight + 2 * Theme.spacingS
        radius: Theme.radiusS
        color: Theme.errorPanelBg
        Accessible.role: Accessible.AlertMessage
        Accessible.name: card.vm ? card.vm.prerequisiteText : ""

        // Plan 0038 A0: the buttons wrap below the message (German, minimum window size).
        ColumnLayout {
            id: prerequisiteRow

            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Icon {
                    symbol: "error"
                    color: Theme.error
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: card.vm ? card.vm.prerequisiteText : ""
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                HButton {
                    objectName: "prerequisiteSettings"
                    text: qsTr("Set path in Settings")
                    onClicked: AppViewModel.openSettings("nrfutil")
                }

                // Plan 0038 A0 (ADR-0263): Nordic's direct download for this OS, then our guide.
                HButton {
                    objectName: "nrfUtilDownload"
                    kind: "ghost"
                    toolTipText: card.vm ? card.vm.nrfUtilDownloadUrl : ""
                    text: qsTr("Download nRF Util ↗")
                    onClicked: Qt.openUrlExternally(card.vm.nrfUtilDownloadUrl)
                }

                HButton {
                    objectName: "nrfUtilInstall"
                    kind: "ghost"
                    text: qsTr("How to install ↗")
                    onClicked: Qt.openUrlExternally(AppViewModel.docsLink("install-nrf-util"))
                }
            }
        }
    }
}
