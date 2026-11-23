// The wizard control "wizard-error" (plan/features/0009): the cause and the suggestion of an error
// as plain text (ADR-0184: runtime texts never go into a step's StyledText body). Used by the
// troubleshooting introduction ("Probably: …") and by the Connect wizard's failed connection.
// `vm` is the wizard; it has the property `shownError` (ErrorInfo). Option `showMessage` (ADR-0290).
// Presentation only.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import HardwarioMonitor

// An Item with a Column (ADR-0292): the implicit height does not depend on the dialog's width.
Item {
    id: step

    property var vm: null
    // Plan 0056 (embedded contract): the WizardViewModel showing this control (set by WizardView).
    property WizardViewModel host: null
    // ADR-0290 option: false when the step's heading is the error's message already.
    property bool showMessage: true
    readonly property var shownError: step.vm !== null ? step.vm.shownError : undefined
    readonly property bool hasError: step.shownError !== undefined && step.shownError.valid

    objectName: "wizardErrorStep"
    implicitHeight: column.implicitHeight

    Column {
        id: column

        width: step.width
        spacing: Theme.spacingS

        Label {
            objectName: "wizardErrorMessage"
            width: column.width
            visible: step.hasError && step.showMessage
            text: step.hasError ? step.shownError.message : ""
            textFormat: Text.PlainText
            font: Theme.fontUiStrong
            color: Theme.text
            wrapMode: Text.Wrap
        }

        Label {
            objectName: "wizardErrorCause"
            width: column.width
            visible: step.hasError && step.shownError.cause !== ""
            text: step.hasError ? qsTr("Probably: %1").arg(step.shownError.cause) : ""
            textFormat: Text.PlainText
            color: Theme.text
            wrapMode: Text.Wrap
        }

        Label {
            objectName: "wizardErrorSuggestion"
            width: column.width
            visible: step.hasError && step.shownError.suggestion !== ""
            text: step.hasError ? qsTr("What to do: %1").arg(step.shownError.suggestion) : ""
            textFormat: Text.PlainText
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }
}
