// MCP in the GUI (plan 0014 B1-B3): Settings → AI assistants in its states (off, listening, error
// with Retry) in light and dark, the permission radios writing mcp/consent/<id> (plan 0055: the
// rows keep their objects and the focus; the All row with its confirmation), the consent
// window per category (detail rows, countdown, Esc = Deny, Allow once, Always allow for the client, the
// catalog firmware by name in Czech at the window's 480 px (plan 0054), the
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

    // Clicking the option that is already selected keeps it selected (both radio buttons used to
    // toggle off, leaving the rule without a visible choice).
    function test_permissionsReclickKeepsSelection() {
        openSection();
        // The rows stay the same objects since plan 0055; looking the buttons up again after each
        // click also keeps this test independent of that.
        const ask = () => find("mcpAsk_flash");
        const allow = () => find("mcpAllow_flash");
        // Real mouse clicks: wait for the page's own scroll to the section, then bring the rule
        // (below the fold of the 800 px window) into view.
        tryCompare(AppViewModel, "settingsSection", "", 5000);
        const flickable = findAll(window.contentItem, "settingsScroll")[0].contentItem;
        const y = allow().mapToItem(flickable.contentItem, 0, 0).y;
        flickable.contentY = Math.max(0, Math.min(y - 60, flickable.contentHeight - flickable.height));
        waitForRendering(window.contentItem);
        verify(ask().checked && !allow().checked);
        mouseClick(ask());
        verify(ask().checked && !allow().checked);
        compare(testSupport.setting("mcp/consent/flash"), "ask");
        mouseClick(allow());
        compare(testSupport.setting("mcp/consent/flash"), "allow");
        tryVerify(() => allow().checked && !ask().checked);
        mouseClick(allow());
        verify(allow().checked && !ask().checked);
        compare(testSupport.setting("mcp/consent/flash"), "allow");
        // Not a double click with the previous press (a double click does not emit clicked()).
        wait(Qt.styleHints.mouseDoubleClickInterval + 50);
        mouseClick(ask());
        compare(testSupport.setting("mcp/consent/flash"), "ask");
        tryVerify(() => ask().checked && !allow().checked);
    }

    // Plan 0055 (ADR-0390): a click changes one row only; the row delegates and the buttons are the
    // same objects afterwards (no rebuild, no flicker) and the focus stays on the clicked button.
    function test_permissionsKeepTheRowsAndTheFocus() {
        McpController.setAllConsentRules(false);
        openSection();
        const ids = ["flash", "erase", "memory-write", "uicr", "connect"];
        const rows = ids.map(id => find("mcpRule_" + id));
        rows.forEach(row => verify(row !== null));
        const askButtons = ids.map(id => find("mcpAsk_" + id));
        const allowErase = find("mcpAllow_erase");
        // The page's own scroll to the section has finished (it must not move the focus later).
        tryCompare(AppViewModel, "settingsSection", "", 5000);
        // Keyboard: focus the button and press Space (click-to-focus differs per platform).
        window.requestActivate();
        allowErase.forceActiveFocus();
        tryVerify(() => allowErase.activeFocus);
        keyClick(Qt.Key_Space);
        compare(testSupport.setting("mcp/consent/erase"), "allow");
        tryVerify(() => allowErase.checked && !find("mcpAsk_erase").checked);
        verify(allowErase.activeFocus);
        verify(find("mcpAllow_erase") === allowErase);
        ids.forEach((id, i) => {
            verify(find("mcpRule_" + id) === rows[i], id);
            verify(find("mcpAsk_" + id) === askButtons[i], id);
        });
        // The other rows are unchanged.
        verify(find("mcpAsk_flash").checked && !find("mcpAllow_flash").checked);
        McpController.setConsentRule("erase", false);
        tryVerify(() => find("mcpAsk_erase").checked);
        verify(find("mcpRule_erase") === rows[1]);
    }

    // Plan 0055 (ADR-0390): the All row. Ask sets every category at once; Always allow asks first
    // (Cancel changes nothing); a button is selected only when all categories agree.
    function test_permissionsAllRow() {
        McpController.setAllConsentRules(false);
        openSection();
        const ids = ["flash", "erase", "memory-write", "uicr", "connect"];
        const rows = ids.map(id => find("mcpRule_" + id));
        const askAll = find("mcpAskAll");
        const allowAll = find("mcpAllowAll");
        verify(askAll !== null && allowAll !== null);
        verify(askAll.checked && !allowAll.checked);

        // Mixed: neither is selected.
        McpController.setConsentRule("uicr", true);
        tryVerify(() => !askAll.checked && !allowAll.checked);

        // All › Ask: at once, without a question.
        askAll.clicked();
        ids.forEach(id => compare(testSupport.setting("mcp/consent/" + id), "ask", id));
        tryVerify(() => askAll.checked && !allowAll.checked);

        // All › Always allow: Cancel changes nothing. The consent-window cases before this one
        // activate the prompt window, so activate the main window for the focus check.
        window.requestActivate();
        tryVerify(() => window.active);
        const dialog = findChild(find("mcpSection"), "mcpAllowAllConfirm");
        verify(dialog !== null);
        allowAll.clicked();
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Always allow everything?");
        compare(findChild(dialog.footer, "dialogAccept").text, "Always allow everything");
        tryVerify(() => findChild(dialog.footer, "dialogReject").activeFocus); // ADR-0038
        mouseClick(findChild(dialog.footer, "dialogReject"));
        tryVerify(() => !dialog.visible);
        ids.forEach(id => compare(testSupport.setting("mcp/consent/" + id), "ask", id));
        verify(askAll.checked && !allowAll.checked);

        // ... and the confirmation sets all five.
        allowAll.clicked();
        tryVerify(() => dialog.opened);
        mouseClick(findChild(dialog.footer, "dialogAccept"));
        tryVerify(() => !dialog.visible);
        ids.forEach(id => compare(testSupport.setting("mcp/consent/" + id), "allow", id));
        tryVerify(() => allowAll.checked && !askAll.checked);
        ids.forEach((id, i) => {
            verify(find("mcpRule_" + id) === rows[i], id);
            verify(find("mcpAllow_" + id).checked, id);
        });
        // Clicking it again while everything is allowed asks nothing.
        allowAll.clicked();
        verify(!dialog.visible);

        McpController.setAllConsentRules(false);
        tryVerify(() => askAll.checked && !allowAll.checked);
    }

    function test_consentWindow_data() {
        return [
            { tag: "flash", tool: "flash", categories: ["flash", "uicr"],
              details: { file: "/home/me/proj/build/merged.hex", size: 421888, target: "app", uicr: true },
              title: "Claude Code wants to flash firmware", remember: "Always allow these for Claude Code", rows: 5 },
            { tag: "erase", tool: "erase", categories: ["erase", "uicr"],
              details: { target: "app", scope: "all" },
              title: "Claude Code wants to erase the device", remember: "Always allow these for Claude Code", rows: 5 },
            { tag: "memory-write", tool: "write_memory", categories: ["memory-write"],
              details: { target: "app", range: "0x20000000-0x2000000F", size: 16 },
              title: "Claude Code wants to write to the memory", remember: "Always allow memory writes for Claude Code", rows: 6 },
            { tag: "connect", tool: "connect", categories: ["connect"], details: {},
              title: "Claude Code wants to connect a device", remember: "Always allow connecting devices for Claude Code", rows: 2 },
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

    // Plan 0062 (ADR-0436): support's prompt for a customer's device: its own title and rows,
    // "Allow for this session", no "Always allow" button, Esc = Deny; an operation on a customer's
    // device names it and has no "Always allow" either.
    function test_remoteConsentWindow() {
        const w = prompt();
        verify(!w.visible);
        testSupport.requestRemoteConsent("Claude Code", "read_terminal", ["remote-device"], "remote:1", { remote: true });
        const index = testCase.consentRequests++;
        tryVerify(() => w.visible);
        compare(findInPrompt("consentTitle").text, "Claude Code wants to work with a customer's device");
        compare(findInPrompt("consentAllowOnce").text, "Allow for this session");
        compare(findInPrompt("consentAllowForClient"), null);
        const values = () => findAll(w.contentItem, "consentDetailValue").filter(effectivelyVisible).map(item => item.text);
        tryVerify(() => values().indexOf("It will read the customer's console and may send commands to the customer's device. Secret values are hidden from it. The customer sees its commands as yours.") >= 0);
        verify(McpController.consent.details[0].label === "Session", McpController.consent.details[0].label);
        w.escapeShortcut.activated();
        tryVerify(() => testSupport.consentOutcome(index) === "mcp.consent_denied");
        tryVerify(() => !w.visible);

        testSupport.requestRemoteConsent("Claude Code", "flash", ["flash"], "remote:1",
                                         { catalog_id: "59489e4e91d741da930ab15b1ab9e31f", target: "app", remote: true });
        const flash = testCase.consentRequests++;
        tryVerify(() => w.visible);
        compare(findInPrompt("consentTitle").text, "Claude Code wants to flash firmware");
        compare(findInPrompt("consentAllowOnce").text, "Allow once");
        compare(findInPrompt("consentAllowForClient"), null);
        tryVerify(() => values().indexOf("the customer's device (remote session)") >= 0);
        tryVerify(() => McpController.consent.armed);
        McpController.consent.allowForClient(McpController.consent.promptId); // ignored
        verify(w.visible);
        mouseClick(findInPrompt("consentAllowOnce"));
        tryVerify(() => testSupport.consentOutcome(flash) === "allowed");
        tryVerify(() => !w.visible);
    }

    // Plan 0062 (ADR-0433): the remote support sessions switch, off by default.
    function test_remoteSessionsSwitch() {
        openSection();
        const toggle = find("mcpRemoteSessions");
        verify(toggle !== null);
        verify(!toggle.checked);
        verify(!McpController.remoteSessions);
        toggle.toggle();
        toggle.toggled();
        tryVerify(() => McpController.remoteSessions);
        compare(testSupport.setting("mcp/remoteSessions"), "true");
        toggle.toggle();
        toggle.toggled();
        tryVerify(() => !McpController.remoteSessions);
    }

    // Plan 0054 (ADR-0389): catalog firmware by name with the full ID as a note, the Show device
    // link in the device row, the MCU title, the client last, "Always allow" with its tooltip -
    // and everything fits the fixed 480 px window in Czech.
    function test_consentWindowCatalogCzech() {
        AppViewModel.setLanguage("cs");
        const w = prompt();
        const id = "59489e4e91d741da930ab15b1ab9e31f";
        const index = ask("flash", ["flash"], "jlink:801000001",
                          { catalog_id: id, firmware_name: "hio-chester-clime", firmware_version: "v3.2.0",
                            size: 660963, target: "app" });
        tryVerify(() => w.visible);
        compare(w.width, 480);
        tryCompare(findAll(w.contentItem, "consentDetail").filter(effectivelyVisible), "length", 4);
        waitForRendering(w.contentItem);
        const rows = findAll(w.contentItem, "consentDetail").filter(effectivelyVisible);
        // Firmware first: name and version, the full catalog ID as the note.
        verify(findAll(rows[0], "consentDetailValue")[0].text.indexOf("hio-chester-clime v3.2.0") === 0);
        const note = findAll(rows[0], "consentDetailNote")[0];
        verify(effectivelyVisible(note));
        verify(note.text.indexOf(id) >= 0, note.text);
        // The Show device link is in the device row (the second), and only there.
        compare(findAll(rows[1], "consentShowDevice").filter(effectivelyVisible).length, 1);
        compare(findAll(w.contentItem, "consentShowDevice").filter(effectivelyVisible).length, 1);
        // The client last: "<name>, <version>" in the GUI language.
        verify(findAll(rows[3], "consentDetailValue")[0].text.indexOf("Claude Code, ") === 0);
        const remember = findInPrompt("consentAllowForClient");
        verify(remember.text.indexOf("Claude Code") > 0, remember.text);
        verify(remember.toolTipText !== "");
        verify(remember.toolTipText !== "Until you remove it in Settings › AI assistants (MCP).", "translated");
        // Nothing sticks out of the window; the remember text is not clipped (it wraps).
        for (const name of ["consentDeny", "consentAllowOnce", "consentAllowForClient", "consentShowDevice",
                            "consentDetailNote"]) {
            const item = findInPrompt(name);
            verify(item !== null, name);
            const right = item.mapToItem(w.contentItem, item.width, 0).x;
            verify(right <= w.width, name + " fits: " + right);
        }
        const rememberText = findInPrompt("consentAllowForClientText");
        verify(rememberText.contentWidth <= rememberText.width + 1, "not clipped: " + rememberText.contentWidth);
        McpController.consent.deny(McpController.consent.promptId);
        tryVerify(() => testSupport.consentOutcome(index) === "mcp.consent_denied");
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
