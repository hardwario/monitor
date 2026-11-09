// The Embedded step "flash-summary" of the flash wizard (plan/features/0010 step 5, "Ready to
// flash"), bound to FlashWizard (`vm`): what is flashed where (plain text: it names the file and the
// probe's serial number, ADR-0184), what is replaced and what is kept, and why [Flash] is disabled
// (e.g. another operation runs on the tab, internal.busy). Presentation only.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: step

    property FlashWizard vm: null

    objectName: "flashSummaryStep"
    spacing: Theme.spacingM

    Label {
        objectName: "flashSummary"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        text: step.vm ? step.vm.summaryText : ""
        font: Theme.fontUiStrong
        color: Theme.text
        wrapMode: Text.Wrap
    }

    Label {
        objectName: "flashSummaryNote"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        text: step.vm ? step.vm.summaryNote : ""
        color: Theme.text
        wrapMode: Text.Wrap
    }

    RowLayout {
        Layout.fillWidth: true
        visible: step.vm !== null && step.vm.cannotFlashReason !== ""
        spacing: Theme.spacingS

        Icon {
            symbol: "info"
            color: Theme.textMuted
            size: 18
        }

        Label {
            objectName: "flashBlockedReason"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: step.vm ? step.vm.cannotFlashReason : ""
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }
}
