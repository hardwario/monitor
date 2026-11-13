// One device tab: device bar + page area (ADR-0043).
import QtQuick
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: view

    required property DeviceTabViewModel tab
    required property int index
    // The model clears `tab` when the row is removed, before the delegate is destroyed; the
    // children keep the last tab until then.
    property DeviceTabViewModel stableTab: null

    spacing: 0

    Binding on stableTab {
        value: view.tab
        when: view.tab !== null
        restoreMode: Binding.RestoreNone
    }

    DeviceBar {
        Layout.fillWidth: true
        visible: view.stableTab !== null
        tab: view.stableTab
    }

    // Plan 0044: the remote support session of this tab (on every page).
    RemoteBanner {
        Layout.fillWidth: true
        visible: hostVisible || joinVisible
        readonly property bool hostVisible: view.stableTab !== null && view.stableTab.remoteShare.sharing
        readonly property bool joinVisible: view.stableTab !== null && view.stableTab.remoteJoin.active
                                            && view.stableTab.remoteJoin.bannerText !== ""
        tab: view.stableTab
    }

    RemoteSessionDialogs {
        tab: view.stableTab
        tabIndex: view.index
    }

    PageHost {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: view.stableTab !== null
        tab: view.stableTab
    }
}
