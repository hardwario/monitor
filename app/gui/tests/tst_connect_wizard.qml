// The Connect and Troubleshooting wizards (plan/features/0009 with the 0035 amendments) in the
// wizard dialog against the fake J-Link library: the Connect flow from a new tab with the keyboard
// only (probe picker, cable, power, check, connect, Open console) in light and dark, the expanded
// row of a failed check with [Show me how], the entry points (Choose device "Guide me" and the
// ghost link, Help menu), the troubleshooting topic list and a topic, the 960 x 600 window in Czech
// and German and, with HWIO_SCREENSHOT_DIR, screenshots of the steps (light/dark, EN/CS).
// tst_gui registers the real Connect wizard for this file (not the test wizard).
import QtQuick
import QtQuick.Controls
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null
    readonly property string oneProbe: '{"libraryVersion": 81000, "probes": [{"serialNumber": 801000001, "product": "Test Probe", "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\\n"}]}, "chip": {}}]}'
    readonly property string lteConnector: '{"libraryVersion": 81000, "probes": [{"serialNumber": 801000001, "product": "Test Probe", "chip": {"family": "nrf91"}}]}'

    name: "ConnectWizard"
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

    function dialog() {
        return findChild(window, "wizardDialog");
    }

    function parts(name) {
        const d = dialog();
        return findAll(d.contentItem, name).concat(findAll(d.footer, name)).concat(findAll(d.header, name))
               .filter(effectivelyVisible);
    }

    function part(name) {
        const items = parts(name);
        return items.length > 0 ? items[0] : null;
    }

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
        window.requestActivate();
        tryVerify(() => window.active);
    }

    function cleanupTestCase() {
        if (AppViewModel.wizard.open)
            AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open, 60000);
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
        verify(testSupport.useScenario(oneProbe));
        testSupport.resetTabs();
    }

    function cleanup() {
        if (AppViewModel.wizard.open)
            AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open, 60000);
        testSupport.resetTabs();
        testSupport.useScenario("");
    }

    function returnKey() {
        tryVerify(() => part("wizardHeading").activeFocus);
        keyClick(Qt.Key_Return);
    }

    function test_keyboardFlowFromANewTab_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_keyboardFlowFromANewTab(data) {
        AppViewModel.theme = data.theme;
        verify(AppViewModel.launcher.start("connect-device", 0));
        tryVerify(() => dialog().opened);
        compare(part("wizardTitle").text, "Connect a device");
        const wizard = testSupport.openWizard();
        compare(wizard.step, ConnectWizard.Probe);
        tryVerify(() => part("probePickerStep") !== null);
        // The one free probe is preselected: Return goes on.
        tryVerify(() => part("wizardPrimary").enabled, 5000);
        returnKey();
        tryVerify(() => wizard.step === ConnectWizard.Cable);
        verify(part("wizardIllustration") !== null);
        returnKey();
        tryVerify(() => wizard.step === ConnectWizard.Power);
        returnKey();
        tryVerify(() => wizard.step === ConnectWizard.Check);
        verify(part("wizardChecks") !== null);
        tryVerify(() => part("wizardPrimary").enabled && part("wizardPrimary").text === "Connect", 20000);
        returnKey();
        tryVerify(() => wizard.step === ConnectWizard.Done);
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 20000);
        tryVerify(() => part("wizardPrimary").text === "Open console");
        verify(part("wizardHeading").text.startsWith("Connected to CHESTER app"));
        compare(part("wizardCancel").text, "Close");
        returnKey();
        tryVerify(() => !AppViewModel.wizard.open);
        tryCompare(AppViewModel.currentTabViewModel, "currentPage", "console");
    }

    function test_failedCheckExpandsAndShowsTheWay() {
        verify(testSupport.useScenario(lteConnector));
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        verify(AppViewModel.launcher.open("connect-device", testSupport.defaultError("target.not_connected"), 0));
        tryVerify(() => dialog().opened);
        const wizard = testSupport.openWizard();
        tryVerify(() => part("wizardPrimary").enabled && part("wizardPrimary").text === "Check again", 20000);
        // The failed row is expanded: its cause and what to do.
        tryVerify(() => part("checkCause") !== null);
        verify(part("wizardBody").text.indexOf("APP connector") >= 0);
        const actions = parts("wizardAction");
        verify(actions.length >= 1);
        compare(actions[0].text, "Show me how");
        mouseClick(actions[0]);
        tryVerify(() => wizard.step === ConnectWizard.Cable);
    }

    function test_entryPoints() {
        // Choose device page: the ghost link below the probe list.
        tryVerify(() => find("connectGuideLink") !== null, 5000);
        find("connectGuideLink").clicked();
        tryVerify(() => dialog().opened);
        compare(AppViewModel.wizard.wizardId, "connect-device");
        AppViewModel.wizard.cancel(false);
        tryVerify(() => !AppViewModel.wizard.open);
        // The empty state (no probe; shown only after a scan finds nothing): [Guide me] replaces
        // the former disabled "Help me fix this".
        const guide = findAll(window.contentItem, "noProbesHelp")[0];
        verify(guide !== undefined);
        compare(guide.text, "Guide me");
        verify(guide.enabled);
        guide.clicked();
        tryVerify(() => dialog().opened);
        compare(AppViewModel.wizard.wizardId, "connect-device");
        AppViewModel.wizard.cancel(false);
        tryVerify(() => !AppViewModel.wizard.open);
        // Help menu: both items are offered for registered wizards (tst_shell checks the menu).
        verify(AppViewModel.launcher.canOpen("connect-device"));
        verify(AppViewModel.launcher.canOpen("troubleshooting"));
        verify(AppViewModel.launcher.start("troubleshooting", -1));
        tryVerify(() => dialog().opened);
        compare(AppViewModel.wizard.wizardId, "troubleshooting");
    }

    function test_troubleshootingTopic() {
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        verify(AppViewModel.launcher.start("troubleshooting", 0));
        tryVerify(() => dialog().opened);
        compare(part("wizardHeading").text, "What is the problem?");
        const choices = parts("wizardChoice");
        compare(choices.length, 9);
        verify(!part("wizardPrimary").enabled);
        mouseClick(choices[2]); // The device has no power or does not respond
        tryVerify(() => part("wizardPrimary").enabled);
        AppViewModel.wizard.primary();
        compare(part("wizardHeading").text, "Power the device");
        AppViewModel.wizard.primary();
        verify(part("wizardChecks") !== null);
        tryVerify(() => part("wizardPrimary").enabled, 20000);
        // From an error: the introduction shows the cause as plain text.
        AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open);
        verify(AppViewModel.launcher.open("troubleshooting", testSupport.defaultError("target.rtt_not_found"), 0));
        tryVerify(() => dialog().opened);
        tryVerify(() => part("wizardErrorStep") !== null);
        verify(part("wizardErrorMessage") === null); // the heading is the message
        verify(part("wizardErrorCause").text.startsWith("Probably: "));
        compare(part("wizardBody").text, "Let's find the cause.");
    }

    function test_minimumWindow_data() {
        return [{ tag: "cs", language: "cs" }, { tag: "de", language: "de" }];
    }

    function test_minimumWindow(data) {
        AppViewModel.setLanguage(data.language);
        window.width = 960;
        window.height = 600;
        verify(testSupport.useScenario(lteConnector));
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        verify(AppViewModel.launcher.open("connect-device", testSupport.defaultError("target.not_connected"), 0));
        tryVerify(() => dialog().opened);
        const d = dialog();
        verify(d.x >= 0 && d.y >= 0);
        verify(d.x + d.width <= window.width && d.y + d.height <= window.height);
        // The widest footer: Show me how · Update LTE firmware… · Check again · Cancel.
        tryVerify(() => parts("wizardAction").length === 2, 20000);
        const names = ["wizardPrimary", "wizardCancel"];
        const buttons = names.map(name => part(name)).concat(parts("wizardAction"));
        for (const button of buttons) {
            verify(button !== null);
            const p = button.mapToItem(d.contentItem.parent, 0, 0);
            verify(p.x >= 0 && p.x + button.width <= d.width + 1, button.text);
            verify(button.width >= button.implicitWidth - 0.5, `${button.text} is narrower than its text`);
        }
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
        verify(AppViewModel.launcher.start("connect-device", 0));
        tryVerify(() => dialog().opened);
        tryVerify(() => part("wizardPrimary").enabled, 5000);
        save("w09-01-probe");
        AppViewModel.wizard.primary();
        save("w09-02-cable");
        AppViewModel.wizard.primary();
        save("w09-03-power");
        AppViewModel.wizard.primary();
        tryVerify(() => part("wizardPrimary").enabled, 20000);
        save("w09-04-check");
        AppViewModel.wizard.primary();
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 20000);
        tryVerify(() => part("wizardPrimary").text !== "");
        save("w09-05-done");
        AppViewModel.wizard.cancel(false);
        tryVerify(() => !AppViewModel.wizard.open);
        verify(AppViewModel.launcher.start("troubleshooting", 0));
        tryVerify(() => dialog().opened);
        save("w09-06-topics");
        AppViewModel.wizard.cancel(false);
        tryVerify(() => !AppViewModel.wizard.open);
        verify(AppViewModel.launcher.open("troubleshooting", testSupport.defaultError("target.no_power"), 0));
        tryVerify(() => dialog().opened);
        save("w09-07-intro");
        AppViewModel.wizard.primary();
        save("w09-08-topic-step");
        verify(testSupport.useScenario(lteConnector));
        testSupport.resetTabs();
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open);
        verify(AppViewModel.launcher.open("connect-device", testSupport.defaultError("target.not_connected"), 0));
        tryVerify(() => dialog().opened);
        tryVerify(() => part("wizardPrimary").enabled, 20000);
        save("w09-09-wrong-connector");
    }
}
