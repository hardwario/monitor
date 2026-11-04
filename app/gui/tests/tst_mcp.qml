// MCP in the GUI (plan 0014 B1-B3): Settings → AI assistants in its states (off, listening, error
// with Retry) in light and dark, the permission radios writing mcp/consent/<id>, the consent
// window per category (detail rows, countdown, Esc = Deny, Allow once, Allow for the client, the
// window flags and no transient parent, the queue), the rail indicator states, the "AI" tab badge,
// the device bar's Cancel of an AI operation, the console input hint, and the layout at 960 x 600 in
// Czech. With HWIO_SCREENSHOT_DIR the screenshots of plan 0014 are saved (960 x 600, light/dark,
// EN/CS).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null
    property int consentRequests: 0

    name: "Mcp"
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

    function findIn(root, name) {
        const items = findAll(root, name).filter(effectivelyVisible);
        return items.length > 0 ? items[0] : null;
    }

    function find(name) {
        return findIn(window.contentItem, name);
    }

    function prompt() {
        return window.consentWindow;
    }

    function findInPrompt(name) {
        return findIn(prompt().contentItem, name);
    }

    // A consent request; returns its index for testSupport.consentOutcome().
    function ask(tool, categories, device, details) {
        testSupport.requestConsent("Claude Code", tool, categories, device, details);
        return testCase.consentRequests++;
    }

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        McpController.enabled = false;
        tryCompare(McpController, "state", McpController.Off, 10000);
        testSupport.setAiBusy(0, false);
        window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "light";
        testSupport.setAiBusy(0, false);
        testSupport.resetTabs();
        // A configured tab: the probe picker does not scan behind the Settings page.
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        window.width = 1280;
        window.height = 800;
    }

    function openSection() {
        AppViewModel.openSettings("mcp");
        tryVerify(() => find("mcpSection") !== null, 5000);
        waitForRendering(window.contentItem);
    }

    function listen() {
        McpController.port = testSupport.freePort();
        McpController.enabled = true;
        tryCompare(McpController, "state", McpController.Listening, 10000);
        // The probe scan the server starts has delivered before the test goes on (TSan: no scan
        // logs through the test's message handler after a data row ended).
        tryVerify(() => testSupport.mcpRefreshPending() === 0, 10000);
    }

    function test_sectionStates_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_sectionStates(data) {
        AppViewModel.theme = data.theme;
        McpController.enabled = false;
        tryCompare(McpController, "state", McpController.Off, 10000);
        openSection();
        compare(find("mcpState").text, "Off");
        verify(find("mcpRailIndicator") === null); // hidden while off
        verify(!find("mcpEnabled").checked);

        listen();
        compare(find("mcpState").text, "Listening");
        verify(find("mcpEnabled").checked);
        verify(find("mcpToken").text.indexOf("•") === 0);
        compare(findAll(window.contentItem, "mcpSnippet").filter(effectivelyVisible).length, 3);
        // Back to a device tab: the rail shows the indicator with a green dot.
        AppViewModel.currentTab = 0;
        tryVerify(() => find("mcpRailIndicator") !== null);
        verify(find("mcpRailDot") !== null);
        verify(find("mcpRailIndicator").Accessible.name.indexOf("listening on port") > 0);

        // Another program on the port: the error with Retry, the red indicator.
        const blocked = testSupport.blockPort(true);
        McpController.port = blocked;
        tryCompare(McpController, "state", McpController.Error, 10000);
        verify(find("mcpRailIndicator") !== null);
        compare(find("mcpRailDot").kind, "error");
        openSection();
        verify(find("mcpState").text.indexOf(String(blocked)) >= 0);
        verify(find("mcpRetry") !== null);
        testSupport.blockPort(false);
        mouseClick(find("mcpRetry"));
        tryCompare(McpController, "state", McpController.Listening, 10000);
        verify(find("mcpRetry") === null);

        McpController.enabled = false;
        tryCompare(McpController, "state", McpController.Off, 10000);
    }

    function test_permissionsWriteTheKeys() {
        openSection();
        const allow = find("mcpAllow_flash");
        verify(allow !== null);
        verify(find("mcpAsk_flash").checked);
        allow.clicked();
        compare(testSupport.setting("mcp/consent/flash"), "allow");
        tryVerify(() => find("mcpAllow_flash").checked && !find("mcpAsk_flash").checked);
        find("mcpAsk_flash").clicked();
        compare(testSupport.setting("mcp/consent/flash"), "ask");
        verify(find("mcpRule_memory-write") !== null);
    }

    function test_consentWindow_data() {
        return [
            { tag: "flash", tool: "flash", categories: ["flash", "uicr"],
              details: { file: "/home/me/proj/build/merged.hex", size: 421888, target: "app", uicr: true },
              title: "Claude Code wants to flash firmware", remember: "Allow these for Claude Code", rows: 5 },
            { tag: "erase", tool: "erase", categories: ["erase", "uicr"],
              details: { target: "app", scope: "all" },
              title: "Claude Code wants to erase the device", remember: "Allow these for Claude Code", rows: 5 },
            { tag: "memory-write", tool: "write_memory", categories: ["memory-write"],
              details: { target: "app", range: "0x20000000-0x2000000F", size: 16 },
              title: "Claude Code wants to write to the memory", remember: "Allow memory writes for Claude Code", rows: 6 },
            { tag: "connect", tool: "connect", categories: ["connect"], details: {},
              title: "Claude Code wants to connect a device", remember: "Allow connecting devices for Claude Code", rows: 2 },
        ];
    }

    function test_consentWindow(data) {
        const w = prompt();
        verify(!w.visible);
        const device = data.tag === "connect" ? "jlink:801000002" : "jlink:801000001";
        const index = ask(data.tool, data.categories, device, data.details);
        tryVerify(() => w.visible);
        // ADR-0108: own frameless always-on-top window without a transient parent.
        verify(w.flags & Qt.WindowStaysOnTopHint);
        verify(w.flags & Qt.FramelessWindowHint);
        compare(w.transientParent, null);
        compare(findInPrompt("consentTitle").text, data.title);
        compare(findInPrompt("consentAllowForClient").text, data.remember);
        tryCompare(findAll(w.contentItem, "consentDetail").filter(effectivelyVisible), "length", data.rows);
        verify(findInPrompt("consentCountdown").text.indexOf("Denied automatically in ") === 0);
        compare(findInPrompt("consentShowDevice") !== null, data.tag !== "connect");
        // Esc = Deny (the window's own shortcut; key events of QtTest go to the main window).
        compare(String(w.escapeShortcut.sequence), "Esc");
        verify(w.escapeShortcut.enabled);
        w.escapeShortcut.activated();
        tryVerify(() => testSupport.consentOutcome(index) === "mcp.consent_denied");
        tryVerify(() => !w.visible);

        // Allow once.
        const once = ask(data.tool, data.categories, device, data.details);
        tryVerify(() => w.visible);
        tryVerify(() => McpController.consent.armed); // review H1: 700 ms after a new prompt
        mouseClick(findInPrompt("consentAllowOnce"));
        tryVerify(() => testSupport.consentOutcome(once) === "allowed");
        tryVerify(() => !w.visible);
    }

    // Review H1: a double click on Allow never answers the next prompt; the Allow buttons are
    // disabled for 700 ms after each new prompt and the focus is on Deny.
    function test_doubleClickDoesNotAnswerTheNextPrompt() {
        const first = ask("flash", ["flash"], "jlink:801000001", { file: "/tmp/a.hex", size: 1000, target: "app" });
        const second = ask("erase", ["erase"], "jlink:801000001", { target: "app", scope: "code" });
        tryVerify(() => prompt().visible);
        verify(!findInPrompt("consentAllowOnce").enabled);
        tryVerify(() => McpController.consent.armed);
        const button = findInPrompt("consentAllowOnce");
        mouseClick(button);
        mouseClick(button); // the second click of a double click
        tryVerify(() => testSupport.consentOutcome(first) === "allowed");
        compare(testSupport.consentOutcome(second), "pending");
        compare(findInPrompt("consentTitle").text, "Claude Code wants to erase the device");
        verify(!findInPrompt("consentAllowOnce").enabled);
        McpController.consent.deny(McpController.consent.promptId);
        tryVerify(() => testSupport.consentOutcome(second) === "mcp.consent_denied");
    }

    // ADR-0108: closing the main window quits; the prompt never keeps the application alive.
    function test_promptHidesWithTheMainWindow() {
        const index = ask("flash", ["flash"], "jlink:801000001", { file: "/tmp/a.hex", size: 1000, target: "app" });
        tryVerify(() => prompt().visible);
        // Minimised: the prompt stays (ADR-0108).
        window.showMinimized();
        wait(100);
        verify(prompt().visible);
        window.showNormal();
        // Closed (hidden): the prompt goes too, so that closing the window quits.
        window.visible = false;
        tryVerify(() => !prompt().visible);
        window.visible = true;
        tryVerify(() => prompt().visible);
        McpController.consent.deny(McpController.consent.promptId);
        tryVerify(() => testSupport.consentOutcome(index) === "mcp.consent_denied");
    }

    function test_queueAndRememberedGrant() {
        openSection();
        verify(find("mcpNoGrants") !== null);
        const first = ask("flash", ["flash"], "jlink:801000001", { file: "/tmp/a.hex", size: 1000, target: "app" });
        const second = ask("flash", ["flash"], "jlink:801000001", { file: "/tmp/b.hex", size: 1000, target: "app" });
        tryVerify(() => prompt().visible);
        tryVerify(() => findInPrompt("consentQueued") !== null);
        tryVerify(() => McpController.consent.armed);
        mouseClick(findInPrompt("consentAllowForClient"));
        tryVerify(() => testSupport.consentOutcome(first) === "allowed");
        tryVerify(() => testSupport.consentOutcome(second) === "allowed"); // covered by the grant
        tryVerify(() => !prompt().visible);
        tryVerify(() => find("mcpGrant") !== null);
        find("mcpRevoke").clicked(); // below the visible part of the page
        tryVerify(() => find("mcpGrant") === null);
    }

    function test_activityList() {
        openSection();
        McpController.activity.clear();
        verify(find("mcpNoActivity") !== null);
        testSupport.recordActivity("Claude Code", "send_command", "ok");
        testSupport.recordActivity("Claude Code", "flash", "mcp.consent_denied");
        tryVerify(() => find("mcpActivity") !== null);
        const results = () => findAll(window.contentItem, "mcpActivityResult").filter(effectivelyVisible);
        tryVerify(() => results().length === 2);
        compare(results()[0].text, "mcp.consent_denied"); // newest first
        find("mcpClearActivity").clicked();
        tryVerify(() => find("mcpNoActivity") !== null);
    }

    function test_tabBadgeCancelAndInputHint() {
        AppViewModel.currentTab = 0;
        testSupport.setOpenedBy(0, "Claude Code");
        tryVerify(() => find("tabAiBadge") !== null);
        testSupport.setAiBusy(0, true);
        tryVerify(() => find("aiCancel") !== null);
        mouseClick(find("aiCancel"));
        verify(testSupport.aiCancelled());
        testSupport.setAiBusy(0, false);
        tryVerify(() => find("aiCancel") === null);
        testSupport.setOpenedBy(0, "");
        tryVerify(() => find("tabAiBadge") === null);
        // The console input hint while an AI command runs (the input stays enabled).
        const tab = AppViewModel.currentTabViewModel;
        tab.connection.connectDevice();
        tryCompare(tab, "state", DeviceTabViewModel.Connected, 10000);
        AppViewModel.showPage("console");
        tryVerify(() => find("inputPlaceholder") !== null, 5000);
        testSupport.setAiCommandActive(0, true);
        tryCompare(find("inputPlaceholder"), "text", "AI is running a command…");
        testSupport.setAiCommandActive(0, false);
        tryVerify(() => find("inputPlaceholder").text !== "AI is running a command…");
        tab.connection.disconnectDevice();
    }

    function test_layoutCzech960() {
        AppViewModel.setLanguage("cs");
        window.width = 960;
        window.height = 600;
        listen();
        openSection();
        const section = find("mcpSection");
        verify(section !== null);
        const right = section.mapToItem(window.contentItem, section.width, 0).x;
        verify(right <= window.width, "the section fits at 960 px: " + right);
        compare(find("mcpState").text, "Naslouchá");
        McpController.enabled = false;
        tryCompare(McpController, "state", McpController.Off, 10000);
    }

    // ---- screenshots (HWIO_SCREENSHOT_DIR) ----

    function save(item, name) {
        waitForRendering(item, 2000);
        wait(250);
        const image = grabImage(item);
        verify(image.width > 0, name);
        image.save(testSupport.screenshotDir + "/" + name + "-" + window.width + "x" + window.height + "-"
                   + AppViewModel.theme + "-" + AppViewModel.language + ".png");
    }

    function test_screenshots_data() {
        let rows = [];
        for (const theme of ["light", "dark"]) {
            for (const language of ["en", "cs"])
                rows.push({ tag: theme + "-" + language, theme: theme, language: language });
        }
        return rows;
    }

    function test_screenshots(data) {
        if (testSupport.screenshotDir === "")
            skip("HWIO_SCREENSHOT_DIR is not set");
        AppViewModel.theme = data.theme;
        AppViewModel.setLanguage(data.language);
        window.width = 960;
        window.height = 600;
        listen();
        testSupport.recordActivity("Claude Code", "flash", "ok");
        testSupport.recordActivity("Claude Code", "send_command", "ok");
        openSection();
        // Scrolled to the section (its heading at the top).
        const flickable = find("settingsScroll").contentItem;
        const top = find("mcpSection").mapToItem(flickable.contentItem, 0, 0).y - 60;
        flickable.contentY = Math.max(0, Math.min(top, flickable.contentHeight - flickable.height));
        save(window.contentItem, "mcp-01-settings");

        const index = ask("flash", ["flash", "uicr"], "jlink:801000001",
                          { file: "/home/me/proj/build/merged.hex", size: 421888, target: "app", uicr: true });
        tryVerify(() => prompt().visible);
        save(prompt().contentItem, "mcp-02-consent");
        McpController.consent.deny(McpController.consent.promptId);
        tryVerify(() => testSupport.consentOutcome(index) !== "pending");

        AppViewModel.currentTab = 0;
        testSupport.setOpenedBy(0, "Claude Code");
        testSupport.setAiBusy(0, true);
        tryVerify(() => find("aiCancel") !== null);
        save(window.contentItem, "mcp-03-ai-tab");
        testSupport.setAiBusy(0, false);
        testSupport.setOpenedBy(0, "");
        McpController.activity.clear();
        McpController.enabled = false;
        tryCompare(McpController, "state", McpController.Off, 10000);
    }
}
