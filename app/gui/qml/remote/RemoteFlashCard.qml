// The Flash page of a remote tab (plan/features/0044 § Joiner, step 4; ADR-0323): catalog firmware
// by ID only, and Erase / Recover / Reset of the application or LTE core; with the LTE core
// selected, the guided LTE update with the customer's recommended firmware (ADR-0363) instead of
// the firmware ID. Every request waits for
// the customer's confirmation; progress and the result are reported by the customer's Monitor
// (ADR-0369: the result names the operation and the time). ADR-0368: support answers the LTE
// update's questions here.
// With a web Terminal host only the explanation is shown. Presentation only: RemoteJoinViewModel.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: card

    property RemoteJoinViewModel join: null
    readonly property bool available: card.join !== null && card.join.operationsAvailable
    readonly property bool idle: card.join !== null && !card.join.operationPending
    readonly property string target: lteButton.checked ? "lte" : "app"

    objectName: "remoteFlashCard"
    spacing: Theme.spacingM

    Label {
        textFormat: Text.PlainText
        text: qsTr("Remote operations")
        font: Theme.fontTitle
        color: Theme.text
    }

    Label {
        objectName: "remoteOpsUnavailable"
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: !card.available
        text: card.join ? card.join.operationsUnavailableText : ""
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    Label {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: card.available
        text: qsTr("The customer confirms every request in their HARDWARIO Monitor. Only firmware from the HARDWARIO catalog can be flashed remotely.")
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    RowLayout {
        visible: card.available
        spacing: Theme.spacingS

        Label {
            textFormat: Text.PlainText
            text: qsTr("Target:")
            color: Theme.text
        }

        RadioButton {
            id: appButton

            objectName: "remoteTargetApp"
            text: qsTr("Application core")
            checked: true
        }

        RadioButton {
            id: lteButton

            objectName: "remoteTargetLte"
            text: qsTr("LTE modem core")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: card.available && appButton.checked
        spacing: Theme.spacingS

        TextField {
            id: firmwareField

            objectName: "remoteFirmwareId"
            Layout.fillWidth: true
            placeholderText: qsTr("Catalog firmware ID (32 hexadecimal digits)")
            font: Theme.fontMono
            Accessible.name: qsTr("Catalog firmware ID")
        }

        HButton {
            objectName: "remoteFlashButton"
            kind: "primary"
            text: qsTr("Ask to flash")
            enabled: card.idle && appButton.checked && card.join.isFirmwareId(firmwareField.text)
            onClicked: card.join.requestOperation("flash", "app", firmwareField.text)
        }
    }

    // ADR-0363: no ID field for the LTE core; the customer's Monitor updates to its recommended
    // version with the guided update (they move the J-Link cable).
    ColumnLayout {
        Layout.fillWidth: true
        visible: card.available && lteButton.checked
        spacing: Theme.spacingS

        Label {
            objectName: "remoteLteUpdateText"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: card.join && card.join.lteUpdateAvailable
                  ? qsTr("The customer's HARDWARIO Monitor updates the LTE firmware to its recommended version. The customer moves the J-Link cable between the APP and LTE connectors when asked.")
                  : qsTr("The customer's HARDWARIO Monitor cannot update the LTE firmware remotely.")
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }

        HButton {
            objectName: "remoteLteUpdateButton"
            kind: "primary"
            text: qsTr("Ask to update LTE firmware")
            enabled: card.idle && card.join.lteUpdateAvailable
            onClicked: card.join.requestOperation("lteUpdate", "lte")
        }
    }

    Flow {
        Layout.fillWidth: true
        visible: card.available
        spacing: Theme.spacingS

        HButton {
            objectName: "remoteEraseButton"
            text: qsTr("Ask to erase")
            enabled: card.idle
            onClicked: card.join.requestOperation("erase", card.target)
        }

        HButton {
            objectName: "remoteRecoverButton"
            text: qsTr("Ask to recover")
            enabled: card.idle
            onClicked: card.join.requestOperation("recover", card.target)
        }

        HButton {
            objectName: "remoteResetButton"
            text: qsTr("Ask to reset")
            enabled: card.idle
            onClicked: card.join.requestOperation("reset", card.target)
        }
    }

    ProgressBar {
        objectName: "remoteOperationProgress"
        Layout.fillWidth: true
        visible: card.join !== null && card.join.operationPending
        indeterminate: card.join !== null && card.join.operationProgress < 0
        value: card.join !== null && card.join.operationProgress >= 0 ? card.join.operationProgress : 0
    }

    // ADR-0368: the customer's Monitor asks support (Continue before the erase, Continue without a
    // copy); the customer only follows the cable instructions.
    ColumnLayout {
        objectName: "remoteQuestion"
        Layout.fillWidth: true
        visible: card.join !== null && card.join.operationQuestion !== ""
        spacing: Theme.spacingS

        Label {
            objectName: "remoteQuestionText"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: card.join ? card.join.operationQuestion : ""
            font: Theme.fontUiStrong
            color: Theme.text
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        RowLayout {
            spacing: Theme.spacingS

            HButton {
                objectName: "remoteQuestionYes"
                kind: "primary"
                text: qsTr("Continue")
                onClicked: card.join.answerQuestion(true)
            }

            HButton {
                objectName: "remoteQuestionNo"
                text: qsTr("Cancel")
                onClicked: card.join.answerQuestion(false)
            }
        }
    }

    // ADR-0369: the result names the operation and the time; an error stays until × (a success
    // clears when support leaves the page or starts another operation).
    RowLayout {
        Layout.fillWidth: true
        visible: status.text !== ""
        spacing: Theme.spacingS

        Label {
            id: status

            objectName: "remoteOperationStatus"
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: card.join ? card.join.operationStatus : ""
            color: card.join && card.join.operationFailed ? Theme.error : Theme.text
            wrapMode: Text.Wrap
        }

        HToolButton {
            objectName: "remoteResultDismiss"
            Layout.alignment: Qt.AlignTop
            visible: card.join !== null && card.join.resultDismissable
            iconName: "close"
            text: qsTr("Dismiss")
            display: AbstractButton.IconOnly
            onClicked: card.join.dismissResult()
        }
    }
}
