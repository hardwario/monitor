// The page area of a tab (page-extension contract, ADR-0059): the connection ErrorPanel and the
// warning banner on top, then the Choose device page or the current page. Each page root is
// loaded once on first use and gets the properties `tab` (DeviceTabViewModel) and `vm` (its view
// model).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: host

    required property DeviceTabViewModel tab
    readonly property bool choosing: host.tab.state === DeviceTabViewModel.ChoosingDevice

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Plan 0033 (ADR-0233, D8): the J-Link is on the LTE connector - a warning instead of the
        // console's error, with the LTE update and a retry (no automatic reconnect).
        Rectangle {
            objectName: "otherConnectorBanner"
            Layout.fillWidth: true
            visible: !host.choosing && host.tab.connection.otherConnector
            implicitHeight: otherConnectorRow.implicitHeight + 2 * Theme.spacingS
            color: Theme.warningPanelBg
            Accessible.role: Accessible.AlertMessage
            Accessible.name: host.tab.connection.otherConnectorText

            RowLayout {
                id: otherConnectorRow

                anchors.fill: parent
                anchors.topMargin: Theme.spacingS
                anchors.bottomMargin: Theme.spacingS
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingS

                Icon {
                    Layout.alignment: Qt.AlignTop
                    symbol: "warning"
                    color: Theme.warning
                }

                Label {
                    textFormat: Text.PlainText
                    objectName: "otherConnectorText"
                    Layout.fillWidth: true
                    text: host.tab.connection.otherConnectorText
                    color: Theme.text
                    wrapMode: Text.Wrap
                }

                HButton {
                    objectName: "updateLteButton"
                    visible: AppViewModel.launcher.canOpen("lte-update")
                    text: qsTr("Update LTE firmware…")
                    onClicked: host.tab.connection.updateLteFirmware()
                }

                HButton {
                    objectName: "otherConnectorRetry"
                    kind: "ghost"
                    text: qsTr("Retry")
                    onClicked: host.tab.connection.reconnect()
                }
            }
        }

        ErrorPanel {
            objectName: "connectionError"
            Layout.fillWidth: true
            Layout.margins: Theme.spacingM
            visible: !host.choosing && host.tab.connection.error.valid && !host.tab.connection.otherConnector
            error: host.tab.connection.error
            compact: Window.window !== null && Window.window.height < 700
            secondaryActionText: host.tab.connection.secondaryActionText
            customActionText: host.tab.connection.customActionText
            note: host.tab.connection.pairingHelp
            onSecondaryActionTriggered: host.tab.connection.triggerSecondaryAction()
            onCustomActionTriggered: host.tab.connection.triggerCustomAction()
            onActionTriggered: action => action === ErrorAction.OpenSettings ? AppViewModel.openSettings(host.tab.connection.error.settingsSection)
                                                                               : host.tab.connection.triggerErrorAction(action)
        }

        WarningBanner {
            objectName: "connectionWarning"
            Layout.fillWidth: true
            visible: !host.choosing && host.tab.connection.warning.valid
            text: host.tab.connection.warning.message
            details: host.tab.connection.warning
            onDismissed: host.tab.connection.dismissWarning()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ChooseDevicePage {
                anchors.fill: parent
                visible: host.choosing
                tab: host.tab
                picker: host.tab.picker
                active: host.choosing && host.tab === AppViewModel.currentTabViewModel
                        && AppViewModel.globalPage === ""
            }

            Repeater {
                model: AppViewModel.pages

                delegate: Loader {
                    id: pageLoader

                    required property string pageId
                    required property bool isGlobal
                    required property url qmlUrl
                    readonly property bool current: !host.choosing && !pageLoader.isGlobal
                                                    && host.tab.currentPage === pageLoader.pageId

                    objectName: "page_" + pageLoader.pageId
                    anchors.fill: parent
                    // Loaded on first use and kept (the page keeps its state while hidden).
                    active: pageLoader.current || pageLoader.status === Loader.Ready
                    visible: pageLoader.current
                    source: pageLoader.isGlobal ? "" : pageLoader.qmlUrl

                    Binding {
                        target: pageLoader.item
                        property: "tab"
                        value: host.tab
                        when: pageLoader.status === Loader.Ready
                    }

                    Binding {
                        target: pageLoader.item
                        property: "vm"
                        value: host.tab.pageViewModel(pageLoader.pageId)
                        when: pageLoader.status === Loader.Ready
                    }
                }
            }
        }
    }
}
