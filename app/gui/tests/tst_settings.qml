// The Settings page (plan 0006): the sections in registry order, live theme, the J-Link default
// speed with "Automatic speed" (ADR-0111) and its slow-speed warning (ADR-0110), the detected
// J-Link library, nRF Util "not found", "Run the setup wizard" hidden while the setup wizard is
// not registered, both themes, and the layout at 960 x 600 in Czech. Plan 0045: the rows of each
// user mode, the Mode selector, a hidden section / row revealed for one visit, and the first-start
// mode dialog.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "Settings"
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

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        // The tab stays configured: no probe picker (and no probe scan) while the test ends.
        window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        verify(testSupport.setSetting("ui/mode", "advanced"));
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "light";
        testSupport.resetTabs();
        // A configured tab: the probe picker does not scan behind the Settings page.
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        window.width = 1280;
        window.height = 800;
        AppViewModel.openGlobalPage("settings");
        tryVerify(() => find("settingsPage") !== null);
        tryVerify(() => find("defaultAutoSpeed") !== null && find("defaultAutoSpeed").width > 0, 5000);
        waitForRendering(window.contentItem);
    }

    function test_sectionsAndDetection() {
        const sections = findAll(window.contentItem, "sectionLoader").filter(effectivelyVisible);
        // Advanced mode (plan 0045): General, Console (0005), J-Link, nRF Util, AI assistants
        // (0014), About; Expert (0033) only in Expert mode.
        compare(sections.length, 6);
        verify(!find("section_expert"));
        verify(find("section_mcp") !== null);
        verify(find("section_general") !== null);
        verify(find("section_console") !== null);
        verify(find("section_about") !== null);
        verify(!find("readOnlyBanner"));
        tryVerify(() => find("jlinkDetected").text.indexOf("fake_jlink") >= 0, 10000);
        tryCompare(find("nrfUtilDetected"), "text", "Not found — needed only for LTE modem firmware", 10000);
        // Wired, hidden until the setup wizard (0008) registers.
        verify(!find("jlinkSetupWizard"));
        verify(!find("nrfUtilSetupWizard"));
        verify(find("aboutText").text.indexOf("HARDWARIO Monitor") === 0);
    }

    // Plan 0044 (ADR-0330): Settings → Expert → "Support server", https only (Expert mode).
    function test_relayUrlField() {
        verify(testSupport.setSetting("ui/mode", "expert"));
        tryVerify(() => find("relayUrlField") !== null);
        const field = find("relayUrlField");
        verify(field !== null);
        const vm = AppViewModel.globalPageViewModel("settings");
        compare(field.placeholderText, vm.defaultRelayUrl);
        field.text = "http://relay.example.com";
        field.editingFinished();
        tryVerify(() => find("relayUrlError") !== null);
        compare(vm.relayUrl, "");
        field.text = "https://relay.example.com";
        field.editingFinished();
        tryVerify(() => find("relayUrlError") === null);
        compare(vm.relayUrl, "https://relay.example.com");
        compare(vm.setRelayUrl(""), "");
        compare(field.text, "");
    }

    // Plan 0045 (ADR-0359): for each mode exactly the rows of the plan's table are visible.
    function test_modeRows_data() {
        const all = ["modeSelector", "languageSelector", "themeSelector", "restoreTabsSwitch",
                     "consoleFontSize", "consoleTimestamps", "consoleWrap", "consoleLogToFile",
                     "consoleLogFolder", "aboutText", "updateAutoCheck", "cliRow", "licenceFiles"];
        const advanced = ["consoleSuggestions", "consoleReadCommands", "consoleRememberCommands",
                          "consoleClearHistory", "section_jlink", "jlinkAuto", "defaultAutoSpeed",
                          "defaultAutoReconnect", "section_nrfutil", "nrfUtilAuto", "section_mcp",
                          "mcpEnabled", "updatePrerelease"];
        const expert = ["defaultLatencyBox", "defaultResetOnConnect", "section_expert", "relayUrlField"];
        return [
            { tag: "basic", mode: "basic", shown: all, hidden: advanced.concat(expert) },
            { tag: "advanced", mode: "advanced", shown: all.concat(advanced), hidden: expert },
            { tag: "expert", mode: "expert", shown: all.concat(advanced, expert), hidden: [] }
        ];
    }

    function test_modeRows(data) {
        verify(testSupport.setSetting("ui/mode", data.mode));
        window.height = 4000; // every section laid out
        waitForRendering(window.contentItem);
        for (const name of data.shown)
            tryVerify(() => find(name) !== null, 3000, `${name} is shown in ${data.mode}`);
        for (const name of data.hidden)
            verify(find(name) === null, `${name} is hidden in ${data.mode}`);
        verify(!find("revealNote"));
    }

    // The Mode selector: the description follows; Settings and the Flash maintenance change at once.
    function test_modeSelector() {
        const vm = AppViewModel.globalPageViewModel("settings");
        compare(vm.mode, UiMode.Advanced);
        compare(find("modeDescription").text, "Develop with CHESTER: J-Link and nRF Util options, AI assistants, console tools.");
        compare(find("modeButton_" + UiMode.Advanced).kind, "primary");
        const flash = testSupport.flashPage(0);
        verify(flash !== null);
        verify(!flash.expertActions);
        mouseClick(find("modeButton_" + UiMode.Expert));
        compare(vm.mode, UiMode.Expert);
        compare(testSupport.setting("ui/mode"), "expert");
        verify(flash.expertActions);
        compare(find("modeDescription").text, "Full control, including timing options and maintenance actions that can erase the device's identity.");
        tryVerify(() => find("section_expert") !== null);
        wait(Qt.styleHints.mouseDoubleClickInterval + 50); // two separate clicks, not a double click
        mouseClick(find("modeButton_" + UiMode.Basic));
        compare(vm.mode, UiMode.Basic);
        verify(!flash.expertActions);
        tryVerify(() => find("section_jlink") === null);
        compare(find("modeDescription").text, "Watch the console and flash firmware. Only the essential settings.");
        compare(find("modeButton_" + UiMode.Basic).kind, "primary");
        compare(find("modeButton_" + UiMode.Expert).kind, "normal");
    }

    // ADR-0361: an error's "Open Settings" in Basic mode shows the J-Link section with the note;
    // leaving Settings hides it again, the mode is unchanged.
    function test_revealHiddenSection() {
        verify(testSupport.setSetting("ui/mode", "basic"));
        tryVerify(() => find("section_jlink") === null);
        AppViewModel.openSettings("jlink");
        tryVerify(() => find("section_jlink") !== null);
        const note = findAll(find("section_jlink"), "revealNote").filter(effectivelyVisible);
        compare(note.length, 1);
        compare(note[0].text, "Shown because it was needed. It is part of the Advanced mode.");
        verify(find("jlinkAuto") !== null);
        verify(!find("defaultLatencyBox")); // Expert rows stay hidden
        tryCompare(AppViewModel, "settingsSection", ""); // scrolled
        compare(AppViewModel.globalPageViewModel("settings").mode, UiMode.Basic);
        AppViewModel.currentTab = 0; // leave Settings
        AppViewModel.openGlobalPage("settings");
        tryVerify(() => find("settingsPage") !== null);
        tryVerify(() => find("section_jlink") === null);
        verify(!find("revealNote"));
    }

    // ADR-0361: a row hidden by the mode (Expert) opened in Advanced mode, with its note.
    function test_revealHiddenRow() {
        verify(!find("defaultLatencyBox"));
        AppViewModel.openSettings("jlink", "jlinkLatency");
        tryVerify(() => find("defaultLatencyBox") !== null);
        const notes = findAll(window.contentItem, "revealNote").filter(effectivelyVisible);
        compare(notes.length, 1);
        compare(notes[0].text, "Shown because it was needed. It is part of the Expert mode.");
        verify(!find("defaultResetOnConnect"));
        tryCompare(AppViewModel, "settingsSection", ""); // scrolled
        AppViewModel.closeSettings();
        AppViewModel.openGlobalPage("settings");
        tryVerify(() => find("settingsPage") !== null);
        tryVerify(() => find("defaultAutoSpeed") !== null);
        verify(!find("defaultLatencyBox"));
    }

    // ADR-0359: the first-start dialog - Advanced preselected, Continue stores the choice, not
    // shown again; ADR-0362: with the rc.3 expert switch on, Expert is preselected and Esc keeps it.
    function test_modeChoiceDialog() {
        const dialog = findChild(window, "modeChoiceDialog");
        verify(dialog !== null);
        verify(!dialog.visible);
        verify(testSupport.resetUiMode());
        tryVerify(() => dialog.opened);
        verify(!AppViewModel.uiMode.chosen);
        compare(dialog.selected, UiMode.Advanced);
        compare(findChild(dialog.contentItem, "modeCard_" + UiMode.Advanced).Accessible.checked, true);
        mouseClick(findChild(dialog.contentItem, "modeCard_" + UiMode.Basic));
        compare(dialog.selected, UiMode.Basic);
        mouseClick(findChild(dialog.footer, "modeChoiceContinue"));
        tryVerify(() => !dialog.visible);
        verify(AppViewModel.uiMode.chosen);
        compare(AppViewModel.uiMode.mode, UiMode.Basic);
        compare(testSupport.setting("ui/mode"), "basic");
        wait(100);
        verify(!dialog.visible); // not shown again

        verify(testSupport.setSetting("advanced/expertActions", true));
        verify(testSupport.resetUiMode());
        tryVerify(() => dialog.opened);
        compare(dialog.selected, UiMode.Expert);
        mouseClick(findChild(dialog.contentItem, "modeCard_" + UiMode.Basic));
        keyClick(Qt.Key_Escape); // closing keeps the preselection
        tryVerify(() => !dialog.visible);
        compare(AppViewModel.uiMode.mode, UiMode.Expert);
        compare(testSupport.setting("ui/mode"), "expert");
        compare(testSupport.setting("advanced/expertActions"), "false"); // removed (ADR-0360)
    }

    // The dialog fits at 960 x 600 in the longest languages.
    function test_modeChoiceDialogLayout_data() {
        return [{ tag: "de" }, { tag: "pl" }, { tag: "cs" }, { tag: "sk" }];
    }

    function test_modeChoiceDialogLayout(data) {
        AppViewModel.setLanguage(data.tag);
        window.width = 960;
        window.height = 600;
        const dialog = findChild(window, "modeChoiceDialog");
        verify(testSupport.resetUiMode());
        tryVerify(() => dialog.opened);
        waitForRendering(window.contentItem);
        verify(dialog.y >= 0 && dialog.y + dialog.height <= window.height + 0.5, `dialog height ${dialog.height}`);
        const button = findChild(dialog.footer, "modeChoiceContinue");
        const r = button.mapToItem(window.contentItem, 0, 0);
        verify(r.y + button.height <= window.height + 0.5 && r.x + button.width <= window.width + 0.5);
        mouseClick(button);
        tryVerify(() => !dialog.visible);
        verify(AppViewModel.uiMode.chosen);
    }

    function test_themeIsLive() {
        const box = find("themeSelector");
        box.activated(2);
        compare(AppViewModel.theme, "dark");
        compare(Theme.dark, true);
        compare(box.currentIndex, 2);
    }

    function test_languageIsLive() {
        const box = find("languageSelector");
        const vm = AppViewModel.globalPageViewModel("settings");
        let czech = -1;
        for (let i = 0; i < vm.languages.length; ++i) {
            if (vm.languages[i].code === "cs")
                czech = i;
        }
        verify(czech > 0);
        box.activated(czech);
        compare(AppViewModel.language, "cs");
        tryCompare(find("settingsTitle"), "text", "Nastavení");
        compare(box.currentIndex, czech);
    }

    // ADR-0210: "System", then the native names in the order of hwio::i18n::supportedLanguages.
    function test_languageList() {
        const vm = AppViewModel.globalPageViewModel("settings");
        compare(vm.languages.map(language => language.code), ["system", "en", "cs", "de", "pl", "sk"]);
        compare(vm.languages.slice(1).map(language => language.name),
                ["English", "Čeština", "Deutsch", "Polski", "Slovenčina"]);
    }

    function test_defaultSpeed() {
        const vm = AppViewModel.globalPageViewModel("settings");
        // 0021 (ADR-0172): "Open Settings" scrolls to the section.
        AppViewModel.openSettings("jlink");
        const automatic = find("defaultAutoSpeed");
        tryVerify(() => automatic.mapToItem(window.contentItem, 0, 0).y + automatic.height < window.height);
        waitForRendering(window.contentItem);
        const speedBox = find("defaultSpeedBox");
        verify(automatic.checked);
        verify(!speedBox.enabled);
        mouseClick(automatic);
        verify(!vm.autoSpeed);
        verify(speedBox.enabled);
        vm.defaultSpeedKhz = 100;
        tryVerify(() => find("defaultSpeedWarning") !== null);
        mouseClick(automatic);
        verify(vm.autoSpeed);
        compare(speedBox.value, 100); // kept for later
        verify(!find("defaultSpeedWarning"));
    }

    // Plan 0034 (B2): "Open Settings" with a section while Settings is not open yet: the page is
    // created, laid out, and then scrolled to the section; the request is cleared only after that.
    function test_sectionOnFirstOpen() {
        AppViewModel.closeGlobalPage();
        tryVerify(() => findAll(window.contentItem, "settingsPage").length === 0);
        AppViewModel.openSettings("nrfutil");
        tryVerify(() => find("settingsPage") !== null);
        const flickable = findAll(window.contentItem, "settingsScroll")[0].contentItem;
        tryVerify(() => find("section_nrfutil") !== null);
        const section = find("section_nrfutil");
        tryVerify(() => section.y > 0 && flickable.contentY > 0
                  && Math.abs(flickable.contentY - Math.min(section.y, flickable.contentHeight - flickable.height)) < 1,
                  5000, "scrolled to the nRF Util section");
        compare(AppViewModel.settingsSection, "");
    }

    // Plan 0034 (B2): an unknown section id is dropped (no scroll, request cleared).
    function test_unknownSection() {
        const page = find("settingsPage");
        const flickable = findAll(window.contentItem, "settingsScroll")[0].contentItem;
        flickable.contentY = 0;
        AppViewModel.openSettings("no-such-section");
        tryCompare(AppViewModel, "settingsSection", "");
        compare(page.pendingSection, "");
        compare(flickable.contentY, 0);
    }

    function test_themes_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_themes(data) {
        AppViewModel.theme = data.theme;
        compare(Theme.dark, data.theme === "dark");
        verify(find("settingsTitle") !== null);
    }

    function test_layout960_data() {
        return [{ tag: "cs", title: "Nastavení" }, { tag: "de", title: "Einstellungen" },
                { tag: "pl", title: "Ustawienia" }, { tag: "sk", title: "Nastavenia" }];
    }

    function test_layout960(data) {
        verify(testSupport.setSetting("ui/mode", "expert")); // every row
        AppViewModel.setLanguage(data.tag);
        window.width = 960;
        window.height = 600;
        waitForRendering(window.contentItem);
        for (const name of ["modeSelector", "languageSelector", "themeSelector", "defaultSpeedBox", "defaultLatencyBox", "copyAbout"]) {
            const item = find(name);
            verify(item !== null, name);
            const r = rect(item);
            verify(r.x >= -0.5 && r.x + r.w <= window.width + 0.5, `${name} is clipped: x=${r.x} w=${r.w}`);
        }
        compare(find("settingsTitle").text, data.title);
    }
}
