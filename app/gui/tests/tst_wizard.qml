// The wizard view with the test wizard (plan/features/0007, C2-C5; plan 0052, ADR-0386): every step
// kind in light and dark, keyboard (Enter, Esc, focus on the heading), busy cancel confirmation, the
// rail entry and the tab badge, two tabs with wizards, the global wizard, the 960 x 600 window in
// Czech (geometry, compact step list), the illustration slots and the report. Plan 0035 (A3,
// ADR-0252): the Embedded `probe-picker` step with the test wizard (light/dark, in-use rows,
// keyboard-only selection).
import QtQuick
import QtQuick.Controls
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        testSupport.resetTabs();
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
        for (let i = 0; i < AppViewModel.tabs.count; ++i) {
            const tab = AppViewModel.tabs.at(i);
            if (tab && tab.wizard.open) {
                const wizard = testSupport.tabWizard(i);
                if (wizard && wizard.busy)
                    wizard.finishOperation();
                tab.wizard.cancel(true);
            }
        }
        if (AppViewModel.globalWizard.open)
            AppViewModel.globalWizard.cancel(true);
        tryVerify(() => !AppViewModel.currentTabViewModel || !AppViewModel.currentTabViewModel.wizard.open);
        if (AppViewModel.report.open)
            AppViewModel.report.close();
    }

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
        return { x: point.x, y: point.y, w: item.width, h: item.height };
    }

    // Plan 0052: the wizard view shown now (of the current tab), or null.
    function view() {
        return find("wizardView");
    }

    function part(name) {
        const items = findAll(view(), name).filter(effectivelyVisible);
        return items.length > 0 ? items[0] : null;
    }

    function wizard() {
        return AppViewModel.currentTabViewModel.wizard;
    }

    function openWizard() {
        verify(AppViewModel.launcher.start("connect-device", 0));
        tryVerify(() => view() !== null);
    }

    function test_everyStepKind_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_everyStepKind(data) {
        AppViewModel.theme = data.theme;
        openWizard();
        // Plan 0052: the view replaces the page area; the rail and the tab show the wizard.
        verify(find("railWizard") !== null);
        verify(find("tabWizardBadge") !== null);
        compare(AppViewModel.currentTabViewModel.currentPage, "wizard");
        // Info: illustration, heading (focused), body, link; no Back on the first step.
        compare(part("wizardTitle").text, "Test wizard");
        compare(part("wizardHeading").text, "Connect the J-Link");
        tryVerify(() => part("wizardHeading").activeFocus);
        verify(part("wizardIllustration") !== null);
        verify(part("wizardLink") !== null);
        verify(part("wizardBack") === null);
        compare(part("wizardPrimary").text, "Next");
        verify(part("wizardStepList") !== null);
        compare(view().title, "Test wizard");

        // Enter = the default button.
        keyClick(Qt.Key_Return);
        tryCompare(part("wizardHeading"), "text", "Which MCU do you want to connect?");
        verify(part("wizardBack") !== null);
        verify(part("wizardChoices") !== null);
        verify(part("wizardIllustration") === null);

        wizard().primary();
        // Checks: one failed row with cause and suggestion.
        verify(part("wizardChecks") !== null);
        compare(part("wizardPrimary").text, "Check again");
        tryVerify(() => findChild(part("wizardChecks"), "checkCause") !== null);
        mouseClick(part("wizardPrimary"));
        compare(part("wizardPrimary").text, "Next");

        wizard().primary();
        verify(part("wizardCommands") !== null);
        verify(findChild(part("wizardCommands"), "commandCopy") !== null);

        wizard().primary();
        tryVerify(() => part("testEmbedded") !== null);
        compare(part("testEmbedded").text, "Embedded control");
        compare(part("wizardPrimary").text, "Flash");

        wizard().primary(); // the operation starts
        const test = testSupport.openWizard();
        verify(test.busy);
        verify(part("wizardBack") === null);
        compare(AppViewModel.currentTabViewModel.busyPage, "connect-device");
        tryVerify(() => findChild(find("railWizard"), "railBusy").visible);
        test.finishOperation();
        compare(part("wizardHeading").text, "Connected");
        compare(part("wizardCancel").text, "Close");
        verify(findChild(part("wizardFooter"), "wizardAction") !== null);
        compare(part("wizardPrimary").text, "Finish");
        mouseClick(part("wizardPrimary"));
        tryVerify(() => view() === null);
        verify(find("railWizard") === null);
        verify(find("tabWizardBadge") === null);
        verify(AppViewModel.currentTabViewModel.currentPage !== "wizard");
    }

    function test_escapeWhileBusyAsks() {
        openWizard();
        const test = testSupport.openWizard();
        test.cancelFinishesImmediately = false;
        test.goTo(4);
        wizard().primary();
        verify(test.busy);
        part("wizardHeading").forceActiveFocus();
        keyClick(Qt.Key_Escape);
        const confirm = findChild(view(), "wizardCancelConfirm");
        tryVerify(() => confirm.opened);
        compare(findChild(confirm, "dialogAccept").text, "Cancel and close");
        compare(findChild(confirm, "dialogReject").text, "Keep going");
        // Plan 0052: centred over the view that asked.
        const v = rect(view());
        const c = rect(confirm.contentItem.parent);
        verify(c.x >= v.x && c.x + c.w <= v.x + v.w, "the confirmation lies over the view");
        mouseClick(findChild(confirm, "dialogReject"));
        tryVerify(() => !confirm.visible);
        verify(view() !== null);
        AppViewModel.currentTabViewModel.wizard.cancel(true);
        verify(wizard().closing);
        verify(view() !== null); // until the operation has ended
        test.finishOperation();
        tryVerify(() => view() === null);
    }

    function test_escapeWhenIdleCloses() {
        openWizard();
        part("wizardHeading").forceActiveFocus();
        keyClick(Qt.Key_Escape);
        tryVerify(() => view() === null);
    }

    // Plan 0052 (ADR-0386): the rail stays usable; the wizard keeps its step and comes back from
    // the rail's "Wizard" entry with the focus on its heading.
    function test_railSwitchKeepsTheStep() {
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.showPage("console");
        openWizard();
        wizard().primary();
        compare(part("wizardHeading").text, "Which MCU do you want to connect?");
        const consoleItem = findAll(find("navRail"), "railItem").filter(item => item.text === "Console")[0];
        mouseClick(consoleItem);
        tryVerify(() => view() === null);
        compare(AppViewModel.currentTabViewModel.currentPage, "console");
        verify(wizard().open);
        verify(find("railWizard") !== null);
        verify(!find("railWizard").selected);
        verify(find("tabWizardBadge") !== null);
        waitForRendering(window.contentItem); // the rail has laid the entry out
        mouseClick(find("railWizard"));
        tryCompare(AppViewModel.currentTabViewModel, "currentPage", "wizard");
        tryVerify(() => view() !== null);
        verify(find("railWizard").selected);
        compare(part("wizardHeading").text, "Which MCU do you want to connect?");
        tryVerify(() => part("wizardHeading").activeFocus);
        wizard().cancel(false);
        tryVerify(() => view() === null);
        compare(AppViewModel.currentTabViewModel.currentPage, "console");
    }

    // Plan 0052 (ADR-0386): one wizard per tab; another tab runs its own.
    function test_twoTabsRunWizards() {
        AppViewModel.newTab();
        compare(AppViewModel.tabs.count, 2);
        verify(AppViewModel.launcher.start("connect-device", 0));
        verify(AppViewModel.launcher.start("connect-device", 1));
        compare(AppViewModel.currentTab, 1);
        tryVerify(() => view() !== null);
        testSupport.tabWizard(1).goTo(3);
        tryVerify(() => part("wizardHeading").text !== "Connect the J-Link");
        const second = part("wizardHeading").text;
        AppViewModel.currentTab = 0;
        tryVerify(() => view() !== null && part("wizardHeading").text === "Connect the J-Link");
        AppViewModel.currentTab = 1;
        tryVerify(() => view() !== null && part("wizardHeading").text === second);
        // A second wizard in the same tab is refused and the open one is shown.
        AppViewModel.currentTab = 0;
        verify(!AppViewModel.launcher.start("connect-device", 1));
        compare(AppViewModel.currentTab, 1);
    }

    // Plan 0052: a global wizard (setup) is a view over the page area of the current tab.
    function test_globalWizardView() {
        testSupport.registerGlobalTestWizard();
        verify(AppViewModel.launcher.start("first-run-setup"));
        const global = () => find("globalWizardView");
        tryVerify(() => global() !== null);
        compare(findAll(global(), "wizardTitle")[0].text, "Test wizard");
        verify(!AppViewModel.currentTabViewModel.wizard.open);
        verify(find("railWizard") === null);
        tryVerify(() => findAll(global(), "wizardHeading")[0].activeFocus);
        keyClick(Qt.Key_Escape);
        tryVerify(() => global() === null);
        verify(!AppViewModel.globalWizard.open);
    }

    function test_minimumWindowInCzech() {
        AppViewModel.setLanguage("cs");
        window.width = 960;
        window.height = 600;
        openWizard();
        waitForRendering(window.contentItem);
        const v = rect(view());
        // The page area: right of the rail, below the tab bar and the device bar.
        const rail = rect(find("navRail"));
        verify(v.x >= rail.x + rail.w - 0.5, "right of the rail");
        verify(v.y > 0 && v.x + v.w <= window.width + 0.5 && v.y + v.h <= window.height + 0.5);
        verify(v.h > 400, "the view uses the page area: " + v.h);
        const content = rect(part("wizardContent"));
        verify(content.h > 250, "the content takes the free height: " + content.h);
        // Every footer button lies inside the view.
        for (const name of ["wizardPrimary", "wizardCancel"]) {
            const r = rect(part(name));
            verify(r.x >= v.x - 0.5 && r.x + r.w <= v.x + v.w + 0.5, name);
            verify(r.y >= v.y - 0.5 && r.y + r.h <= v.y + v.h + 0.5, name);
        }
        testSupport.openWizard().goTo(2);
        verify(part("wizardStepList") !== null);
        for (const name of ["wizardBack", "wizardPrimary", "wizardCancel"]) {
            const r = rect(part(name));
            verify(r.x >= v.x - 0.5 && r.x + r.w <= v.x + v.w + 0.5, name);
        }
        AppViewModel.setLanguage("en");
    }

    function test_compactStepList() {
        openWizard();
        const steps = part("wizardStepList");
        verify(!findChild(steps, "wizardStepCompact").visible || steps.showCompact);
        steps.compact = true;
        verify(findChild(steps, "wizardStepCompact").visible);
        compare(findChild(steps, "wizardStepCompact").text, "Step 1 of 6 · Probe");
        steps.compact = false;
    }

    Component {
        id: illustrationComponent

        Illustration {}
    }

    function test_illustrationSlotsResolve() {
        for (const slot of ["jlink-usb", "chester-app-cable", "chester-lte-cable", "chester-power", "placeholder"]) {
            const item = createTemporaryObject(illustrationComponent, testCase, { slot: slot });
            // Plan 0051 (ADR-0380): the cable slots have a dark variant.
            verify(String(item.source).endsWith("/" + slot + ".svg") || String(item.source).endsWith("/" + slot + "-dark.svg"),
                   slot + ": " + item.source);
            const image = findChild(item, "illustrationImage");
            tryCompare(image, "status", Image.Ready);
        }
        const unknown = createTemporaryObject(illustrationComponent, testCase, { slot: "nope" });
        verify(String(unknown.source).endsWith("/placeholder.svg"));
    }

    // Plan 0052 (ADR-0387): the report is a view of the page area; Close returns to the page.
    function test_reportDialog() {
        AppViewModel.report.show();
        tryVerify(() => find("reportDialog") !== null);
        compare(AppViewModel.overlayView, "report");
        const report = find("reportDialog");
        tryVerify(() => findChild(report, "reportTitle").activeFocus);
        tryCompare(AppViewModel.report, "state", "ready", 30000);
        verify(findChild(report, "reportSummaryText").text.indexOf("HARDWARIO Monitor diagnostic report") === 0);
        verify(findChild(report, "reportNote").visible);
        verify(findChild(report, "reportSave").visible);
        verify(findChild(report, "reportCopy").visible);
        mouseClick(findChild(report, "reportClose"));
        tryVerify(() => find("reportDialog") === null);
        verify(!AppViewModel.report.open);
        compare(AppViewModel.overlayView, "");
        // Help → Create diagnostic report… opens it through the view; Esc closes it.
        verify(AppViewModel.openView("report"));
        verify(AppViewModel.report.open);
        tryVerify(() => find("reportDialog") !== null);
        tryVerify(() => findChild(find("reportDialog"), "reportTitle").activeFocus);
        keyClick(Qt.Key_Escape);
        tryVerify(() => find("reportDialog") === null);
        verify(!AppViewModel.report.open);
    }

    // Plan 0052: every content view at 960 x 600 in Czech: inside the page area, footer buttons
    // inside the view.
    function test_contentViewsAt960InCzech_data() {
        return [{ tag: "report", footer: "reportClose" }, { tag: "join", footer: "remoteJoinButton" },
                { tag: "shortcuts", footer: "shortcutsClose" }, { tag: "share", footer: "remoteShareStart" }];
    }

    function test_contentViewsAt960InCzech(data) {
        AppViewModel.setLanguage("cs");
        window.width = 960;
        window.height = 600;
        verify(AppViewModel.openView(data.tag));
        const names = { report: "reportDialog", join: "remoteJoinInputDialog", shortcuts: "shortcutOverlay",
                        share: "remoteShareDialog" };
        tryVerify(() => find(names[data.tag]) !== null);
        waitForRendering(window.contentItem);
        const v = rect(find(names[data.tag]));
        const rail = rect(find("navRail"));
        verify(v.x >= rail.x + rail.w - 0.5 && v.x + v.w <= window.width + 0.5 && v.y + v.h <= window.height + 0.5);
        const button = rect(findAll(find(names[data.tag]), data.footer)[0]);
        verify(button.x >= v.x - 0.5 && button.x + button.w <= v.x + v.w + 0.5, data.footer);
        verify(button.y >= v.y - 0.5 && button.y + button.h <= v.y + v.h + 0.5, data.footer);
        AppViewModel.closeView();
        tryVerify(() => find(names[data.tag]) === null);
        AppViewModel.setLanguage("en");
    }

    // ---- plan 0035: the probe-picker step -----------------------------------------------------

    function openProbePickerStep() {
        openWizard();
        const wizard = testSupport.openWizard();
        wizard.useProbePicker();
        wizard.goTo(4);
        tryVerify(() => findChild(view(), "probePickerStep") !== null);
        return wizard;
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

    function test_probePickerStep_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_probePickerStep(data) {
        AppViewModel.theme = data.theme;
        // Tab 2 uses 801000002: "In use (tab 2)", disabled, no tab switch.
        AppViewModel.newTab();
        verify(testSupport.configureTab(1, "app", 801000002, "Test Probe"));
        AppViewModel.currentTab = 0;
        const wizard = openProbePickerStep();
        const step = findChild(view(), "probePickerStep");
        verify(step.vm !== null);
        tryVerify(() => probeRows().length === 2, 5000);
        // No Connect, no J-Link Software error panel in the step.
        compare(findChild(step, "connectButton"), null);
        compare(findChild(step, "libraryError"), null);
        verify(!findChild(step, "probeListEmpty").visible);
        // The only free probe is preselected; [Flash] (the step's default button) is enabled.
        tryCompare(wizard, "selectedSerial", "801000001");
        verify(part("wizardPrimary").enabled);
        const rows = probeRows();
        const used = rows.find(row => row.serial === "801000002");
        verify(used !== undefined);
        verify(!used.enabled);
        compare(findChild(used, "probeInUse").text, "In use (tab 2)");
        mouseClick(used);
        compare(AppViewModel.currentTab, 0);
        compare(wizard.selectedSerial, "801000001");
        // The check rows of the wizard below the list.
        verify(findChild(step, "probePickerChecks").visible);
        AppViewModel.currentTabViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.currentTabViewModel.wizard.open);
        AppViewModel.theme = "system";
    }

    function test_probePickerKeyboardSelection() {
        const wizard = openProbePickerStep();
        tryVerify(() => probeRows().length === 2, 5000);
        compare(wizard.selectedSerial, ""); // two free probes: nothing preselected
        verify(!part("wizardPrimary").enabled);
        tryVerify(() => part("wizardHeading").activeFocus);
        let focused = null;
        for (let i = 0; i < 12 && focused === null; ++i) {
            keyClick(Qt.Key_Tab);
            focused = probeRows().find(row => row.activeFocus) || null;
        }
        verify(focused !== null, "a probe row is reachable with Tab");
        keyClick(Qt.Key_Space);
        compare(wizard.selectedSerial, focused.serial);
        verify(part("wizardPrimary").enabled);
    }

    function test_probePickerReleasesTheScanner() {
        const wizard = openProbePickerStep();
        const picker = findChild(view(), "probePickerStep").vm;
        verify(picker.active);
        wizard.goTo(3); // another step: the scanner is released
        verify(!picker.active);
        wizard.goTo(4);
        verify(picker.active);
    }

    name: "Wizard"
    when: windowShown
}
