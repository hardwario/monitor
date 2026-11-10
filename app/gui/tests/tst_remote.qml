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

    function test_shareNotOfferedInARemoteTab() {
        verify(AppViewModel.joinRemoteSession("482913", false));
        tryVerify(() => tab().transport === "remote", 5000);
        AppViewModel.showPage("console");
        tryVerify(() => findAll(window.contentItem, "shareButton").filter(effectivelyVisible).length === 0, 5000);
        verify(!tab().remoteShare.available);
    }
}
