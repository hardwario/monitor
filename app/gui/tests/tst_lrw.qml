// Plan 0040 (B1-B3): the CHESTER LoRaWAN module - the Flash page's third card (designated
// lora-modem firmware, own file, the cabling note), the kind hint by address range, the LoRaWAN
// module wizard with its cable moves and the unlock confirmation of a protected module, and the
// `lrw` maintenance actions (expert setting) that open the wizard. The fake J-Link board has the
// APP and the LoRaWAN connector; the module answers only while the application MCU is held.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "Lrw"
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

    // An item of the open wizard dialog (a popup: not below the window's content item).
    function part(name) {
        const dialog = findChild(window, "wizardDialog");
        const items = dialog ? findAll(dialog.contentItem, name).filter(effectivelyVisible) : [];
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
        verify(testSupport.setSetting("advanced/expertActions", false));
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

    function cleanup() {
        if (AppViewModel.wizard.open)
            AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open, 10000);
        const dialog = findChild(window, "wizardDialog");
        tryVerify(() => !dialog || !dialog.visible);
        verify(testSupport.moveCable("app"));
    }

    function openBoard(rdp) {
        verify(testSupport.useLrwBoard(rdp));
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.showPage("flash");
        tryVerify(() => find("flashPage") !== null);
        tryVerify(() => find("kindCard" + FirmwareSourceViewModel.LrwFirmware) !== null, 5000);
        waitForRendering(window.contentItem);
        return testSupport.flashPage(0);
    }

    function test_cardOffersTheModuleFirmware() {
        const flash = openBoard(0);
        const card = find("kindCard" + FirmwareSourceViewModel.LrwFirmware);
        compare(card.Accessible.name, "LoRaWAN module");
        // Three cards side by side at 1280 px.
        const first = find("kindCard" + FirmwareSourceViewModel.AppFirmware);
        compare(card.mapToItem(null, 0, 0).y, first.mapToItem(null, 0, 0).y);
        mouseClick(card);
        compare(flash.kind, FirmwareSourceViewModel.LrwFirmware);
        tryVerify(() => find("designatedRadio") !== null);
        verify(find("designatedRadio").checked);
        compare(find("designatedRadio").text, "lora-modem for CHESTER 1.4.1 (recommended)");
        compare(find("designatedDescription").text,
                "Open LoRaWAN modem firmware by HARDWARIO for the Murata module");
        verify(find("cableNoteText").text.startsWith(
                   "Connect the J-Link to the programming connector of the LoRaWAN module."));
        verify(find("eraseChipOption") !== null);
        verify(!find("haltOption"));
        verify(!find("lteUpdateCard"));
        mouseClick(find("ownFileRadio"));
        compare(flash.source.lteSource, FirmwareSourceViewModel.OwnFile);
        verify(find("dropText") !== null);
        verify(find("cableNoteText") !== null); // always shown
    }

    function test_kindHintSwitchesToTheModule() {
        const flash = openBoard(0);
        compare(flash.kind, FirmwareSourceViewModel.AppFirmware);
        flash.source.loadFile(testSupport.writeModuleHex("lora-modem.hex", 1024));
        tryVerify(() => find("kindHint") !== null, 5000);
        compare(find("kindHint").Accessible.name, "This is LoRaWAN module firmware — switch?");
        mouseClick(find("switchKind"));
        compare(flash.kind, FirmwareSourceViewModel.LrwFirmware);
        compare(flash.source.lteSource, FirmwareSourceViewModel.OwnFile);
        verify(!find("kindHint"));
        tryVerify(() => find("flashButton").enabled, 5000);
    }

    // Flash: the wizard asks for the cable moves; the protected module needs the unlock.
    function test_flashRunsTheGuidedWizard() {
        const flash = openBoard(1);
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LrwFirmware));
        tryVerify(() => find("ownFileRadio") !== null);
        mouseClick(find("ownFileRadio"));
        flash.source.loadFile(testSupport.writeModuleHex("module.hex", 1024));
        tryVerify(() => find("flashButton").enabled, 5000);
        mouseClick(find("flashButton"));
        const dialog = findChild(window, "wizardDialog");
        tryVerify(() => dialog.opened);
        const wizard = testSupport.openWizard();
        verify(wizard !== null);
        compare(AppViewModel.wizard.wizardId, "lrw-update");
        compare(flash.opState, FlashViewModel.Idle);
        tryVerify(() => wizard.illustration === "chester-lrw-cable", 20000);
        compare(wizard.stage, LrwUpdateWizard.Connector);
        tryCompare(part("lrwPrompt"), "text",
                   "The application MCU is stopped. Move the J-Link cable to the LoRaWAN connector of the LoRaWAN module.");
        compare(findChild(dialog, "wizardHeading").text, "Connect the J-Link to the LoRaWAN connector");
        wait(300); // the first check of the connector has ended (a move drops the connection)
        verify(testSupport.moveCable("lrw"));
        tryVerify(() => wizard.stage === LrwUpdateWizard.UnlockConfirmation, 20000);
        verify(part("lrwUnlockText").text.endsWith("the module's original Murata firmware is removed."));
        verify(!part("lrwPrompt"));
        verify(AppViewModel.wizard.canPrimary);
        AppViewModel.wizard.primary();
        // The APP connector (the fake's CTRL-AP may answer behind the LoRaWAN connector too, so the
        // wizard can be past this step already).
        tryVerify(() => wizard.illustration === "chester-app-cable" || wizard.stage === LrwUpdateWizard.Summary, 60000);
        wait(300);
        verify(testSupport.moveCable("app"));
        tryVerify(() => wizard.stage === LrwUpdateWizard.Summary, 20000);
        compare(findChild(dialog, "wizardHeading").text, "LoRaWAN module flashed with module.hex.");
        AppViewModel.wizard.action("open-console");
        tryVerify(() => !AppViewModel.wizard.open);
    }

    // Expert maintenance: the module is the third MCU; its actions open the wizard.
    function test_maintenanceOfTheModule() {
        verify(testSupport.setSetting("advanced/expertActions", true));
        const flash = openBoard(0);
        mouseClick(find("maintenanceHeader"));
        tryVerify(() => find("maintenanceTarget_lrw") !== null);
        waitForRendering(window.contentItem); // the choice above the buttons is laid out
        compare(find("maintenanceTarget_lrw").text, "LoRaWAN module");
        mouseClick(find("maintenanceTarget_lrw"));
        compare(flash.maintenanceTarget, "lrw");
        tryVerify(() => !find("resetHaltButton"));
        verify(!find("eraseCodeButton"));
        compare(find("eraseAllButton").text, "Erase chip…");
        mouseClick(find("recoverButton"));
        const confirm = findChild(find("flashPage"), "maintenanceConfirm");
        tryVerify(() => confirm.opened);
        compare(confirm.title, "Unlock the LoRaWAN module?");
        compare(confirm.consequence, "The module's original Murata firmware is removed. This cannot be undone.");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !confirm.visible);
        mouseClick(find("resetButton"));
        const dialog = findChild(window, "wizardDialog");
        tryVerify(() => dialog.opened);
        compare(AppViewModel.wizard.wizardId, "lrw-update");
        compare(AppViewModel.wizard.title, "Reset the LoRaWAN module");
    }
}
