// Remote support session in the GUI (plan 0044 C1/C2): Share with support → dialog → banner with
// the code; a join opens the approval dialog with End session as the default; allowed commands
// reach the device with the remote marker; an operation request opens the confirmation with Deny as
// the default; End sharing. Joining: the Join dialog validates the code, a remote tab opens, the
// web Terminal host's sync fills the console, the Flash page explains that only the console is
// available, and the tab stays (read-only) after the session ended. The relay is the in-process
// FakeRelay (testSupport.startFakeRelay).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

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

    function shown(name) {
        return findAll(window.contentItem, name).concat(findAll(window.overlay, name))
                .filter(effectivelyVisible);
    }

    function one(name) {
        const items = shown(name);
        verify(items.length === 1, name + ": " + items.length);
        return items[0];
    }

    function tab() {
        return AppViewModel.currentTabViewModel;
    }

    function initTestCase() {
        AppViewModel.setLanguage("en");
        testSupport.resetTabs();
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        testSupport.resetTabs();
        testSupport.stopFakeRelay();
        window.destroy();
        wait(50);
    }

    function init() {
        testSupport.resetTabs();
        verify(testSupport.startFakeRelay());
    }

    function cleanup() {
        testSupport.resetTabs();
        testSupport.stopFakeRelay();
    }

    function connectConsole() {
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        tab().connection.connectDevice();
        AppViewModel.showPage("console");
        tryVerify(() => tab().state === DeviceTabViewModel.Connected, 10000);
        tryVerify(() => shown("shareButton").length === 1, 5000);
    }

    function test_shareJoinApprovalAndOperation() {
        connectConsole();
        const share = tab().remoteShare;
        tryVerify(() => share.available, 5000);
        mouseClick(one("shareButton"));
        const shareDialog = findChild(one("shareButton"), "remoteShareDialog");
        tryVerify(() => shareDialog.opened, 5000);
        mouseClick(findChild(shareDialog.footer, "dialogAccept")); // Start sharing
        tryVerify(() => share.state === "sharing", 10000);
        tryVerify(() => shown("remoteShareBanner").length === 1, 5000);
        verify(share.code.length === 7, share.code); // "482 913"
        compare(one("remoteShareCode").text, share.code);

        verify(testSupport.remoteJoin(share.code));
        tryVerify(() => share.joinQuestionOpen, 10000);
        const dialogs = findAll(window.contentItem, "remoteSessionDialogs").filter(effectivelyVisible)[0];
        const join = findChild(dialogs, "remoteJoinDialog");
        tryVerify(() => join.opened, 5000);
        // End session is the default (focused) button.
        const reject = findChild(join.footer, "dialogReject");
        tryVerify(() => reject.activeFocus, 5000);
        compare(reject.text, "End session");
        mouseClick(findChild(join.footer, "dialogAccept")); // Allow
        tryVerify(() => share.participantAllowed, 5000);
        tryVerify(() => testSupport.remoteReceived("sync") === 1, 5000);
        // ADR-0364: the tab's command tree goes to the allowed participant, also when it grows.
        testSupport.setConsoleCommands(AppViewModel.currentTab, ["app", "lte"]);
        tryVerify(() => testSupport.remoteReceivedCommands() === "app lte", 5000);

        testSupport.remoteRequest("reset", "app");
        tryVerify(() => share.operationQuestionOpen, 10000);
        const operation = findChild(dialogs, "remoteOperationDialog");
        tryVerify(() => operation.opened, 5000);
        const deny = findChild(operation.footer, "dialogReject");
        tryVerify(() => deny.activeFocus, 5000);
        compare(deny.text, "Deny");
        keyClick(Qt.Key_Escape); // Esc = Deny
        tryVerify(() => !share.operationQuestionOpen, 5000);
        tryVerify(() => !operation.visible, 5000); // the modal dialog has closed

        mouseClick(one("remoteEndSharing"));
        tryVerify(() => !share.sharing, 5000);
        tryVerify(() => shown("remoteShareBanner").length === 0, 5000);
    }

    function test_joinAWebTerminalHost() {
        AppViewModel.showPage("console");
        mouseClick(one("joinSupportSession"));
        const input = findChild(one("transportBar").parent, "remoteJoinInputDialog");
        tryVerify(() => input.opened, 5000);
        const field = findChild(input.contentItem, "remoteCodeField");
        const joinButton = findChild(input.footer, "remoteJoinButton");
        field.text = "12a";
        tryVerify(() => findChild(input.contentItem, "remoteCodeError").visible, 5000);
        verify(!joinButton.enabled);
        field.text = "12 345";
        tryVerify(() => joinButton.enabled, 5000);
        // The web Terminal host is in the session before the joiner.
        verify(testSupport.remoteJoin("12345"));
        mouseClick(joinButton);
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        tryVerify(() => shown("remoteJoinBanner").length === 1, 5000);

        testSupport.remoteHostSync("hello from the web host");
        tryVerify(() => tab().remoteJoin.state === "joined", 10000);
        compare(tab().title, "Session 12345 — web Terminal");
        tryVerify(() => testSupport.consoleLastSeq(AppViewModel.currentTab) > 0, 5000);

        AppViewModel.showPage("flash");
        tryVerify(() => shown("remoteFlashCard").length === 1, 5000);
        verify(!tab().remoteJoin.operationsAvailable);
        tryVerify(() => shown("remoteOpsUnavailable").length === 1, 5000);

        // Leaving the session keeps the tab and its console (the log can still be saved). The
        // ended state after a lost relay is tested in libs/remote (RemoteSession.*).
        const index = AppViewModel.currentTab;
        const lines = testSupport.consoleLastSeq(index);
        tab().connection.disconnectDevice();
        tryVerify(() => tab().state === DeviceTabViewModel.Disconnected, 5000);
        compare(AppViewModel.currentTab, index);
        verify(tab().transport === "remote");
        verify(testSupport.consoleLastSeq(index) >= lines);
    }

    // ADR-0363: support asks for the LTE update; the confirmation names the recommended firmware
    // and the cable moves (Deny is the default); Allow opens Flash › Update LTE firmware… with the
    // recommended firmware. nRF Util is missing in these tests, so the request ends with that
    // error, the wizard stays open for the customer, and sharing stays active.
    function test_lteUpdateRequestOpensTheGuidedUpdate() {
        connectConsole();
        const share = tab().remoteShare;
        tryVerify(() => share.available, 5000);
        share.startSharing();
        tryVerify(() => share.state === "sharing", 10000);
        verify(testSupport.remoteJoin(share.code));
        tryVerify(() => share.joinQuestionOpen, 10000);
        share.answerJoin(true);
        tryVerify(() => share.participantAllowed, 5000);

        testSupport.remoteRequest("lteUpdate", "lte");
        tryVerify(() => share.operationQuestionOpen, 10000);
        verify(share.operationQuestionText.indexOf("HARDWARIO LTE firmware 1.7.0") >= 0, share.operationQuestionText);
        verify(share.operationQuestionText.indexOf("move the J-Link cable") >= 0, share.operationQuestionText);
        const dialogs = findAll(window.contentItem, "remoteSessionDialogs").filter(effectivelyVisible)[0];
        const operation = findChild(dialogs, "remoteOperationDialog");
        tryVerify(() => operation.opened, 5000);
        tryVerify(() => findChild(operation.footer, "dialogReject").activeFocus, 5000);
        mouseClick(findChild(operation.footer, "dialogAccept")); // Allow
        tryVerify(() => AppViewModel.wizard.open, 10000);
        compare(AppViewModel.wizard.wizardId, "lte-update");
        const wizard = testSupport.openWizard();
        compare(wizard.lteSource.lteSource, FirmwareSourceViewModel.Designated);
        tryVerify(() => testSupport.remoteLastResult() === "dependency.nrfutil_not_found", 20000, testSupport.remoteLastResult());
        verify(AppViewModel.wizard.open);
        verify(share.sharing);
        AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open, 5000);
        // The modal dialog blocks the clicks of the next test until it has really closed.
        const wizardDialog = findChild(window, "wizardDialog");
        tryVerify(() => !wizardDialog || !wizardDialog.visible, 5000);
        share.endSharing();
        tryVerify(() => !share.sharing, 5000);
    }

    // ADR-0363: the joiner's Flash page offers the LTE update (no ID field) with the LTE core.
    function test_joinerAsksForAnLteUpdate() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(true);
        tryVerify(() => tab().remoteJoin.operationsAvailable, 10000);
        verify(tab().remoteJoin.lteUpdateAvailable);
        AppViewModel.showPage("flash");
        tryVerify(() => shown("remoteFlashCard").length === 1, 5000);
        verify(shown("remoteFirmwareId").length === 1);
        verify(shown("remoteLteUpdateButton").length === 0);
        const lte = one("remoteTargetLte");
        lte.click(); // the card may be below the visible part of the page
        tryVerify(() => lte.checked, 5000);
        tryVerify(() => shown("remoteFirmwareId").length === 0, 5000);
        const button = one("remoteLteUpdateButton");
        compare(button.text, "Ask to update LTE firmware");
        verify(button.enabled);
        button.clicked();
        tryVerify(() => testSupport.remoteRequestedKind() === "lteUpdate", 10000);
        verify(tab().remoteJoin.operationPending);
    }

    // ADR-0364: a remote tab completes from the Monitor host's command tree and asks the host for
    // a command's subcommands (tunnel `learn`), without any query of its own.
    function test_remoteTabCompletesFromTheHostsCommands() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(false);
        tryVerify(() => tab().remoteJoin.state === "joined", 10000);
        testSupport.remoteHostCommands(["app", "lte", "info"]);
        AppViewModel.showPage("console");
        const field = one("commandInput");
        const input = one("shellInput");
        tryVerify(() => field.enabled, 10000);
        field.forceActiveFocus();
        field.text = "";
        keyClick(Qt.Key_A);
        const list = input.suggestionList;
        tryVerify(() => list.opened, 10000);
        compare(list.rows[0].label, "app");
        keyClick(Qt.Key_Tab);
        compare(field.text, "app ");
        tryVerify(() => testSupport.remoteLearnRequest() === "app", 10000);
        verify(testSupport.remoteRequestedKind() === ""); // no operation, no shell query
    }

    function test_shareNotOfferedInARemoteTab() {
        verify(AppViewModel.joinRemoteSession("482913", false));
        tryVerify(() => tab().transport === "remote", 5000);
        AppViewModel.showPage("console");
        tryVerify(() => findAll(window.contentItem, "shareButton").filter(effectivelyVisible).length === 0, 5000);
        verify(!tab().remoteShare.available);
    }
}
