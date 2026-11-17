// "Create diagnostic report" (plan/features/0007, ADR-0075), a content view of the page area
// (plan 0052, ADR-0387): collecting (spinner, Cancel), then the read-only summary.txt, the privacy
// note and [Save report…] (native file dialog, Documents, hwio-diagnostics-YYYYMMDD-HHMMSS.zip) ·
// [Copy summary] · [Close]. A failed save shows an ErrorPanel (diag.report_write_failed); after
// saving, [Show in folder]. While collecting or saving the view stays with its tab on a tab switch.
// AppViewModel.report; × / Esc / Close = AppViewModel.closeView().
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HardwarioMonitor

ViewFrame {
    id: view

    readonly property ReportViewModel report: AppViewModel.report
    readonly property bool collecting: view.report.state === "collecting"

    objectName: "reportDialog"
    namePrefix: "report"
    closeObjectName: "reportHeaderClose"
    title: qsTr("Create diagnostic report")
    closeText: view.collecting ? qsTr("Cancel") : qsTr("Close")
    onCloseRequested: AppViewModel.closeView()
    Component.onCompleted: view.focusTitle()

    RowLayout {
        objectName: "reportCollecting"
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.spacingXl
        visible: view.collecting
        spacing: Theme.spacingS

        BusyIndicator {
            running: view.collecting && !AppViewModel.reducedMotion
        }

        Label {
            text: qsTr("Collecting information…")
            color: Theme.text
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    // The summary scrolls on its own (no wrapping); the notes below it stay in view.
    ScrollView {
        id: summaryScroll

        objectName: "reportSummary"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(160, (view.flickable ? view.flickable.height : 400) - 220)
        visible: !view.collecting
        clip: true

        TextArea {
            textFormat: TextEdit.PlainText
            objectName: "reportSummaryText"
            text: view.report.summary
            readOnly: true
            selectByMouse: true
            font: Theme.fontMonoSmall
            color: Theme.text
            wrapMode: TextEdit.NoWrap
            padding: Theme.spacingS
            topPadding: Theme.spacingS
            bottomPadding: Theme.spacingS
            Accessible.name: qsTr("Report summary")
            background: Rectangle {
                color: Theme.consoleBg
                border.color: Theme.border
                radius: Theme.radiusS
            }
        }
    }

    Label {
        objectName: "reportNote"
        Layout.fillWidth: true
        visible: !view.collecting
        text: qsTr("The report contains this summary, the recent application logs and the settings file. Your user name and home folder are replaced. The report stays on your computer — attach it to your support request.")
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    // 0025 (ADR-0186): the logs may hold device output; PlainText so "<redacted>" is shown.
    Label {
        objectName: "reportDeviceOutputWarning"
        Layout.fillWidth: true
        visible: !view.collecting
        textFormat: Text.PlainText
        text: qsTr("The logs may contain device output. Values that look like passwords, tokens or keys are replaced by <redacted>; check the report before sending it.")
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    ErrorPanel {
        objectName: "reportError"
        Layout.fillWidth: true
        visible: view.report.state === "error"
        error: view.report.error
        primaryAction: ErrorAction.Dismiss
        onActionTriggered: saveDialog.open()
    }

    Label {
        textFormat: Text.PlainText
        objectName: "reportSaved"
        Layout.fillWidth: true
        visible: view.report.state === "saved"
        text: qsTr("Saved to %1").arg(view.report.savedPath)
        color: Theme.success
        wrapMode: Text.WrapAnywhere
    }

    footerData: Flow {
        Layout.fillWidth: true
        layoutDirection: Qt.RightToLeft
        spacing: Theme.spacingS

        HButton {
            objectName: "reportClose"
            text: view.collecting ? qsTr("Cancel") : qsTr("Close")
            onClicked: AppViewModel.closeView()
        }

        HButton {
            objectName: "reportCopy"
            visible: !view.collecting
            text: qsTr("Copy summary")
            onClicked: view.report.copySummary()
        }

        HButton {
            objectName: "reportShowFolder"
            visible: view.report.state === "saved"
            text: qsTr("Show in folder")
            onClicked: view.report.showInFolder()
        }

        HButton {
            objectName: "reportSave"
            visible: !view.collecting
            enabled: view.report.state !== "saving"
            kind: "primary"
            text: qsTr("Save report…")
            onClicked: saveDialog.open()
        }
    }

    FileDialog {
        id: saveDialog

        title: qsTr("Save diagnostic report")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Zip archives (*.zip)")]
        currentFolder: view.report.defaultFolder
        selectedFile: view.report.defaultFolder + "/" + view.report.defaultFileName
        onAccepted: view.report.save(saveDialog.selectedFile)
    }
}
