// A wizard in the page area (plan 0052, ADR-0386; the engine is ADR-0067): a device tab's wizard
// replaces the tab's page area (PageHost, page id "wizard"), a global wizard is shown like a content
// view (ContentViewHost). Header (ViewFrame): the title, the step list (compact "Step 2 of 4"
// below 800 px view width) and × (Cancel). Content: the current step rendered by its kind (Info,
// Checks, Choice, Embedded, Commands, Done), scrolling with the ADR-0384 hints. Footer, pinned:
// Back (left; hidden on the first step and while busy), the step's actions, the default button and
// Cancel. Enter = default button, Esc = Cancel while the view has focus (asks only while the wizard
// runs an operation). The focus moves to the step heading when the view is shown and on every new
// step. Presentation only: `wizard` (DeviceTabViewModel.wizard or AppViewModel.globalWizard).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ViewFrame {
    id: view

    property WizardViewModel wizard: null
    readonly property var step: view.wizard ? view.wizard.step : ({})
    readonly property bool compact: view.width < 800
    // A new step: focus moves to its heading and the step is announced.
    readonly property string stepId: view.step.id !== undefined ? view.step.id : ""

    objectName: "wizardView"
    namePrefix: "wizard"
    title: view.wizard ? view.wizard.title : ""
    closeText: qsTr("Cancel")
    closeEnabled: view.wizard !== null && view.wizard.canCancel
    onCloseRequested: view.wizard.cancel(false)
    onDefaultRequested: view.wizard.primary()

    function focusHeading() {
        if (!view.visible)
            return;
        heading.forceActiveFocus();
        if (view.wizard && view.wizard.stepAnnouncement !== "")
            heading.Accessible.announce(view.wizard.stepAnnouncement);
    }

    onVisibleChanged: if (view.visible) view.focusHeading()
    onStepIdChanged: view.focusHeading()
    Component.onCompleted: view.focusHeading()

    Connections {
        target: view.wizard

        function onCancelConfirmationNeeded(text: string) {
            cancelConfirm.body = text;
            cancelConfirm.open();
        }

        function onRaiseRequested() {
            view.focusHeading();
        }
    }

    headerData: WizardStepList {
        objectName: "wizardStepList"
        Layout.fillWidth: true
        steps: view.wizard ? view.wizard.steps : []
        compact: view.compact
        compactText: view.wizard ? view.wizard.stepLabel : ""
        visible: view.wizard !== null && view.wizard.currentIndex >= 0
    }

    Illustration {
        objectName: "wizardIllustration"
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: Math.min(480, view.columnWidth)
        Layout.preferredHeight: Layout.preferredWidth * 270 / 480
        visible: view.step.illustration !== undefined && view.step.illustration !== ""
        slot: view.step.illustration !== undefined ? view.step.illustration : ""
        caption: view.step.heading !== undefined ? view.step.heading : ""
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingS

        Icon {
            visible: view.step.kind === StepKind.Done
            symbol: "success"
            color: Theme.success
            size: 28
        }

        Label {
            id: heading

            textFormat: Text.PlainText
            objectName: "wizardHeading"
            Layout.fillWidth: true
            text: view.step.heading !== undefined ? view.step.heading : ""
            font: Theme.fontTitle
            color: Theme.text
            wrapMode: Text.Wrap
            focusPolicy: Qt.StrongFocus
            activeFocusOnTab: true
            Accessible.role: Accessible.Heading
            Accessible.name: heading.text
            Keys.onReturnPressed: view.wizard.primary()
            Keys.onEnterPressed: view.wizard.primary()
        }
    }

    Label {
        objectName: "wizardBody"
        Layout.fillWidth: true
        visible: text !== ""
        text: view.step.body !== undefined ? view.step.body : ""
        textFormat: Text.StyledText
        color: Theme.text
        wrapMode: Text.Wrap
    }

    HButton {
        objectName: "wizardLink"
        visible: view.step.linkText !== undefined && view.step.linkText !== ""
        kind: "ghost"
        text: view.step.linkText !== undefined ? view.step.linkText + " ↗" : ""
        onClicked: Qt.openUrlExternally(view.step.linkUrl)
    }

    // Created on a Checks step only: a hidden CheckList outside the layout would size itself from
    // its own width (a recursive rearrange).
    Loader {
        Layout.fillWidth: true
        active: view.step.kind === StepKind.Checks
        visible: active

        sourceComponent: CheckList {
            objectName: "wizardChecks"
            model: view.wizard ? view.wizard.checks : null
        }
    }

    ColumnLayout {
        objectName: "wizardChoices"
        Layout.fillWidth: true
        visible: view.step.kind === StepKind.Choice
        spacing: Theme.spacingS

        Repeater {
            model: view.step.kind === StepKind.Choice ? view.step.choices : []

            delegate: AbstractButton {
                id: card

                required property var modelData

                objectName: "wizardChoice"
                Layout.fillWidth: true
                checkable: true
                checked: card.modelData.selected === true
                focusPolicy: Qt.StrongFocus
                implicitHeight: cardContent.implicitHeight + 2 * Theme.spacingM
                Accessible.role: Accessible.RadioButton
                Accessible.name: card.modelData.title
                Accessible.description: card.modelData.subtitle !== undefined ? card.modelData.subtitle : ""
                onClicked: view.wizard.choose(card.modelData.id)

                background: Rectangle {
                    radius: Theme.radiusM
                    color: card.checked ? Theme.accentSoft : Theme.bg
                    border.color: card.checked ? Theme.accent : Theme.border
                    border.width: card.checked ? 2 : 1

                    FocusRing {
                        control: card
                    }
                }

                contentItem: ColumnLayout {
                    id: cardContent

                    spacing: 2

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingM
                        Layout.rightMargin: Theme.spacingM
                        text: card.modelData.title
                        font: Theme.fontUiStrong
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingM
                        Layout.rightMargin: Theme.spacingM
                        visible: text !== ""
                        text: card.modelData.subtitle !== undefined ? card.modelData.subtitle : ""
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    Loader {
        id: embeddedLoader

        objectName: "wizardEmbedded"
        Layout.fillWidth: true
        // Plan 0008 (ADR-0292): a step of any kind may name a control (below its own content).
        active: view.step.embedded !== undefined && view.step.embedded !== ""
        visible: embeddedLoader.active
        source: view.wizard ? view.wizard.embeddedSource : ""
        // ADR-0290: the step's embeddedOptions (e.g. readOnly of the probe picker).
        onLoaded: if (view.wizard) view.wizard.applyEmbeddedOptions(embeddedLoader.item)

        Binding {
            target: embeddedLoader.item
            property: "vm"
            value: view.wizard ? view.wizard.embedded : null
            when: embeddedLoader.status === Loader.Ready
        }

        // Plan 0056 (embedded contract): every registered control declares `host`.
        Binding {
            target: embeddedLoader.item
            property: "host"
            value: view.wizard
            when: embeddedLoader.status === Loader.Ready
        }

        Connections {
            target: view.wizard

            function onChanged() {
                if (embeddedLoader.status === Loader.Ready)
                    view.wizard.applyEmbeddedOptions(embeddedLoader.item);
            }
        }

        // ADR-0384: an embedded step may ask to scroll a part of it into view.
        Connections {
            target: embeddedLoader.item
            ignoreUnknownSignals: true

            function onScrollIntoViewRequested(item: Item) {
                view.ensureVisible(item);
            }
        }
    }

    CommandBlock {
        objectName: "wizardCommands"
        Layout.fillWidth: true
        visible: view.step.kind === StepKind.Commands
        commands: view.step.commands !== undefined ? view.step.commands : []
        onCopyRequested: text => view.wizard.copyText(text)
    }

    footerData: RowLayout {
        objectName: "wizardFooter"
        Layout.fillWidth: true
        spacing: Theme.spacingS

        HButton {
            objectName: "wizardBack"
            visible: view.wizard !== null && view.wizard.canBack
            text: qsTr("Back")
            onClicked: view.wizard.back()
        }

        Item {
            Layout.fillWidth: true
        }

        Repeater {
            model: view.step.actions !== undefined ? view.step.actions : []

            delegate: HButton {
                required property var modelData

                objectName: "wizardAction"
                // ADR-0291: an action may be shown disabled (its text says why).
                enabled: modelData.enabled !== false
                text: modelData.text
                onClicked: view.wizard.action(modelData.id)
            }
        }

        HButton {
            objectName: "wizardPrimary"
            visible: view.step.primaryText !== undefined && view.step.primaryText !== ""
            kind: "primary"
            enabled: view.wizard !== null && view.wizard.canPrimary
            text: view.step.primaryText !== undefined ? view.step.primaryText : ""
            onClicked: view.wizard.primary()
        }

        HButton {
            objectName: "wizardCancel"
            text: view.step.kind === StepKind.Done ? qsTr("Close") : qsTr("Cancel")
            enabled: view.wizard !== null && !view.wizard.closing && view.wizard.canCancel
            onClicked: view.wizard.cancel(false)
        }
    }

    ConfirmDialog {
        id: cancelConfirm

        objectName: "wizardCancelConfirm"
        // Plan 0052: centred over the view that asked.
        parent: view
        anchors.centerIn: parent
        title: qsTr("Cancel the wizard?")
        // Plan 0033: a wizard may name the buttons ("Cancel anyway" / "Back").
        acceptText: view.wizard && view.wizard.cancelAcceptText !== "" ? view.wizard.cancelAcceptText : qsTr("Cancel and close")
        rejectText: view.wizard && view.wizard.cancelRejectText !== "" ? view.wizard.cancelRejectText : qsTr("Keep going")
        alternativeText: view.wizard ? view.wizard.cancelAlternativeText : "" // plan 0051 (ADR-0381): "Write back"
        onAccepted: view.wizard.cancel(true)
        onAlternative: view.wizard.cancelAlternative()
    }
}
