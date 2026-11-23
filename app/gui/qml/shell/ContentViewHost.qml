// The views shown over a page area (plan 0052, ADR-0386, ADR-0387): the content views of
// AppViewModel.overlayView (report, J-Link firmware update, join and share a support session,
// keyboard shortcuts; plan 0056: the mode choice is the setup wizard's Welcome) and, while none is open, the global wizard (setup). One host per page
// area (each device tab's PageHost and the Settings tab); only the host of the current tab
// (`current`) shows a view, so a view is never instantiated twice at once. While it shows a view
// the host covers the page area and the page beneath is hidden (`shown`); `closed` is emitted when
// the view goes away, so the page can take the focus back.
pragma ComponentBehavior: Bound

import QtQuick
import HardwarioMonitor

Item {
    id: viewHost

    // This host's page area is the current one (its tab is current).
    property bool current: false
    readonly property string viewId: AppViewModel.overlayView !== "" ? AppViewModel.overlayView
                                     : AppViewModel.globalWizard.open ? "globalWizard" : ""
    readonly property bool shown: viewHost.current && viewHost.viewId !== ""

    signal closed

    objectName: "contentViewHost"
    visible: viewHost.shown
    onShownChanged: if (!viewHost.shown) viewHost.closed()

    Loader {
        id: loader

        anchors.fill: parent
        active: viewHost.shown
        sourceComponent: {
            switch (viewHost.viewId) {
            case "report":
                return reportView;
            case "probeFirmware":
                return probeFirmwareView;
            case "join":
                return joinView;
            case "share":
                return shareView;
            case "shortcuts":
                return shortcutsView;
            case "globalWizard":
                return globalWizardView;
            default:
                return null;
            }
        }
    }

    Component {
        id: reportView

        ReportView {}
    }

    Component {
        id: probeFirmwareView

        ProbeFirmwareView {}
    }

    Component {
        id: joinView

        JoinView {}
    }

    Component {
        id: shareView

        ShareView {}
    }

    Component {
        id: shortcutsView

        ShortcutsView {}
    }

    Component {
        id: globalWizardView

        WizardView {
            objectName: "globalWizardView"
            wizard: AppViewModel.globalWizard
        }
    }
}
