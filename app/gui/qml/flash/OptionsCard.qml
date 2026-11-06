// Flash step 3: options. "Halt the CPU after flashing" (not for modem firmware) and "Allow writing
// UICR" with a warning, only when the image has UICR records (ADR-0036). Plan 0039: on an STM32
// chip "Erase the whole chip first" and "Keep data EEPROM" (off while erasing the chip).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FlashCard {
    id: card

    required property FlashViewModel vm

    number: 3
    title: qsTr("Options")
    visible: card.vm ? card.vm.haltAvailable || card.vm.source.hasUicr || card.vm.stm32 : false

    CheckBox {
        objectName: "eraseChipOption"
        visible: card.vm ? card.vm.stm32 : false
        text: qsTr("Erase the whole chip first")
        checked: card.vm ? card.vm.eraseChip : false
        onToggled: card.vm.eraseChip = checked
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: card.vm ? card.vm.stm32 : false
        spacing: Theme.spacingXs

        CheckBox {
            objectName: "keepEepromOption"
            text: qsTr("Keep data EEPROM")
            enabled: card.vm ? !card.vm.eraseChip : false
            checked: card.vm ? card.vm.keepEeprom : true
            onToggled: card.vm.keepEeprom = checked
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingXl
            visible: card.vm ? card.vm.source.hasEeprom && card.vm.keepEeprom : false
            text: qsTr("The firmware contains data EEPROM records. Turn off Keep data EEPROM to write them.")
            font: Theme.fontUiSmall
            color: Theme.warning
            wrapMode: Text.Wrap
        }
    }

    CheckBox {
        objectName: "haltOption"
        visible: card.vm ? card.vm.haltAvailable : false
        text: qsTr("Halt the CPU after flashing")
        checked: card.vm ? card.vm.halt : false
        onToggled: card.vm.halt = checked
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: card.vm ? card.vm.source.hasUicr : false
        spacing: Theme.spacingXs

        CheckBox {
            objectName: "uicrOption"
            text: qsTr("Allow writing UICR")
            checked: card.vm ? card.vm.source.allowUicr : false
            onToggled: card.vm.source.allowUicr = checked
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingXl
            text: qsTr("The firmware contains UICR configuration. Writing it changes the device configuration; the Product Information Block is kept.")
            font: Theme.fontUiSmall
            color: Theme.warning
            wrapMode: Text.Wrap
        }
    }
}
