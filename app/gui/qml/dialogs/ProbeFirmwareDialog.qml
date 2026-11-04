// "Update J-Link firmware?" (plan/features/0036-jlink-firmware-update-gui.md, ADR-0132): reading
// the J-Link (spinner), the confirmation (product, SN, current and new firmware strings, the
// "do not unplug" warning; [Cancel] [Update], no default button, Esc = Cancel), the update
// (spinner, no Cancel; Esc does nothing) and a failure (ErrorPanel with [Close]). ADR-0261: an
// unknown firmware status asks "Update anyway?"; a failure while quitting keeps the app open.
// AppViewModel.probeFirmware; presentation only (ADR-0003).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Dialog {
    id: dialog

    readonly property ProbeFirmwareViewModel vm: AppViewModel.probeFirmware
    readonly property int phase: dialog.vm.state
    readonly property bool working: dialog.phase === ProbeFirmwareViewModel.Reading
                                    || dialog.phase === ProbeFirmwareViewModel.Updating
    readonly property bool showsStrings: dialog.phase === ProbeFirmwareViewModel.Confirm
                                         || dialog.phase === ProbeFirmwareViewModel.Updating

    objectName: "probeFirmwareDialog"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, (parent ? parent.width : 560) - 48)
    padding: Theme.spacingXl
    // The update must not be interrupted: no Esc while it runs.
    closePolicy: dialog.phase === ProbeFirmwareViewModel.Updating ? Popup.NoAutoClose : Popup.CloseOnEscape
    visible: dialog.phase !== ProbeFirmwareViewModel.Idle
    title: dialog.phase === ProbeFirmwareViewModel.Confirm || dialog.phase === ProbeFirmwareViewModel.Reading
           ? qsTr("Update J-Link firmware?") : qsTr("J-Link firmware update")
    onClosed: if (dialog.phase !== ProbeFirmwareViewModel.Idle) dialog.vm.cancel()
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusL
        border.color: Theme.border
    }

    Connections {
        target: dialog.vm

        function onChanged() {
            // No default button (ADR-0132): the focus starts on Cancel.
            if (dialog.phase === ProbeFirmwareViewModel.Confirm)
                cancelButton.forceActiveFocus();
        }
    }

    header: Label {
        textFormat: Text.PlainText
        objectName: "probeFirmwareTitle"
        text: dialog.title
        font: Theme.fontTitle
        color: Theme.text
        padding: Theme.spacingXl
        bottomPadding: 0
        wrapMode: Text.Wrap
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingM

        Label {
            textFormat: Text.PlainText
            objectName: "probeFirmwareProbe"
            Layout.fillWidth: true
            visible: dialog.phase !== ProbeFirmwareViewModel.Failed
            text: dialog.vm.product !== "" ? qsTr("%1 · SN %2").arg(dialog.vm.product).arg(dialog.vm.serial)
                                           : qsTr("SN %1").arg(dialog.vm.serial)
            color: Theme.text
            wrapMode: Text.Wrap
        }

        RowLayout {
            objectName: "probeFirmwareWorking"
            Layout.fillWidth: true
            visible: dialog.working
            spacing: Theme.spacingS

            BusyIndicator {
                running: dialog.working && !AppViewModel.reducedMotion
            }

            Label {
                textFormat: Text.PlainText
                objectName: "probeFirmwareWorkingText"
                Layout.fillWidth: true
                text: dialog.phase === ProbeFirmwareViewModel.Updating ? qsTr("Updating J-Link firmware…")
                                                                       : qsTr("Reading the J-Link…")
                color: Theme.text
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        GridLayout {
            objectName: "probeFirmwareStrings"
            Layout.fillWidth: true
            visible: dialog.showsStrings
            columns: 2
            columnSpacing: Theme.spacingM
            rowSpacing: Theme.spacingXs

            Label {
                textFormat: Text.PlainText
                text: qsTr("Current:")
                color: Theme.textMuted
            }

            Label {
                textFormat: Text.PlainText
                objectName: "probeFirmwareCurrent"
                Layout.fillWidth: true
                text: dialog.vm.currentString
                font: Theme.fontMonoSmall
                color: Theme.text
                wrapMode: Text.WrapAnywhere
            }

            Label {
                textFormat: Text.PlainText
                text: qsTr("New:")
                color: Theme.textMuted
            }

            Label {
                textFormat: Text.PlainText
                objectName: "probeFirmwareNew"
                Layout.fillWidth: true
                text: dialog.vm.availableString
                font: Theme.fontMonoSmall
                color: Theme.text
                wrapMode: Text.WrapAnywhere
            }
        }

        // ADR-0261: the status could not be determined - the user decides explicitly.
        Label {
            textFormat: Text.PlainText
            objectName: "probeFirmwareUnknown"
            Layout.fillWidth: true
            visible: dialog.phase === ProbeFirmwareViewModel.Confirm && dialog.vm.statusUnknown
            text: qsTr("The J-Link firmware status could not be determined. Update anyway?")
            color: Theme.text
            wrapMode: Text.Wrap
        }

        Label {
            textFormat: Text.PlainText
            objectName: "probeFirmwareUnplugWarning"
            Layout.fillWidth: true
            visible: dialog.showsStrings
            text: qsTr("Do not unplug the J-Link during the update (about 10–30 s).")
            color: Theme.text
            font.bold: true
            wrapMode: Text.Wrap
        }

        Label {
            textFormat: Text.PlainText
            objectName: "probeFirmwareQuitNote"
            Layout.fillWidth: true
            visible: dialog.phase === ProbeFirmwareViewModel.Updating && dialog.vm.quitPending
            text: qsTr("HARDWARIO Monitor quits when the update has finished.")
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }

        ErrorPanel {
            objectName: "probeFirmwareError"
            Layout.fillWidth: true
            visible: dialog.phase === ProbeFirmwareViewModel.Failed && dialog.vm.error.valid
            error: dialog.vm.error
            // [Close] replaces the primary action; a dependency error keeps [Open Settings].
            customActionText: dialog.vm.error.primaryAction === ErrorAction.OpenSettings ? "" : qsTr("Close")
            secondaryActionText: dialog.vm.error.primaryAction === ErrorAction.OpenSettings ? qsTr("Close") : ""
            onCustomActionTriggered: dialog.vm.cancel()
            onSecondaryActionTriggered: dialog.vm.cancel()
            onActionTriggered: action => {
                if (action === ErrorAction.OpenSettings)
                    AppViewModel.openSettings(dialog.vm.error.settingsSection);
                dialog.vm.cancel();
            }
        }
    }

    footer: DialogButtonBox {
        objectName: "probeFirmwareButtons"
        visible: dialog.phase === ProbeFirmwareViewModel.Confirm || dialog.phase === ProbeFirmwareViewModel.Reading
        padding: Theme.spacingXl
        topPadding: 0
        background: null

        HButton {
            id: cancelButton

            objectName: "probeFirmwareCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.vm.cancel()
        }

        HButton {
            objectName: "probeFirmwareConfirm"
            kind: "primary"
            enabled: dialog.phase === ProbeFirmwareViewModel.Confirm
            text: qsTr("Update")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.vm.confirm()
        }
    }
}
