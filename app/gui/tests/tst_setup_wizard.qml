// The setup wizard as a global view (plan/features/0008 as amended by 0035 and 0056): every step in light and
// dark, the language combo on Welcome (ADR-0254), the read-only probe step through embeddedOptions
// (ADR-0290), CommandBlock [Copy], a keyboard-only walk-through, every step at 960 x 600 in Czech
// and German, and screenshots of every step (light/dark, EN/CS/DE) when HWIO_SCREENSHOT_DIR is
// set (QT_QUICK_BACKEND=software).
import QtQuick
import QtQuick.Controls
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function initTestCase() {
        // Worker-thread log messages bypass Qt Test's per-row handler (TSan, review 0009).
        testSupport.routeWorkerThreadMessages();
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
        // The nRF Util step shows its commands: an nrfutil that does not exist.
        verify(testSupport.setSetting("nrfutil/path", "/nonexistent/hwio-test/nrfutil"));
    }

    function cleanupTestCase() {
        if (AppViewModel.globalWizard.open)
            AppViewModel.globalWizard.cancel(true);
        if (window)
            window.destroy();
        wait(50);
        testSupport.resetTabs();
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        window.width = 1280;
        window.height = 800;
        testSupport.resetTabs();
    }

    function cleanup() {
        if (AppViewModel.globalWizard.open)
            AppViewModel.globalWizard.cancel(true);
        tryVerify(() => !AppViewModel.globalWizard.open);
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "system";
    }

    // Plan 0056: the setup wizard is a global view over the page area (ContentViewHost), or null.
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

    function view() {
        const items = findAll(window.contentItem, "globalWizardView").filter(effectivelyVisible);
        return items.length > 0 ? items[0] : null;
    }

    function part(name) {
        return findChild(view(), name);
    }

    function setup() {
        return testSupport.openWizard();
    }

    function control() {
        return findChild(view(), "setupStep");
    }

    function openSetup() {
        verify(AppViewModel.launcher.start("first-run-setup", -1));
        tryVerify(() => view() !== null);
    }

    // The step's checks have finished (the default button is enabled again).
    function settle() {
        tryVerify(() => !setup().running, 15000);
        waitForRendering(window.contentItem, 2000);
    }

    // Leaves the current step forward (Next, or the step's "Continue anyway" / "Next" action).
    function forward() {
        settle();
        const id = setup().stepId;
        const before = id;
        if (id === "jlink" && part("wizardPrimary").text !== "Next")
            AppViewModel.globalWizard.action("continue-anyway");
        else if (id === "usb" && AppViewModel.globalWizard.step.actions.length > 0 && AppViewModel.globalWizard.step.actions[0].id === "next")
            AppViewModel.globalWizard.action("next");
        else
            AppViewModel.globalWizard.primary();
        tryVerify(() => setup() === null || setup().stepId !== before, 5000, "left " + before + " (" + part("wizardPrimary").text + ", " + JSON.stringify(AppViewModel.globalWizard.step.actions) + ")");
    }

    function walkTo(id) {
        for (let i = 0; i < 8 && setup().stepId !== id; ++i)
            forward();
        compare(setup().stepId, id);
        settle();
    }

    // Every footer button and the step's control lie inside the view.
    function verifyInside(name) {
        const d = view();
        for (const button of [part("wizardPrimary"), part("wizardCancel"), part("wizardBack")]) {
            if (!button || !button.visible)
                continue;
            const p = button.mapToItem(d, 0, 0);
            verify(p.x >= 0 && p.x + button.width <= d.width + 1, name + ": " + button.objectName + " " + button.text);
        }
        const footer = part("wizardFooter");
        verify(footer.implicitWidth <= d.width + 1, name + ": footer " + footer.implicitWidth + " > " + d.width);
    }

    function save(name) {
        if (testSupport.screenshotDir === "")
            return;
        waitForRendering(window.contentItem, 2000);
        wait(250);
        const image = grabImage(window.contentItem);
        verify(image.width > 0, name);
        image.save(testSupport.screenshotDir + "/setup-" + name + "-" + AppViewModel.theme + "-" + AppViewModel.language + ".png");
    }

    function test_everyStep_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_everyStep(data) {
        AppViewModel.theme = data.theme;
        openSetup();
        compare(part("wizardTitle").text, "Setup");
        // Welcome: illustration, heading, the language combo, [Skip setup].
        compare(part("wizardHeading").text, "Welcome to HARDWARIO Monitor");
        verify(part("wizardIllustration").visible);
        tryVerify(() => control() !== null);
        verify(findChild(control(), "setupLanguage").visible);
        verify(!findChild(control(), "setupDontShow").visible);
        compare(part("wizardAction").text, "Skip setup");
        verify(!part("wizardBack").visible);

        forward(); // J-Link Software: the check row and the control
        compare(setup().stepId, "jlink");
        settle();
        verify(part("wizardChecks").visible);
        compare(part("wizardChecks").model.count, 1);
        tryVerify(() => control() !== null);
        tryVerify(() => findChild(control(), "setupStatus").visible, 5000, "status: " + setup().statusText + " / " + part("wizardPrimary").text); // the fake library

        walkTo("nrfutil");
        compare(part("wizardHeading").text, "nRF Util (optional)");
        verify(findChild(control(), "setupCommands").visible);
        compare(findChild(control(), "setupCommands").commands[0], "nrfutil install device");
        compare(part("wizardPrimary").text, "Skip");
        verify(findChild(control(), "setupButton") !== null); // It is installed elsewhere…

        forward(); // Test with a probe: the read-only picker (ADR-0290)
        compare(setup().stepId, "probe");
        tryVerify(() => findChild(view(), "probePickerStep") !== null);
        const step = findChild(view(), "probePickerStep");
        verify(step.readOnly);
        tryVerify(() => step.vm !== null && step.vm.count === 2, 5000);
        verify(part("wizardPrimary").enabled);

        forward(); // Done: the summary, [Close]
        compare(setup().stepId, "done");
        tryVerify(() => control() !== null && findChild(control(), "setupSummary").visible);
        verify(findChild(control(), "setupSummary").model.count >= 3); // + USB access on Linux
        verify(testSupport.setting("setup/completedVersion") !== "");
        compare(part("wizardCancel").text, "Close");
        // The probe picker's control is gone (another control on this step).
        tryVerify(() => findChild(view(), "probePickerStep") === null);
        mouseClick(part("wizardCancel"));
        tryVerify(() => view() === null);
    }

    function test_languageComboRetranslatesLive() {
        openSetup();
        tryVerify(() => control() !== null);
        const combo = findChild(control(), "setupLanguage");
        compare(combo.currentIndex, 1); // the tests run in English
        setup().selectLanguage(2); // Čeština
        compare(AppViewModel.language, "cs");
        compare(part("wizardHeading").text, "Vítejte v HARDWARIO Monitor");
        compare(combo.currentIndex, 2);
        setup().selectLanguage(1);
        compare(part("wizardHeading").text, "Welcome to HARDWARIO Monitor");
    }

    function test_commandCopy() {
        openSetup();
        walkTo("nrfutil");
        const copy = findChild(findChild(control(), "setupCommands"), "commandCopy");
        verify(copy !== null);
        mouseClick(copy);
        tryVerify(() => AppViewModel.toasts.length > 0);
    }

    function test_keyboardOnly() {
        openSetup();
        tryVerify(() => part("wizardHeading").activeFocus);
        keyClick(Qt.Key_Return); // Next
        tryCompare(setup(), "stepId", "jlink");
        settle();
        tryVerify(() => part("wizardHeading").activeFocus);
        keyClick(Qt.Key_Return); // Next (the fake J-Link library is found)
        tryVerify(() => setup().stepId !== "jlink");
        for (let i = 0; i < 4 && setup().stepId !== "done"; ++i) {
            settle();
            const before = setup().stepId;
            part("wizardHeading").forceActiveFocus();
            if (before === "usb" && AppViewModel.globalWizard.step.primaryText === "Check again")
                AppViewModel.globalWizard.action("next");
            else
                keyClick(Qt.Key_Return);
            tryVerify(() => setup().stepId !== before);
        }
        compare(setup().stepId, "done");
        part("wizardHeading").forceActiveFocus();
        keyClick(Qt.Key_Escape);
        tryVerify(() => view() === null);
    }

    function test_minimumWindow_data() {
        return [{ tag: "cs", language: "cs" }, { tag: "de", language: "de" }];
    }

    function test_minimumWindow(data) {
        AppViewModel.setLanguage(data.language);
        window.width = 960;
        window.height = 600;
        openSetup();
        const d = view();
        const origin = d.mapToItem(window.contentItem, 0, 0);
        verify(origin.x >= 0 && origin.x + d.width <= window.width + 1 && origin.y + d.height <= window.height + 1);
        for (let i = 0; i < 8; ++i) {
            settle();
            verifyInside(data.language + "/" + setup().stepId);
            if (setup().stepId === "done")
                break;
            forward();
        }
        compare(setup().stepId, "done");
    }

    function test_screenshots_data() {
        return [{ tag: "light-en", theme: "light", language: "en" }, { tag: "dark-en", theme: "dark", language: "en" },
                { tag: "light-cs", theme: "light", language: "cs" }, { tag: "dark-cs", theme: "dark", language: "cs" },
                { tag: "light-de", theme: "light", language: "de" }];
    }

    function test_screenshots(data) {
        if (testSupport.screenshotDir === "")
            skip("HWIO_SCREENSHOT_DIR is not set");
        AppViewModel.theme = data.theme;
        AppViewModel.setLanguage(data.language);
        window.width = 960;
        window.height = 600;
        openSetup();
        for (let i = 1; i <= 8; ++i) {
            settle();
            save(i + "-" + setup().stepId);
            if (setup().stepId === "done")
                break;
            forward();
        }
    }
}
