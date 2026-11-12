// The Flash page (plan 0006): kind cards, loading a file, the Flash bar, a flash through the fake
// J-Link library with the progress and result cards, the maintenance ConfirmDialog (ADR-0038:
// Cancel is the default and Esc cancels), "Guide me" hidden while the flash wizard is not
// registered, both themes, and the layout at 960 x 600 in Czech (geometry assertions).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "Flash"
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

    function rect(item) {
        const point = item.mapToItem(window.contentItem, 0, 0);
        return { x: point.x, y: point.y, w: item.width, h: item.height, name: item.objectName };
    }

    // Horizontally inside the window (the page scrolls vertically; with `vertical` also inside
    // the window height, for the sticky Flash bar).
    function checkInside(item, vertical) {
        const r = rect(item);
        verify(r.x >= -0.5 && r.x + r.w <= window.width + 0.5, `${r.name} is clipped: x=${r.x} w=${r.w}`);
        if (vertical)
            verify(r.y >= -0.5 && r.y + r.h <= window.height + 0.5, `${r.name} is clipped: y=${r.y} h=${r.h}`);
        if (item.implicitWidth !== undefined && item.text !== undefined)
            verify(item.width >= item.implicitWidth - 0.5, `${r.name} is narrower than its text`);
    }

    function checkNoOverlap(items) {
        for (let i = 0; i < items.length; ++i) {
            for (let j = i + 1; j < items.length; ++j) {
                const a = rect(items[i]);
                const b = rect(items[j]);
                const overlap = a.x < b.x + b.w - 0.5 && b.x < a.x + a.w - 0.5
                              && a.y < b.y + b.h - 0.5 && b.y < a.y + a.h - 0.5;
                verify(!overlap, `${a.name} overlaps ${b.name}`);
            }
        }
    }

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
        window.requestActivate();
        tryVerify(() => window.active);
    }

    function cleanupTestCase() {
        // The tab stays configured: no probe picker (and no probe scan) while the test ends.
        window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "light";
        testSupport.resetTabs();
        // Tall enough that every card is on screen for the clicks (the recent list grows).
        window.width = 1280;
        window.height = 1400;
        verify(testSupport.setSetting("ui/mode", "advanced"));
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.showPage("flash");
        tryVerify(() => find("flashPage") !== null);
        // A new tab starts with the kind of its target (ADR-0055; nothing is persisted).
        compare(vm().kind, FirmwareSourceViewModel.AppFirmware);
        // Laid out before the clicks.
        tryVerify(() => find("kindCard0") !== null && find("kindCard0").width > 100, 5000);
        waitForRendering(window.contentItem);
    }

    function vm() {
        return testSupport.flashPage(0);
    }

    function test_kindCardsAndSource() {
        const flash = vm();
        compare(flash.kind, FirmwareSourceViewModel.AppFirmware);
        // "Guide me" is wired but hidden until the flash wizard (0010) registers.
        verify(!find("guideMe"));
        compare(find("flashBarText").text, "Choose a firmware file or enter a catalog ID");
        verify(!find("flashButton").enabled);

        // Plan 0033 (ADR-0234): the LTE card - a text and [Update LTE firmware…] instead of the
        // firmware and options cards; the Flash button stays disabled.
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LteFirmware));
        compare(flash.kind, FirmwareSourceViewModel.LteFirmware);
        verify(!find("catalogIdField"));
        verify(!find("designatedRadio"));
        verify(find("lteUpdateCard") !== null);
        verify(find("lteUpdateButton").enabled);
        verify(!find("flashButton").enabled);
        compare(find("flashBarText").text,
                "Use Update LTE firmware… above: the J-Link has to be moved between the connectors.");
        tryVerify(() => find("prerequisite") !== null, 10000); // nRF Util is missing
        // Plan 0038 A0: direct Nordic download for this OS and our localized install guide.
        verify(find("nrfUtilDownload") !== null);
        verify(flash.nrfUtilDownloadUrl.startsWith("https://"), flash.nrfUtilDownloadUrl);
        verify(find("nrfUtilInstall") !== null);
        verify(AppViewModel.docsLink("install-nrf-util").endsWith(
                   "/install.md#nordic-nrf-util-lte-modem-firmware-only"));

        waitForRendering(window.contentItem, 2000);
        mouseClick(find("kindCard" + FirmwareSourceViewModel.AppFirmware));
        compare(flash.kind, FirmwareSourceViewModel.AppFirmware);
        flash.source.loadFile(testSupport.writeHex("app.hex", 4096, false));
        tryVerify(() => flash.source.sourceState === FirmwareSourceViewModel.Ready, 5000);
        verify(find("summaryText").text.startsWith("app.hex · Intel HEX"));
        compare(find("flashBarText").text, "Application firmware · app.hex");
        verify(find("flashButton").enabled);
        verify(find("recentRow0") !== null);
    }

    // Review F3: a click on the selected card keeps it selected.
    function test_clickOnTheSelectedKindKeepsIt() {
        const flash = vm();
        const card = find("kindCard" + FirmwareSourceViewModel.AppFirmware);
        verify(card.checked);
        mouseClick(card);
        compare(flash.kind, FirmwareSourceViewModel.AppFirmware);
        verify(card.checked);
        keyClick(Qt.Key_Space);
        verify(card.checked);
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LteFirmware));
        verify(!card.checked);
        verify(find("kindCard" + FirmwareSourceViewModel.LteFirmware).checked);
    }

    function test_kindHintSwitches() {
        const flash = vm();
        flash.source.loadFile(testSupport.writeModemZip());
        tryVerify(() => find("kindHint") !== null, 5000);
        compare(find("kindHint").Accessible.name, "This is LTE firmware — switch to LTE firmware?");
        mouseClick(find("switchKind"));
        compare(flash.kind, FirmwareSourceViewModel.LteFirmware);
        verify(!find("kindHint"));
        // Plan 0033: LTE firmware goes through the LTE update wizard.
        verify(find("lteUpdateButton") !== null);
    }

    // 0021 (ADR-0165), plan 0033: the designated LTE firmware downloads when its row is shown in
    // the LTE update wizard (a local fixture here); "Use your own file…" shows the drop zone.
    function test_lteDesignatedFirmware() {
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LteFirmware));
        mouseClick(find("lteUpdateButton"));
        const dialog = findChild(window, "wizardDialog");
        tryVerify(() => dialog.opened);
        const part = name => {
            const items = findAll(dialog.contentItem, name).filter(effectivelyVisible);
            return items.length > 0 ? items[0] : null;
        };
        const wizard = testSupport.openWizard();
        verify(wizard !== null);
        compare(wizard.stage, LteUpdateWizard.Firmware);
        tryVerify(() => part("designatedRadio") !== null);
        verify(part("designatedRadio").checked);
        compare(part("designatedRadio").text, "HARDWARIO LTE firmware 1.7.0 (recommended)");
        tryVerify(() => wizard.lteSource.designatedState === FirmwareSourceViewModel.DesignatedReady, 10000);
        tryCompare(part("designatedStatusText"), "text", "Ready");
        verify(!part("dropText"));
        tryVerify(() => part("ltePrerequisite") !== null, 10000); // nRF Util is missing
        verify(!AppViewModel.wizard.canPrimary);
        mouseClick(part("ownFileRadio"));
        compare(wizard.lteSource.lteSource, FirmwareSourceViewModel.OwnFile);
        verify(part("dropText") !== null);
        mouseClick(part("designatedRadio"));
        compare(wizard.lteSource.lteSource, FirmwareSourceViewModel.Designated);
        AppViewModel.wizard.cancel(false);
        tryVerify(() => !AppViewModel.wizard.open);
    }

    // Plan 0034 (B4): at 960 x 600 (Czech, own file) the missing nRF Util is shown above the firmware
    // card, inside the visible part of the wizard (it is why Next is disabled); the card carries no
    // Flash page step number ("Step 1 of 8" is the wizard's).
    function test_ltePrerequisiteVisible960() {
        AppViewModel.setLanguage("cs");
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LteFirmware));
        mouseClick(find("lteUpdateButton"));
        const dialog = findChild(window, "wizardDialog");
        tryVerify(() => dialog.opened);
        const part = name => {
            const items = findAll(dialog.contentItem, name).filter(effectivelyVisible);
            return items.length > 0 ? items[0] : null;
        };
        const wizard = testSupport.openWizard();
        tryVerify(() => part("ownFileRadio") !== null, 10000);
        mouseClick(part("ownFileRadio"));
        wizard.lteSource.loadFile(testSupport.writeModemZip());
        tryVerify(() => wizard.lteSource.sourceState === FirmwareSourceViewModel.Ready, 5000);
        window.width = 960;
        window.height = 600;
        tryVerify(() => part("ltePrerequisite") !== null, 10000);
        waitForRendering(window.contentItem);
        verify(!AppViewModel.wizard.canPrimary);
        const content = rect(findChild(dialog, "wizardContent"));
        const prerequisite = rect(part("ltePrerequisite"));
        const card = part("lteSourceCard");
        verify(prerequisite.y < rect(card).y, "the prerequisite is above the firmware card");
        verify(prerequisite.y >= content.y - 0.5 && prerequisite.y + prerequisite.h <= content.y + content.h + 0.5,
               `the prerequisite is visible without scrolling: y=${prerequisite.y} h=${prerequisite.h}, view y=${content.y} h=${content.h}`);
        compare(card.number, 0);
        compare(wizard.stage, LteUpdateWizard.Firmware);
    }

    // Plan 0046 (ADR-0366): on the APP connector the guide says what will happen and waits for
    // [Continue]; [Cancel] there closes it at once (nothing was changed). An own LTE .hex needs no
    // nRF Util.
    function test_lteUpdateAsksBeforeTheErase() {
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LteFirmware));
        mouseClick(find("lteUpdateButton"));
        const dialog = findChild(window, "wizardDialog");
        tryVerify(() => dialog.opened);
        const part = name => {
            const items = findAll(dialog.contentItem, name).filter(effectivelyVisible);
            return items.length > 0 ? items[0] : null;
        };
        const wizard = testSupport.openWizard();
        tryVerify(() => part("ownFileRadio") !== null, 10000);
        mouseClick(part("ownFileRadio"));
        wizard.lteSource.loadFile(testSupport.writeHex("lte.hex", 4096, false));
        tryVerify(() => AppViewModel.wizard.canPrimary, 10000);
        const primary = findChild(dialog, "wizardPrimary");
        mouseClick(primary);
        tryVerify(() => wizard.stage === LteUpdateWizard.Confirm, 20000);
        tryCompare(findChild(dialog, "wizardHeading"), "text", "Ready to update the LTE MCU");
        compare(primary.text, "Continue");
        verify(primary.enabled);
        compare(part("ltePrompt").text, "The application on this CHESTER will be erased. Then you move the J-Link "
                + "cable to the LTE connector, and at the end back to APP; the application is written back.");
        compare(AppViewModel.currentTabViewModel.busyText, "Paused for the LTE firmware update");
        const cancel = findChild(dialog, "wizardCancel");
        verify(cancel.enabled);
        mouseClick(cancel);
        tryVerify(() => !AppViewModel.wizard.open, 5000);
        tryCompare(AppViewModel.currentTabViewModel, "busyPage", "", 5000); // the closed wizard is deleted later
    }

    function cleanup() {
        if (AppViewModel.wizard.open)
            AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open);
        // The modal dialog blocks the clicks of the next test until it has really closed.
        const dialog = findChild(window, "wizardDialog");
        tryVerify(() => !dialog || !dialog.visible);
    }

    // 0021 (ADR-0167): "Find firmware IDs ↗" is the documentation in the UI language.
    function test_docsLink() {
        const link = find("docsLink");
        verify(link !== null);
        compare(vm().source.docsUrl.toString(),
                "https://docs.hardwario.com/chester/catalog-applications/catalog-applications/#application-firmware");
        AppViewModel.setLanguage("cs");
        tryCompare(link, "text", "Najít ID firmwaru ↗");
        compare(vm().source.docsUrl.toString(),
                "https://docs.hardwario.com/cs/chester/catalog-applications/catalog-applications/#application-firmware");
        AppViewModel.setLanguage("en");
    }

    function test_catalogIdValidator() {
        const field = find("catalogIdField");
        field.forceActiveFocus();
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_X);
        tryVerify(() => find("catalogIdError") !== null);
        compare(find("catalogIdError").text, "The ID has 32 characters 0–9 a–f.");
        field.clear();
    }

    function test_flashShowsProgressAndResult() {
        const flash = vm();
        flash.source.loadFile(testSupport.writeHex("app.hex", 4096, false));
        tryVerify(() => flash.canFlash, 5000);
        keySequence("Ctrl+Return");
        tryVerify(() => find("progressCard") !== null || find("resultCard") !== null, 5000);
        tryVerify(() => flash.opState === FlashViewModel.Succeeded, 30000);
        tryVerify(() => find("resultText") !== null);
        verify(find("resultText").text.startsWith("Firmware flashed and verified in"));
        verify(find("openConsole") !== null);
        // Review F6: no drops on the result card.
        verify(!findChild(find("flashPage"), "flashDropArea").enabled);
        mouseClick(find("flashAgain"));
        verify(findChild(find("flashPage"), "flashDropArea").enabled);
        compare(flash.opState, FlashViewModel.Idle);
        verify(find("flashBar") !== null);
    }

    // Plan 0021 phase 2 (ADR-0166): "In the device" on the Application card from `info show` after
    // the attach, "Now in the device" on the result card after the flash.
    function test_firmwareInTheDevice() {
        verify(testSupport.useScenario(JSON.stringify({
            libraryVersion: 81000,
            probes: [{ serialNumber: 801000001, product: "Test Probe",
                       rtt: { output: [{ atMs: 0, buffer: 0, text: "uart:~$ booted\n" }],
                              responses: { "info show": "fw-name: hio-chester-push-fm\nfw-version: v4.0.1\n" } } }]
        })));
        verify(!find("deviceFirmware") || !find("deviceFirmware").visible);
        AppViewModel.currentTabViewModel.connection.connectDevice();
        tryVerify(() => find("deviceFirmware") !== null && find("deviceFirmware").visible, 10000);
        compare(find("deviceFirmware").text, "In the device: hio-chester-push-fm v4.0.1");
        const flash = vm();
        flash.source.loadFile(testSupport.writeHex("app.hex", 4096, false));
        tryVerify(() => flash.canFlash, 5000);
        flash.flash();
        tryVerify(() => flash.opState === FlashViewModel.Succeeded, 30000);
        tryVerify(() => find("resultDeviceText") !== null && find("resultDeviceText").visible, 10000);
        compare(find("resultDeviceText").text, "Now in the device: hio-chester-push-fm v4.0.1");
        mouseClick(find("flashAgain"));
        AppViewModel.currentTabViewModel.connection.disconnectDevice();
        tryVerify(() => !find("deviceFirmware") || !find("deviceFirmware").visible);
        verify(testSupport.useScenario(""));
    }

    function test_uicrOption() {
        const flash = vm();
        flash.source.loadFile(testSupport.writeHex("uicr.hex", 64, true));
        tryVerify(() => find("uicrOption") !== null, 5000);
        verify(!find("flashButton").enabled);
        mouseClick(find("uicrOption"));
        verify(flash.source.allowUicr);
        verify(find("flashButton").enabled);
    }

    function test_maintenanceConfirmDialog() {
        const flash = vm();
        verify(!find("maintenanceActions"));
        mouseClick(find("maintenanceHeader"));
        verify(find("maintenanceActions") !== null);
        // ADR-0236 / ADR-0360: Erase everything and Recover only in Expert mode.
        verify(!find("eraseAllButton"));
        verify(!find("recoverButton"));
        verify(!find("maintenanceTargets"));
        verify(testSupport.setSetting("ui/mode", "expert"));
        tryVerify(() => find("eraseAllButton") !== null);
        verify(find("maintenanceTargets") !== null);
        waitForRendering(window.contentItem);
        mouseClick(find("eraseAllButton"));
        const dialog = findChild(find("flashPage"), "maintenanceConfirm");
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Erase everything on the application MCU?");
        compare(dialog.items.length, 3);
        // ADR-0038: Cancel has the focus (the destructive button is not the default); Esc cancels.
        const reject = findChild(dialog, "dialogReject");
        const accept = findChild(dialog, "dialogAccept");
        tryVerify(() => reject.activeFocus);
        compare(accept.kind, "danger");
        compare(accept.text, "Erase everything");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        compare(flash.opState, FlashViewModel.Idle);
        // Recover asks too.
        mouseClick(find("recoverButton"));
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Recover the application MCU?");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
    }

    // Review F1/F2: the LTE MCU has no code-only erase; its erase-all and recover name the PIB.
    // Plan 0033 (ADR-0236): the LTE MCU is chosen in Maintenance in Expert mode (ADR-0360).
    function test_lteMaintenance() {
        const flash = vm();
        verify(testSupport.setSetting("ui/mode", "expert"));
        mouseClick(find("maintenanceHeader"));
        verify(find("eraseCodeButton") !== null);
        tryVerify(() => find("maintenanceTarget_lte") !== null);
        waitForRendering(window.contentItem); // the choice above the buttons is laid out
        mouseClick(find("maintenanceTarget_lte"));
        compare(flash.maintenanceTarget, "lte");
        tryVerify(() => find("eraseCodeButton") === null);
        mouseClick(find("eraseAllButton"));
        const dialog = findChild(find("flashPage"), "maintenanceConfirm");
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Erase everything on the LTE MCU?");
        compare(dialog.items.length, 3);
        verify(dialog.items[2].startsWith("Product Information Block"));
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        mouseClick(find("recoverButton"));
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Recover the LTE MCU?");
        compare(dialog.items.length, 3);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        compare(flash.opState, FlashViewModel.Idle);
    }

    // Plan 0033 (ADR-0233): no connector warning; the maintenance stays on the application MCU.
    function test_noConnectorWarning() {
        const flash = vm();
        flash.kind = FirmwareSourceViewModel.LteFirmware;
        verify(!find("connectorWarning"));
        compare(find("maintenanceHeader").Accessible.name, "Maintenance — application MCU");
    }

    function test_themes_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_themes(data) {
        AppViewModel.theme = data.theme;
        compare(Theme.dark, data.theme === "dark");
        verify(find("flashBar") !== null);
        verify(find("kindCard0") !== null);
    }

    // Plan 0028 (ADR-0204, D22): the Flash button ends at the right edge of the form column and
    // the bar row starts at its left edge, at 960, 1280 and 2560 px, with and without a vertical
    // scroll bar; the bar background stays full width; the button has the bolt icon.
    function test_flashBarFollowsTheFormColumn_data() {
        return [
            { tag: "960", width: 960, height: 1400 }, { tag: "1280", width: 1280, height: 1400 },
            { tag: "2560", width: 2560, height: 1400 }, { tag: "1280-scrolling", width: 1280, height: 500 },
            { tag: "2560-scrolling", width: 2560, height: 500 }
        ];
    }

    function test_flashBarFollowsTheFormColumn(data) {
        window.width = data.width;
        window.height = data.height;
        waitForRendering(window.contentItem);
        const form = rect(find("flashForm"));
        tryVerify(() => Math.abs(rect(find("flashButton")).x + rect(find("flashButton")).w - (rect(find("flashForm")).x + rect(find("flashForm")).w)) < 1, 3000, `button right edge vs form right edge at ${data.tag}`);
        const row = rect(find("flashBarRow"));
        verify(Math.abs(row.x - form.x) < 1, `row x ${row.x} vs form x ${form.x}`);
        verify(Math.abs(row.w - form.w) < 1, `row width ${row.w} vs form width ${form.w}`);
        const bar = rect(find("flashBar"));
        const content = rect(find("flashContent"));
        verify(Math.abs(bar.x - content.x) < 1 && Math.abs(bar.w - content.w) < 1, "the bar spans the page");
        compare(find("flashButton").iconName, "flash");
        if (data.width >= 1280)
            verify(form.w <= 880.5);
    }

    function test_layoutCzech960() {
        AppViewModel.setLanguage("cs");
        window.width = 960;
        window.height = 600;
        const flash = vm();
        flash.source.loadFile(testSupport.writeHex("app.hex", 4096, true));
        tryVerify(() => flash.source.sourceState === FirmwareSourceViewModel.Ready, 5000);
        verify(testSupport.setSetting("ui/mode", "expert"));
        vm().maintenanceExpanded = true; // below the fold at 600 px
        tryVerify(() => find("maintenanceActions") !== null);
        waitForRendering(window.contentItem);
        checkInside(find("flashButton"), true);
        for (const name of ["browseButton", "downloadButton", "clearSource"])
            checkInside(find(name), false);
        checkNoOverlap(["flashButton", "flashBarText"].map(find));
        const buttons = ["resetButton", "resetHaltButton", "eraseCodeButton", "eraseAllButton", "recoverButton"].map(find);
        checkNoOverlap(buttons);
        for (const button of buttons)
            verify(button.width >= button.implicitWidth - 0.5, `${button.objectName} is narrower than its text`);
        const cards = [0, 1].map(kind => find("kindCard" + kind));
        checkNoOverlap(cards);
        for (const card of cards)
            checkInside(card, false);
    }
}
