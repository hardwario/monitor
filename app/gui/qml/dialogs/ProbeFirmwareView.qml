// "Update J-Link firmware?" (plan/features/0036-jlink-firmware-update-gui.md, ADR-0132), a content
// view of the page area (plan 0052, ADR-0387): reading the J-Link (spinner), the confirmation
// (product, SN, current and new firmware strings, the "do not unplug" warning; [Cancel] [Update],
// no default button: the focus starts on Cancel, Esc = Cancel), the update (spinner, no Cancel; Esc
// and × do nothing, a tab switch is refused) and a failure (ErrorPanel with [Close]). ADR-0261: an
// unknown firmware status asks "Update anyway?"; a failure while quitting keeps the app open.
// Opened by AppViewModel while AppViewModel.probeFirmware is not Idle; presentation only
// (ADR-0003).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ViewFrame {
    id: view

    readonly property ProbeFirmwareViewModel vm: AppViewModel.probeFirmware
    readonly property int phase: view.vm.state
    readonly property bool working: view.phase === ProbeFirmwareViewModel.Reading
                                    || view.phase === ProbeFirmwareViewModel.Updating
    readonly property bool showsStrings: view.phase === ProbeFirmwareViewModel.Confirm
                                         || view.phase === ProbeFirmwareViewModel.Updating

    objectName: "probeFirmwareDialog"
    namePrefix: "probeFirmware"
    closeObjectName: "probeFirmwareHeaderClose"
    // The update must not be interrupted: no × and no Esc while it runs.
    closeEnabled: view.phase !== ProbeFirmwareViewModel.Updating
    closeText: qsTr("Cancel")
    title: view.phase === ProbeFirmwareViewModel.Confirm || view.phase === ProbeFirmwareViewModel.Reading
           ? qsTr("Update J-Link firmware?") : qsTr("J-Link firmware update")
    onCloseRequested: AppViewModel.closeView()

    function focusStart() {
        // No default button (ADR-0132): the focus starts on Cancel.
        if (view.phase === ProbeFirmwareViewModel.Confirm)
            cancelButton.forceActiveFocus();
        else
            view.focusTitle();
    }

    Component.onCompleted: view.focusStart()

    Connections {
        target: view.vm

        function onChanged() {
            if (view.phase === ProbeFirmwareViewModel.Confirm)
                cancelButton.forceActiveFocus();
        }
    }

    Label {
        textFormat: Text.PlainText
        objectName: "probeFirmwareProbe"
        Layout.fillWidth: true
        visible: view.phase !== ProbeFirmwareViewModel.Failed
        text: view.vm.product !== "" ? qsTr("%1 · SN %2").arg(view.vm.product).arg(view.vm.serial)
                                       : qsTr("SN %1").arg(view.vm.serial)
        color: Theme.text
        wrapMode: Text.Wrap
    }

    RowLayout {
        objectName: "probeFirmwareWorking"
        Layout.fillWidth: true
        visible: view.working
        spacing: Theme.spacingS

        BusyIndicator {
            running: view.working && !AppViewModel.reducedMotion
        }

        Label {
            textFormat: Text.PlainText
            objectName: "probeFirmwareWorkingText"
            Layout.fillWidth: true
            text: view.phase === ProbeFirmwareViewModel.Updating ? qsTr("Updating J-Link firmware…")
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
        visible: view.showsStrings
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
            text: view.vm.currentString
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
            text: view.vm.availableString
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
        visible: view.phase === ProbeFirmwareViewModel.Confirm && view.vm.statusUnknown
        text: qsTr("The J-Link firmware status could not be determined. Update anyway?")
        color: Theme.text
        wrapMode: Text.Wrap
    }

    Label {
        textFormat: Text.PlainText
        objectName: "probeFirmwareUnplugWarning"
        Layout.fillWidth: true
        visible: view.showsStrings
        text: qsTr("Do not unplug the J-Link during the update (about 10–30 s).")
        color: Theme.text
        font.bold: true
        wrapMode: Text.Wrap
    }

    Label {
        textFormat: Text.PlainText
        objectName: "probeFirmwareQuitNote"
        Layout.fillWidth: true
        visible: view.phase === ProbeFirmwareViewModel.Updating && view.vm.quitPending
        text: qsTr("HARDWARIO Monitor quits when the update has finished.")
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    ErrorPanel {
        objectName: "probeFirmwareError"
        Layout.fillWidth: true
        visible: view.phase === ProbeFirmwareViewModel.Failed && view.vm.error.valid
        error: view.vm.error
        // Plan 0036 P5: with the troubleshooting wizard registered (plan 0009), "Help me fix
        // this" replaces [Close]: the view closes first, then the wizard opens with the error.
        readonly property bool offersWizard: AppViewModel.launcher.canOpen(view.vm.error.wizard)
        showWizardButton: false
        // [Close] replaces the primary action; a dependency error keeps [Open Settings].
        customActionText: view.vm.error.primaryAction === ErrorAction.OpenSettings ? "" : (offersWizard ? qsTr("Help me fix this") : qsTr("Close"))
        secondaryActionText: view.vm.error.primaryAction === ErrorAction.OpenSettings ? qsTr("Close") : ""
        onCustomActionTriggered: {
            if (offersWizard) {
                const error = view.vm.error;
                view.vm.cancel();
                AppViewModel.launcher.open(error.wizard, error, -1);
            } else {
                view.vm.cancel();
            }
        }
        onSecondaryActionTriggered: view.vm.cancel()
        onActionTriggered: action => {
            if (action === ErrorAction.OpenSettings)
                AppViewModel.openSettings(view.vm.error.settingsSection);
            view.vm.cancel();
        }
    }

    // [Cancel] [Update] at the right, in this Tab order.
    footerData: RowLayout {
        objectName: "probeFirmwareButtons"
        Layout.fillWidth: true
        visible: view.phase === ProbeFirmwareViewModel.Confirm || view.phase === ProbeFirmwareViewModel.Reading
        spacing: Theme.spacingS

        Item {
            Layout.fillWidth: true
        }

        HButton {
            id: cancelButton

            objectName: "probeFirmwareCancel"
            text: qsTr("Cancel")
            onClicked: AppViewModel.closeView()
        }

        HButton {
            objectName: "probeFirmwareConfirm"
            kind: "primary"
            enabled: view.phase === ProbeFirmwareViewModel.Confirm
            text: qsTr("Update")
            onClicked: view.vm.confirm()
        }
    }
}
