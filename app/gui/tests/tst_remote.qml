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
        // Plan 0052 (ADR-0387): a view of the page area; Cancel has the focus.
        tryVerify(() => shown("remoteShareDialog").length === 1, 5000);
        const shareDialog = one("remoteShareDialog");
        tryVerify(() => findChild(shareDialog, "remoteShareCancel").activeFocus, 5000);
        verify(findChild(shareDialog, "remoteShareBody").text !== "");
        mouseClick(findChild(shareDialog, "remoteShareStart")); // Start sharing
        tryVerify(() => shown("remoteShareDialog").length === 0, 5000);
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

    // Plan 0061 Amendment 2 (ADR-0439): support leaves (tunnel `leave`): the customer's banner no
    // longer says that support can send commands, the console has the audit line, a line after it
    // does not reach the device, and the next join asks again.
    function test_customerSeesThatSupportLeft() {
        connectConsole();
        const share = tab().remoteShare;
        const index = AppViewModel.currentTab;
        tryVerify(() => share.available, 5000);
        mouseClick(one("shareButton"));
        tryVerify(() => shown("remoteShareDialog").length === 1, 5000);
        mouseClick(findChild(one("remoteShareDialog"), "remoteShareStart"));
        tryVerify(() => share.state === "sharing", 10000);
        const code = share.code;

        verify(testSupport.remoteJoin(code));
        tryVerify(() => share.joinQuestionOpen, 10000);
        const dialogs = findAll(window.contentItem, "remoteSessionDialogs").filter(effectivelyVisible)[0];
        const join = findChild(dialogs, "remoteJoinDialog");
        tryVerify(() => join.opened, 5000);
        mouseClick(findChild(join.footer, "dialogAccept")); // Allow
        tryVerify(() => share.participantAllowed, 5000);
        tryCompare(one("remoteShareText"), "text",
                   "Shared with support — code " + code + ". Support can send commands.", 5000);

        testSupport.remoteLeave();
        tryVerify(() => !share.participantAllowed, 5000);
        tryCompare(one("remoteShareText"), "text", "Shared with support — code " + code, 5000);
        tryVerify(() => testSupport.consoleHasLine(index, "— Remote: Support left the session. —"), 5000);
        verify(share.sharing);
        verify(shown("remoteShareBanner").length === 1);

        // Join again: the customer is asked again.
        verify(testSupport.remoteJoin(code));
        tryVerify(() => share.joinQuestionOpen, 10000);
        tryVerify(() => join.opened, 5000);
        mouseClick(findChild(join.footer, "dialogAccept"));
        tryVerify(() => share.participantAllowed, 5000);
        tryVerify(() => !join.visible, 5000); // the modal dialog has closed

        mouseClick(one("remoteEndSharing"));
        tryVerify(() => !share.sharing, 5000);
    }

    function test_joinAWebTerminalHost() {
        AppViewModel.showPage("console");
        mouseClick(one("joinSupportSession"));
        // Plan 0052 (ADR-0387): a view of the page area; the code field has the focus.
        tryVerify(() => shown("remoteJoinInputDialog").length === 1, 5000);
        const input = one("remoteJoinInputDialog");
        const field = findChild(input, "remoteCodeField");
        const joinButton = findChild(input, "remoteJoinButton");
        tryVerify(() => field.activeFocus, 5000);
        field.text = "12a";
        tryVerify(() => findChild(input, "remoteCodeError").visible, 5000);
        verify(!joinButton.enabled);
        field.text = "12 345";
        tryVerify(() => joinButton.enabled, 5000);
        // The web Terminal host is in the session before the joiner.
        verify(testSupport.remoteJoin("12345"));
        mouseClick(joinButton);
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        compare(AppViewModel.overlayView, "");
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        tryVerify(() => shown("remoteJoinBanner").length === 1, 5000);

        testSupport.remoteHostSync("hello from the web host");
        tryVerify(() => tab().remoteJoin.state === "joined", 10000);
        compare(tab().title, "Session 12345 — web Terminal or mobile app");
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
        tryVerify(() => AppViewModel.currentTabViewModel.wizard.open, 10000);
        compare(AppViewModel.currentTabViewModel.wizard.wizardId, "lte-update");
        const wizard = testSupport.openWizard();
        compare(wizard.lteSource.lteSource, FirmwareSourceViewModel.Designated);
        tryVerify(() => testSupport.remoteLastResult() === "dependency.nrfutil_not_found", 20000, testSupport.remoteLastResult());
        verify(AppViewModel.currentTabViewModel.wizard.open);
        verify(share.sharing);
        AppViewModel.currentTabViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.currentTabViewModel.wizard.open, 5000);
        tryVerify(() => findAll(window.contentItem, "wizardView").filter(effectivelyVisible).length === 0, 5000);
        share.endSharing();
        tryVerify(() => !share.sharing, 5000);
    }

    // Plan 0056 (U-2, D-7, ADR-0393): on a joined tab the troubleshooting and guided flash wizards
    // refuse with a toast (the Help menu, "Help me fix this" and Flash "Guide me" all go through the
    // launcher); nothing opens and the session goes on. The LTE guide's entry points (the
    // other-connector banner, the local Flash page's LTE section) are not shown on a joined tab.
    function test_joinerRefusesTheLocalDeviceWizards() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        const text = "This wizard works only with a device connected to this computer, not in a support session.";
        for (const id of ["troubleshooting", "flash-firmware"]) {
            verify(!AppViewModel.launcher.canStart(id, -1), id);
            verify(!AppViewModel.launcher.start(id, -1), id);
            verify(!tab().wizard.open, id);
        }
        verify(!AppViewModel.launcher.open("troubleshooting", testSupport.defaultError("target.connect_failed"), -1));
        verify(AppViewModel.toasts.indexOf(text) >= 0);
        verify(!tab().wizard.open);
        compare(tab().transport, "remote");
        compare(tab().remoteJoin.state, "waiting"); // the session goes on
        AppViewModel.showPage("flash");
        wait(200);
        compare(shown("guideMe").length, 0);
        compare(shown("updateLteButton").length, 0);
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

    // ADR-0368: the customer's Monitor asks support (Continue before the erase, Continue without a
    // copy) on the joiner's Flash card; the answer goes back as op-answer and the question closes.
    function test_joinerAnswersTheLteUpdateQuestions() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(true);
        tryVerify(() => tab().remoteJoin.lteUpdateAvailable, 10000);
        AppViewModel.showPage("flash");
        tryVerify(() => shown("remoteFlashCard").length === 1, 5000);
        tab().remoteJoin.requestOperation("lteUpdate", "lte", "");
        tryVerify(() => testSupport.remoteRequestedKind() === "lteUpdate", 10000);
        testSupport.remoteHostAccept();
        verify(shown("remoteQuestion").length === 0);

        testSupport.remoteHostQuestion("continue");
        tryVerify(() => shown("remoteQuestion").length === 1, 5000);
        verify(one("remoteQuestionText").text.indexOf("will be erased") >= 0, one("remoteQuestionText").text);
        compare(one("remoteQuestionYes").text, "Continue");
        one("remoteQuestionYes").clicked();
        tryVerify(() => testSupport.remoteLastAnswer() === "yes", 5000);
        tryVerify(() => shown("remoteQuestion").length === 0, 5000);

        testSupport.remoteHostQuestion("continueWithoutCopy");
        tryVerify(() => shown("remoteQuestion").length === 1, 5000);
        compare(one("remoteQuestionText").text, "The application could not be copied. Continue without a copy?");
        one("remoteQuestionNo").clicked();
        tryVerify(() => testSupport.remoteLastAnswer() === "no", 5000);
        tryVerify(() => shown("remoteQuestion").length === 0, 5000);

        // Confirmed 2026-11-12: support's own No is a cancellation, not an error.
        testSupport.remoteHostResult("cancelled");
        tryVerify(() => !tab().remoteJoin.operationPending, 5000);
        verify(/^LTE firmware update cancelled by you \(\d\d:\d\d\)\. Nothing was changed\.$/.test(tab().remoteJoin.operationStatus), tab().remoteJoin.operationStatus);
        verify(!tab().remoteJoin.operationFailed);
        verify(!tab().remoteJoin.resultDismissable);
    }

    // ADR-0369: the result names the operation and the local time and is also a console line; a
    // success clears when support leaves the Flash page, an error stays until ×.
    function test_joinerSeesTheOperationResult() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(true);
        tryVerify(() => tab().remoteJoin.operationsAvailable, 10000);
        AppViewModel.showPage("flash");
        tryVerify(() => shown("remoteFlashCard").length === 1, 5000);
        const join = tab().remoteJoin;

        const results = [
            ["reset", "app", /^Customer's device reset \(\d\d:\d\d\)\.$/],
            ["erase", "lte", /^LTE MCU erased on the customer's device \(\d\d:\d\d\)\.$/],
            ["lteUpdate", "lte", /^LTE firmware updated, application written back \(\d\d:\d\d\)\.$/],
        ];
        for (let i = 0; i < results.length; ++i) {
            join.requestOperation(results[i][0], results[i][1], "");
            tryVerify(() => testSupport.remoteRequestedKind() === results[i][0], 10000);
            testSupport.remoteHostAccept();
            testSupport.remoteHostResult("");
            tryVerify(() => !join.operationPending, 5000);
            verify(results[i][2].test(join.operationStatus), join.operationStatus);
            verify(!join.operationFailed);
            verify(!join.resultDismissable);
            verify(testSupport.consoleHasLine(AppViewModel.currentTab, join.operationStatus));
        }
        compare(one("remoteOperationStatus").text, join.operationStatus);
        // A success clears when support leaves the Flash page.
        AppViewModel.showPage("console");
        tryVerify(() => join.operationStatus === "", 5000);
        verify(testSupport.consoleHasLine(AppViewModel.currentTab, "LTE firmware updated, application written back"));

        // An error stays (also on other pages) until ×.
        AppViewModel.showPage("flash");
        join.requestOperation("reset", "app", "");
        tryVerify(() => testSupport.remoteRequestedKind() === "reset", 10000);
        testSupport.remoteHostAccept();
        testSupport.remoteHostResult("target.connect_failed");
        tryVerify(() => join.operationFailed, 5000);
        verify(/^Resetting the customer's device failed \(\d\d:\d\d\): /.test(join.operationStatus), join.operationStatus);
        verify(testSupport.consoleHasLine(AppViewModel.currentTab, "Resetting the customer's device failed"));
        AppViewModel.showPage("console");
        AppViewModel.showPage("flash");
        verify(join.operationFailed);
        tryVerify(() => shown("remoteResultDismiss").length === 1, 5000);
        one("remoteResultDismiss").clicked();
        tryVerify(() => join.operationStatus === "", 5000);
        verify(shown("remoteOperationStatus").length === 0);
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

    // Plan 0046 A5 (ADR-0171): hexdump continuation lines from the host are log lines of level
    // Hexdump, hidden by the default chips as in a local tab.
    function test_remoteHexdumpLinesAreHiddenByDefault() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(false);
        tryVerify(() => tab().remoteJoin.state === "joined", 10000);
        AppViewModel.showPage("console");
        const log = testSupport.consoleViewModel(AppViewModel.currentTab).log;
        const before = log.totalLines;
        const visible = log.visibleLines;
        testSupport.remoteHostRead("[00:00:12.345,000] <inf> app_lrw: Received:");
        testSupport.remoteHostRead("                                 44 74 4f 34 78 54 73 31  4b 36 69 41 73 41 53 4a |DtO4xTs1 K6iAsASJ");
        testSupport.remoteHostRead("                                 00 01 02 0a                                      |....            ");
        tryVerify(() => log.totalLines === before + 3, 10000, log.totalLines);
        // The filtered count follows the appended lines asynchronously on some platforms.
        tryCompare(log, "visibleLines", visible + 1, 10000); // only the header
    }

    function bar(name) {
        return findAll(window.contentItem, name).filter(effectivelyVisible)[0];
    }

    // Plan 0061 (ADR-0429): the device bar of a joined remote tab offers Leave session; leaving
    // keeps the tab and its console with a marker and the banner text; Join again opens a session
    // with the same code (the host sees a new client); Close tab is the banner's action.
    function test_leaveSessionAndJoinAgain() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        AppViewModel.showPage("console");
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteHostSync("hello from the web host");
        tryVerify(() => tab().remoteJoin.state === "joined", 10000);
        const index = AppViewModel.currentTab;
        tryVerify(() => bar("connectionAction") !== undefined, 5000);
        const button = bar("connectionAction");
        tryVerify(() => button.width > 0 && button.enabled, 5000);
        compare(button.text, "Leave session");
        compare(tab().connection.action, "leave");
        const clients = testSupport.remoteReceived("newClient");

        mouseClick(button);
        tryVerify(() => tab().remoteJoin.state === "ended", 5000);
        compare(AppViewModel.currentTab, index);
        verify(tab().transport === "remote");
        compare(tab().remoteJoin.bannerText, "You left the session. The console stays here so that you can save it.");
        tryVerify(() => shown("remoteJoinBanner").length === 1, 5000);
        verify(testSupport.consoleHasLine(index, "— you left the session —"));
        verify(testSupport.consoleHasLine(index, "hello from the web host"));
        tryCompare(button, "text", "Join again", 5000);
        verify(button.enabled);
        compare(button.kind, "primary");

        mouseClick(button);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        tryVerify(() => testSupport.remoteReceived("newClient") > clients, 10000);
        compare(tab().remoteJoin.bannerText, "Waiting for the customer to allow you… The live output may already appear.");
        tryCompare(button, "text", "Leave session", 5000);

        // The shortcut (device.toggle) leaves as well.
        AppViewModel.toggleConnection();
        tryVerify(() => tab().remoteJoin.state === "ended", 5000);
        tryVerify(() => shown("remoteJoinBanner").length === 1, 5000);
        const banner = one("remoteJoinBanner");
        compare(banner.actionText, "Close tab");
    }

    // ADR-0439: Leave session and closing the tab tell a Monitor host that lists the `leave` cap.
    function test_leaveAndCloseTabTellTheHost() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        AppViewModel.showPage("console");
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(false);
        tryVerify(() => tab().remoteJoin.state === "joined" && tab().remoteJoin.operationsAvailable, 10000);
        tryVerify(() => testSupport.remoteReceived("write") >= 1, 5000); // the joiner's hello
        tryVerify(() => bar("connectionAction") !== undefined, 5000);
        const button = bar("connectionAction");
        tryVerify(() => button.width > 0 && button.enabled, 5000);
        compare(testSupport.remoteReceivedLeave(), 0);

        mouseClick(button); // Leave session
        tryVerify(() => tab().remoteJoin.state === "ended", 5000);
        tryVerify(() => testSupport.remoteReceivedLeave() === 1, 5000);

        mouseClick(button); // Join again
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(false);
        tryVerify(() => tab().remoteJoin.state === "joined" && tab().remoteJoin.operationsAvailable, 10000);
        AppViewModel.closeTab(AppViewModel.currentTab, true);
        tryVerify(() => testSupport.remoteReceivedLeave() === 2, 5000);
    }

    // ADR-0429: while an operation support started runs, the button (and the shortcut) waits.
    function test_leaveWaitsForARunningOperation() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        AppViewModel.showPage("console");
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(false);
        tryVerify(() => tab().remoteJoin.operationsAvailable, 10000);
        tryVerify(() => bar("connectionAction") !== undefined, 5000);
        const button = bar("connectionAction");
        tryVerify(() => button.width > 0 && button.enabled, 5000);

        tab().remoteJoin.requestOperation("reset", "app", "");
        tryVerify(() => testSupport.remoteRequestedKind() === "reset", 10000);
        tryVerify(() => !button.enabled, 5000);
        compare(tab().connection.actionBlockedReason, "Wait until the running operation ends.");
        AppViewModel.toggleConnection();
        tab().connection.triggerAction();
        wait(100);
        compare(tab().remoteJoin.state, "joined");

        testSupport.remoteHostAccept();
        testSupport.remoteHostResult("");
        tryVerify(() => !tab().remoteJoin.operationPending, 5000);
        tryVerify(() => button.enabled, 5000);
        compare(tab().connection.actionBlockedReason, "");
    }

    function test_shareNotOfferedInARemoteTab() {
        verify(AppViewModel.joinRemoteSession("482913", false));
        tryVerify(() => tab().transport === "remote", 5000);
        AppViewModel.showPage("console");
        tryVerify(() => findAll(window.contentItem, "shareButton").filter(effectivelyVisible).length === 0, 5000);
        verify(!tab().remoteShare.available);
    }

    // Plan 0062 (ADR-0436): support's banner names the AI client support allowed for the session;
    // Stop AI ends the grant.
    function test_aiClientBannerAndStopAi() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(false);
        tryVerify(() => tab().remoteJoin.state === "joined", 10000);
        AppViewModel.showPage("console");
        verify(shown("remoteAiBanner").length === 0);
        verify(testSupport.remoteAllowAi(AppViewModel.currentTab, "Claude Code"));
        tryVerify(() => shown("remoteAiBanner").length === 1, 5000);
        compare(one("remoteAiText").text, "AI client active: Claude Code");
        compare(one("remoteStopAi").text, "Stop AI");
        tryVerify(() => one("remoteStopAi").width > 0 && one("remoteStopAi").height > 0, 5000);
        waitForRendering(window.contentItem);
        mouseClick(one("remoteStopAi"));
        tryVerify(() => shown("remoteAiBanner").length === 0, 5000);
        compare(tab().remoteJoin.aiClientText, "");
    }

    // Plan 0062 (D12, B3): an AI client's operation shows "AI: …" on the Flash card with Stop
    // waiting; after it the card keeps following the operation and Leave session still waits.
    function test_aiOperationStopWaiting() {
        verify(testSupport.remoteJoin("12345"));
        verify(AppViewModel.joinRemoteSession("12345", false));
        tryVerify(() => tab() !== null && tab().transport === "remote", 5000);
        tryVerify(() => tab().remoteJoin.state === "waiting", 10000);
        testSupport.remoteMonitorHost(false);
        tryVerify(() => tab().remoteJoin.operationsAvailable, 10000);
        AppViewModel.showPage("flash");
        tryVerify(() => shown("remoteFlashCard").length === 1, 5000);
        const join = tab().remoteJoin;
        verify(shown("remoteStopWaiting").length === 0);

        verify(testSupport.remoteAiReset(AppViewModel.currentTab));
        tryVerify(() => testSupport.remoteRequestedKind() === "reset", 10000);
        tryVerify(() => shown("remoteStopWaiting").length === 1, 5000);
        verify(join.aiWaiting);
        verify(join.operationStatus.indexOf("AI: Resetting…") === 0, join.operationStatus);
        compare(one("remoteOperationStatus").text, join.operationStatus);
        verify(!one("remoteResetButton").enabled); // one operation at a time
        compare(one("remoteStopWaiting").text, "Stop waiting");

        mouseClick(one("remoteStopWaiting"));
        tryVerify(() => testSupport.remoteAiOutcome() === "stopped", 5000);
        verify(!join.aiWaiting);
        verify(join.operationPending); // the tab follows the operation to its result
        verify(join.operationStatus.indexOf("AI:") < 0, join.operationStatus);
        compare(tab().connection.actionBlockedReason, "Wait until the running operation ends.");
        verify(!testSupport.remoteAiReset(AppViewModel.currentTab)); // internal.busy

        testSupport.remoteHostAccept();
        testSupport.remoteHostResult("");
        tryVerify(() => !join.operationPending, 5000);
        verify(/^Customer's device reset \(\d\d:\d\d\)\.$/.test(join.operationStatus), join.operationStatus);
        compare(testSupport.remoteAiOutcome(), "stopped"); // no result after Stop waiting
        compare(tab().connection.actionBlockedReason, "");
    }
}
