// The views shown over a page area (plan 0052, ADR-0386, ADR-0387): the global wizard (setup). One
// host per page area (each device tab's PageHost and the Settings tab); only the host of the
// current tab (`current`) shows the view, so a view is never instantiated twice at once. While it
// shows a view the host covers the page area and the page beneath is hidden (`shown`).
pragma ComponentBehavior: Bound

import QtQuick
import HardwarioMonitor

Item {
    id: viewHost

    // This host's page area is the current one (its tab is current).
    property bool current: false
    readonly property bool shown: viewHost.current && AppViewModel.globalWizard.open

    objectName: "contentViewHost"
    visible: viewHost.shown

    Loader {
        id: loader

        anchors.fill: parent
        active: viewHost.shown

        sourceComponent: WizardView {
            objectName: "globalWizardView"
            wizard: AppViewModel.globalWizard
        }
    }
}
