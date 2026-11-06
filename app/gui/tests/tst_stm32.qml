// Plan 0039 (C4): the STM32 chip device - the Flash page's single card with the .bin start address,
// the STM32 options (Keep data EEPROM off while erasing the chip), the maintenance actions
// [Erase chip…] / [Unlock (erase)…] with their ConfirmDialog (never accepted here), the result
// card's [Unlock…] after target.protected, and the Console page's "no console" state (ADR-0280 D8).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "Stm32"
    when: windowShown

    function findAll(parent, name) {
        let result = [];
        const children = parent && parent.children ? parent.children : [];
        for (let i = 0; i < children.length; ++i) {
            if (children[i].objectName === name)
                result.push(children[i]);
            result = result.concat(findAll(children[i], name));
        }
        return result;
    }

    function effectivelyVisible(item) {
        for (let current = item; current; current = current.parent) {
            if (!current.visible)
                return false;
        }
        return true;
    }

    function find(name) {
        const items = findAll(window.contentItem, name).filter(effectivelyVisible);
        return items.length > 0 ? items[0] : null;
    }

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
        window.requestActivate();
        tryVerify(() => window.active);
    }

    function cleanupTestCase() {
        window.destroy();
        wait(50);
        verify(testSupport.useScenario(""));
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "light";
        testSupport.resetTabs();
        window.width = 1280;
        window.height = 1400;
    }

    function openChip(rdp) {
        verify(testSupport.useScenario(`{"probes": [{"serialNumber": 801000001, "product": "Test Probe",
            "chip": {"family": "stm32l0", "devId": 1047, "flashKib": 64, "eepromBytes": 2048, "rdp": ${rdp}}}]}`));
        verify(testSupport.configureStm32Tab(0, "stm32l0-cat3-64k", 801000001, "Test Probe"));
        AppViewModel.showPage("flash");
        tryVerify(() => find("flashPage") !== null);
        tryVerify(() => find("kindCard2") !== null && find("kindCard2").width > 100, 5000); // Stm32Firmware
        waitForRendering(window.contentItem);
        return testSupport.flashPage(0);
    }

    function test_cardBinAddressAndOptions() {
        const flash = openChip(0);
        compare(flash.kind, FirmwareSourceViewModel.Stm32Firmware);
        verify(!find("kindCard0"));
        verify(!find("kindCard1"));
        verify(find("eraseChipOption") !== null);
        const keep = find("keepEepromOption");
        verify(keep.checked);
        mouseClick(find("eraseChipOption"));
        verify(flash.eraseChip);
        verify(!keep.checked);
        verify(!keep.enabled);
        mouseClick(find("eraseChipOption"));
        flash.source.loadFile(testSupport.writeBin("fw.bin", 256));
        tryVerify(() => find("binAddressField") !== null, 5000);
        compare(find("binAddressField").text, "0x08000000");
        verify(!find("binAddressError"));
        verify(find("flashButton").enabled);
        find("binAddressField").forceActiveFocus();
        find("binAddressField").selectAll();
        keyClick(Qt.Key_X);
        tryVerify(() => find("binAddressError") !== null);
        verify(!find("flashButton").enabled);
    }

    function test_maintenanceEraseChipAndUnlock() {
        openChip(0);
        mouseClick(find("maintenanceHeader"));
        tryVerify(() => find("eraseAllButton") !== null);
        compare(find("eraseAllButton").text, "Erase chip…");
        compare(find("recoverButton").text, "Unlock (erase)…");
        verify(!find("eraseCodeButton"));
        mouseClick(find("recoverButton"));
        const dialog = findChild(find("flashPage"), "maintenanceConfirm");
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Unlock the STM32L0 cat. 3, 64 KiB?");
        compare(dialog.items.length, 2);
        // ADR-0038: Cancel is the default; Esc cancels.
        tryVerify(() => findChild(dialog, "dialogReject").activeFocus);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
    }

    function test_protectedChipOffersUnlock() {
        const flash = openChip(1);
        flash.source.loadFile(testSupport.writeBin("fw.bin", 64));
        tryVerify(() => find("flashButton") !== null && find("flashButton").enabled, 5000);
        mouseClick(find("flashButton"));
        tryVerify(() => flash.opState === FlashViewModel.Failed, 20000);
        compare(flash.error.code, "target.protected");
        tryVerify(() => find("resultError") !== null);
        compare(flash.errorCustomActionText, "Unlock…");
    }

    function test_consoleExplainsNoConsole() {
        openChip(0);
        AppViewModel.showPage("console");
        tryVerify(() => find("noConsoleState") !== null, 5000);
        verify(!find("notConnectedState"));
        verify(!find("consoleSplit"));
    }
}
