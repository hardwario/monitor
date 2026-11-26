// The flash wizard (plan/features/0010 with the 0035 amendments) in the wizard view against the
// fake J-Link library: the flow from a new tab with the keyboard only (probe picker, cable check,
// file, confirm, flash, Open console) in light and dark, Esc while flashing asks, the entry points
// (Flash page "Guide me" for application firmware only, the Choose device link, Help), the
// 960 x 600 window in Czech and German and, with HWIO_SCREENSHOT_DIR, screenshots of every step
// (light/dark, EN/CS).
import QtQuick
import QtQuick.Controls
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null
    readonly property string slowScenario: '{"libraryVersion": 81000, "probes": [{"serialNumber": 801000001, "product": "Test Probe", "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\\n"}]}, "chip": {"readDelayMs": 1000}}]}'

    name: "FlashWizard"
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

    // Plan 0052 / 0056: the wizard view shown now (a tab's or the global one), or null.
    function view() {
        return find("wizardView") || find("globalWizardView");
    }

    // The wizard instance of the current tab (the global one while Settings is current).
    function host() {
        return AppViewModel.currentTabViewModel ? AppViewModel.currentTabViewModel.wizard : AppViewModel.globalWizard;
    }

    function closeWizards() {
        for (let i = 0; i < AppViewModel.tabs.count; ++i) {
            const tab = AppViewModel.tabs.at(i);
            if (tab && tab.wizard.open)
                tab.wizard.cancel(true);
        }
        if (AppViewModel.globalWizard.open)
            AppViewModel.globalWizard.cancel(true);
        tryVerify(() => {
            for (let i = 0; i < AppViewModel.tabs.count; ++i) {
                const tab = AppViewModel.tabs.at(i);
                if (tab && tab.wizard.open)
                    return false;
            }
            return !AppViewModel.globalWizard.open;
        }, 60000);
    }

    function part(name) {
        const v = view();
        const items = v ? findAll(v, name).filter(effectivelyVisible) : [];
        return items.length > 0 ? items[0] : null;
    }

    function initTestCase() {
        // Worker-thread log messages bypass Qt Test's per-row handler (TSan, review 0009).
        testSupport.routeWorkerThreadMessages();
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
        window.requestActivate();
        tryVerify(() => window.active);
    }

    function cleanupTestCase() {
        closeWizards();
        testSupport.resetTabs();
        window.destroy();
        wait(50);
        testSupport.useScenario("");
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "light";
        window.width = 1280;
        window.height = 800;
        testSupport.useScenario("");
        testSupport.resetTabs();
    }

    function cleanup() {
        closeWizards();
        testSupport.useScenario("");
    }

    function probeRows() {
        const list = findChild(view(), "probeList");
        const rows = [];
        if (!list)
            return rows;
        const visit = item => {
            for (let i = 0; i < item.children.length; ++i) {
                const child = item.children[i];
                if (child.objectName === "probeRow")
                    rows.push(child);
                else
                    visit(child);
            }
        };
        visit(list);
        return rows;
    }

    // A tab with the probe: the wizard skips the probe and cable steps (Diagnose passes).
    function openAtFirmware() {
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        verify(AppViewModel.launcher.start("flash-firmware", 0));
        tryVerify(() => view() !== null);
        const wizard = testSupport.openWizard();
        tryCompare(wizard, "stage", FlashWizard.FirmwareStage, 20000);
        tryVerify(() => part("firmwareSourceStep") !== null);
        return wizard;
    }

    function test_keyboardFlowFromANewTab_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_keyboardFlowFromANewTab(data) {
        AppViewModel.theme = data.theme;
        // Help › Flash firmware (guided)… on the Choose device tab.
        verify(AppViewModel.launcher.canOpen("flash-firmware"));
        verify(AppViewModel.launcher.start("flash-firmware", 0));
        tryVerify(() => view() !== null);
        compare(part("wizardTitle").text, "Flash application firmware");
        const wizard = testSupport.openWizard();
        compare(wizard.stage, FlashWizard.ProbeStage);
        tryVerify(() => part("probePickerStep") !== null);
        tryVerify(() => probeRows().length === 2, 5000);
        verify(!part("wizardPrimary").enabled); // two free probes: nothing preselected
        // Keyboard only: Tab to the first row, Space selects it.
        tryVerify(() => part("wizardHeading").activeFocus);
        let focused = null;
        for (let i = 0; i < 12 && focused === null; ++i) {
            keyClick(Qt.Key_Tab);
            focused = probeRows().find(row => row.activeFocus) ?? null;
        }
        verify(focused !== null, "a probe row takes the focus");
        keyClick(Qt.Key_Space);
        tryVerify(() => part("wizardPrimary").enabled);
        part("wizardHeading").forceActiveFocus();
        keyClick(Qt.Key_Return);
        // Cable: the guide and the four checks.
        tryCompare(wizard, "stage", FlashWizard.CableStage);
        verify(part("wizardChecks") !== null);
        verify(part("wizardIllustration") !== null);
        tryVerify(() => part("wizardPrimary").enabled && part("wizardPrimary").text === "Next", 20000);
        tryVerify(() => part("wizardHeading").activeFocus);
        keyClick(Qt.Key_Return);
        // Firmware.
        tryCompare(wizard, "stage", FlashWizard.FirmwareStage);
        tryVerify(() => part("wizardSourceCard") !== null);
        verify(part("wizardLteLink") !== null);
        verify(!part("wizardPrimary").enabled);
        wizard.source.loadFile(testSupport.writeHex("app.hex", 4096, false));
        tryVerify(() => part("wizardPrimary").enabled, 5000);
        tryVerify(() => part("wizardHeading").activeFocus);
        keyClick(Qt.Key_Return);
        // Confirm.
        tryCompare(wizard, "stage", FlashWizard.ConfirmStage);
        verify(part("flashSummary").text.startsWith("app.hex (Intel HEX, 4 kB) → Application MCU"));
        compare(part("wizardPrimary").text, "Flash");
        tryVerify(() => part("wizardPrimary").enabled, 10000);
        tryVerify(() => part("wizardHeading").activeFocus);
        keyClick(Qt.Key_Return);
        // Flash → Done.
        tryVerify(() => wizard.stage === FlashWizard.Flashing || wizard.stage === FlashWizard.Succeeded);
        tryCompare(wizard, "stage", FlashWizard.Succeeded, 60000);
        verify(part("wizardHeading").text.startsWith("Firmware flashed and verified in "));
        compare(part("wizardPrimary").text, "Open console");
        compare(part("wizardCancel").text, "Close");
        verify(part("wizardAction") !== null); // Flash another
        tryVerify(() => part("wizardHeading").activeFocus);
        keyClick(Qt.Key_Return);
        tryVerify(() => !host().open);
        tryCompare(AppViewModel.currentTabViewModel, "currentPage", "console");
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 20000);
    }

    function test_escapeWhileFlashingAsks() {
        testSupport.useScenario(slowScenario);
        const wizard = openAtFirmware();
        wizard.source.loadFile(testSupport.writeHex("big.hex", 65536, false));
        tryVerify(() => part("wizardPrimary").enabled, 5000);
        host().primary();
        tryCompare(wizard, "stage", FlashWizard.ConfirmStage);
        tryVerify(() => part("wizardPrimary").enabled, 10000);
        host().primary();
        tryCompare(wizard, "stage", FlashWizard.Flashing);
        verify(part("flashPhaseStepper") !== null);
        compare(AppViewModel.currentTabViewModel.busyPage, "flash-firmware");
        verify(!part("wizardBack"));
        part("wizardHeading").forceActiveFocus();
        keyClick(Qt.Key_Escape);
        const confirm = findChild(view(), "wizardCancelConfirm");
        tryVerify(() => confirm.opened);
        compare(confirm.body, "Flashing is in progress. Cancel it and close the wizard?");
        confirm.reject();
        tryVerify(() => !confirm.opened);
        verify(host().open);
        // The stepper's Cancel: the "cancelled" step.
        wizard.cancelFlash();
        tryCompare(wizard, "stage", FlashWizard.Cancelled, 60000);
        compare(part("wizardPrimary").text, "Flash again");
    }

    function test_entryPoints() {
        // Choose device page (J-Link view): "Flash firmware with a guide".
        tryVerify(() => find("flashGuideLink") !== null, 5000);
        find("flashGuideLink").clicked(); // below the probe list (may be scrolled out of view)
        tryVerify(() => view() !== null);
        compare(host().wizardId, "flash-firmware");
        host().cancel(false);
        tryVerify(() => !host().open);
        // Flash page "Guide me": application firmware only.
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.showPage("flash");
        tryVerify(() => find("guideMe") !== null, 5000);
        const flash = testSupport.flashPage(0);
        flash.kind = FirmwareSourceViewModel.LteFirmware;
        tryVerify(() => find("guideMe") === null);
        flash.kind = FirmwareSourceViewModel.AppFirmware;
        tryVerify(() => find("guideMe") !== null);
        find("guideMe").clicked();
        tryVerify(() => view() !== null);
        compare(host().wizardId, "flash-firmware");
    }

    function test_minimumWindow_data() {
        return [{ tag: "cs", language: "cs" }, { tag: "de", language: "de" }];
    }

    function test_minimumWindow(data) {
        AppViewModel.setLanguage(data.language);
        window.width = 960;
        window.height = 600;
        const wizard = openAtFirmware();
        const d = view();
        const origin = d.mapToItem(window.contentItem, 0, 0);
        verify(origin.x >= 0 && origin.y >= 0);
        verify(origin.x + d.width <= window.width + 1 && origin.y + d.height <= window.height + 1);
        wizard.source.loadFile(testSupport.writeHex("app.hex", 4096, false));
        tryVerify(() => part("wizardPrimary").enabled, 5000);
        for (const name of ["wizardPrimary", "wizardCancel", "wizardBack"]) {
            const button = part(name);
            verify(button !== null, name);
            const p = button.mapToItem(d, 0, 0);
            verify(p.x >= 0 && p.x + button.width <= d.width + 1, name);
            verify(button.width >= button.implicitWidth - 0.5, `${name} is narrower than its text`);
        }
        host().primary();
        tryCompare(wizard, "stage", FlashWizard.ConfirmStage);
        const summary = part("flashSummary");
        verify(summary.width > 0 && summary.width <= d.width);
    }

    function save(name) {
        waitForRendering(window.contentItem, 2000);
        wait(250);
        const image = grabImage(window.contentItem);
        verify(image.width > 0, name);
        image.save(testSupport.screenshotDir + "/" + name + "-" + AppViewModel.theme + "-" + AppViewModel.language + ".png");
    }

    function test_screenshots_data() {
        return [{ tag: "light-en", theme: "light", language: "en" }, { tag: "dark-en", theme: "dark", language: "en" },
                { tag: "light-cs", theme: "light", language: "cs" }, { tag: "dark-cs", theme: "dark", language: "cs" }];
    }

    function test_screenshots(data) {
        if (testSupport.screenshotDir === "")
            skip("HWIO_SCREENSHOT_DIR is not set");
        AppViewModel.theme = data.theme;
        AppViewModel.setLanguage(data.language);
        window.width = 960;
        window.height = 600;
        verify(AppViewModel.launcher.start("flash-firmware", 0));
        tryVerify(() => view() !== null);
        const wizard = testSupport.openWizard();
        tryVerify(() => probeRows().length === 2, 5000);
        save("w10-01-probe");
        mouseClick(probeRows()[0]);
        host().primary();
        tryVerify(() => part("wizardPrimary").enabled, 20000);
        save("w10-02-cable");
        host().primary();
        tryCompare(wizard, "stage", FlashWizard.FirmwareStage);
        wizard.source.loadFile(testSupport.writeHex("chester-clime-v3.2.1.hex", 4096, true));
        tryVerify(() => part("wizardUicrOption") !== null, 5000);
        save("w10-03-firmware");
        wizard.source.allowUicr = true;
        host().primary();
        tryCompare(wizard, "stage", FlashWizard.ConfirmStage);
        tryVerify(() => part("wizardPrimary").enabled, 10000);
        save("w10-04-confirm");
        host().primary();
        tryCompare(wizard, "stage", FlashWizard.Succeeded, 60000);
        save("w10-05-done");
    }
}
