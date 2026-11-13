// The global Settings page (Ctrl+,; plan/features/0006-m3-flash-settings.md, ADR-0059; shown by the
// Settings tab, 0021 ADR-0172): the
// sections registered in the PageRegistry (General, Console by 0005, J-Link, nRF Util, About), each
// under its title; only those the user mode shows (plan 0045), plus one opened by an error or a guide
// with a note (ADR-0361). Changes apply immediately; invalid input shows an inline error and is not saved.
// A settings file of a newer version is read-only: a banner and all controls disabled.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: page

    property DeviceTabViewModel tab
    property SettingsViewModel vm

    // The section still to scroll to (plan 0034 B2): kept until the page is laid out.
    property string pendingSection: ""

    // 0021 (ADR-0172): scrolls to a section ("Open Settings" of an error panel, a wizard link).
    function showSection(sectionId: string) {
        if (sectionId === "")
            return;
        page.pendingSection = sectionId;
        page.scrollToPending();
    }

    // Scrolls only once the section and the content below it are laid out (plan 0034 B2: on the
    // first open the Loaders are not laid out yet, contentY would clamp to 0); retried on layout
    // changes, and only a real scroll clears the request.
    function scrollToPending() {
        if (page.pendingSection === "")
            return;
        const flickable = scroll.contentItem as Flickable;
        for (let i = 0; i < sections.count; ++i) {
            const item = sections.itemAt(i);
            if (item === null || item.objectName !== "section_" + page.pendingSection)
                continue;
            if (item.y <= 0 || scroll.availableWidth <= 0 || flickable.height <= 0
                    || flickable.contentHeight < item.y + item.height)
                return; // not laid out yet: retried by the layout change
            flickable.contentY = Math.max(0, Math.min(item.y, flickable.contentHeight - flickable.height));
            page.pendingSection = "";
            AppViewModel.clearSettingsSection(); // used: never re-applied later (review F8)
            return;
        }
        if (sections.count > 0) {
            // An unknown section id: nothing to scroll to, the request is dropped.
            page.pendingSection = "";
            AppViewModel.clearSettingsSection();
        }
    }

    function retryScroll() {
        if (page.pendingSection !== "")
            deferredScroll.restart();
    }

    // Defers to the next turn of the event loop. A Timer instead of Qt.callLater: it dies with the
    // page, so a deferred scroll never runs on a page destroyed in the meantime (an error link that
    // opens Settings while the page closes in the same turn crashed the QML engine).
    function deferShowSection(sectionId: string) {
        if (sectionId === "")
            return;
        page.pendingSection = sectionId;
        deferredScroll.restart();
    }

    objectName: "settingsPage"
    onVisibleChanged: if (page.visible && page.vm) page.vm.pageShown()
    onVmChanged: {
        if (page.visible && page.vm)
            page.vm.pageShown();
        page.deferShowSection(AppViewModel.settingsSection);
    }

    Timer {
        id: deferredScroll

        interval: 0
        onTriggered: page.scrollToPending()
    }

    Connections {
        target: AppViewModel

        function onSettingsSectionRequested(sectionId: string) {
            page.deferShowSection(sectionId);
        }
    }

    Connections {
        target: scroll.contentItem as Flickable

        function onContentHeightChanged() {
            page.retryScroll();
        }

        function onHeightChanged() {
            page.retryScroll();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: page.vm !== null

        WarningBanner {
            objectName: "readOnlyBanner"
            Layout.fillWidth: true
            visible: page.vm !== null && page.vm.readOnly
            text: qsTr("Settings were created by a newer version and are read-only.")
        }

        // A failed write (io.write_failed, settings.newer_schema): the value was not saved.
        Rectangle {
            objectName: "writeError"
            Layout.fillWidth: true
            visible: page.vm !== null && page.vm.writeError !== ""
            implicitHeight: writeErrorText.implicitHeight + 2 * Theme.spacingS
            color: Theme.errorPanelBg
            Accessible.role: Accessible.AlertMessage
            Accessible.name: writeErrorText.text

            Label {
                id: writeErrorText

                textFormat: Text.PlainText
                anchors.fill: parent
                anchors.margins: Theme.spacingS
                anchors.leftMargin: Theme.spacingM
                text: page.vm ? qsTr("The setting was not saved: %1").arg(page.vm.writeError) : ""
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }

        ScrollView {
            id: scroll

            objectName: "settingsScroll"
            onAvailableWidthChanged: page.retryScroll()
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                x: Math.max(Theme.spacingXl, (scroll.availableWidth - width) / 2)
                width: Math.min(760, scroll.availableWidth - 2 * Theme.spacingXl)
                spacing: Theme.spacingL
                enabled: page.vm !== null && !page.vm.readOnly

                Item {
                    implicitHeight: Theme.spacingS
                }

                Label {
                    textFormat: Text.PlainText
                    objectName: "settingsTitle"
                    text: AppViewModel.pages.revision >= 0 ? AppViewModel.pages.title("settings") : ""
                    font: Theme.fontTitle
                    color: Theme.text
                    Accessible.role: Accessible.Heading
                }

                Repeater {
                    id: sections

                    model: page.vm ? page.vm.sections : []

                    delegate: ColumnLayout {
                        id: section

                        required property var modelData

                        objectName: "section_" + section.modelData.sectionId
                        Layout.fillWidth: true
                        spacing: Theme.spacingS
                        onYChanged: page.retryScroll()

                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingS
                            text: section.modelData.title
                            font: Theme.fontUiStrong
                            color: Theme.accent
                            Accessible.role: Accessible.Heading
                            Accessible.name: section.modelData.title
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                        }

                        // Plan 0045 (ADR-0361): a section the mode hides, opened by an error or a
                        // guide, is shown for this visit.
                        RevealNote {
                            Layout.fillWidth: true
                            visible: section.modelData.revealed
                            mode: section.modelData.minimumMode
                        }

                        Loader {
                            objectName: "sectionLoader"
                            Layout.fillWidth: true
                            source: section.modelData.qml
                        }
                    }
                }

                Item {
                    implicitHeight: Theme.spacingXl
                }
            }
        }
    }
}
