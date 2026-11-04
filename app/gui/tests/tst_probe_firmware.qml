// J-Link firmware update (plan 0036; ADR-0132, ADR-0246): the banner of an outdated probe with
// [Disconnect and update…] and [Not now], the disabled button with its reason while the tab is
// busy, the dialog keyboard-only (focus on Cancel, Esc = Cancel, no Esc while updating), both
// themes, and the layout at 960 x 600 in German and Czech (geometry assertions). Fake J-Link.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null
    readonly property string scenario: '{"probes": [{"serialNumber": 801000001, "product": "Test Probe",'
        + ' "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",'
        + ' "libraryFirmware": "J-Link V11 compiled Oct  1 2024 09:00:00", "updateDelayMs": 600,'
        + ' "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\\n"}]}}]}'

    name: "ProbeFirmware"
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

    // In the window or in the update dialog (a popup).
    function find(name) {
        const dialog = findChild(window, "probeFirmwareDialog");
        const roots = [window.contentItem, dialog.contentItem.parent]; // the popup item
        for (const root of roots) {
            const items = findAll(root, name).filter(effectivelyVisible);
            if (items.length > 0)
                return items[0];
        }
        return null;
    }

    function rect(item) {
        const point = item.mapToItem(window.contentItem, 0, 0);
        return { x: point.x, y: point.y, w: item.width, h: item.height, name: item.objectName };
    }

    function checkInside(item) {
        const r = rect(item);
        verify(r.x >= -0.5 && r.x + r.w <= window.width + 0.5, `${r.name} is clipped: x=${r.x} w=${r.w}`);
        verify(r.y >= -0.5 && r.y + r.h <= window.height + 0.5, `${r.name} is clipped: y=${r.y} h=${r.h}`);
        if (item.implicitWidth !== undefined && item.text !== undefined)
            verify(item.width >= item.implicitWidth - 0.5, `${r.name} is narrower than its text`);
    }

    function checkNoOverlap(a, b) {
        const ra = rect(a);
        const rb = rect(b);
        const overlap = ra.x < rb.x + rb.w - 0.5 && rb.x < ra.x + ra.w - 0.5
                      && ra.y < rb.y + rb.h - 0.5 && rb.y < ra.y + ra.h - 0.5;
        verify(!overlap, `${ra.name} overlaps ${rb.name}`);
    }

    function tab() {
        return AppViewModel.currentTabViewModel;
    }

    function connectWithBanner() {
        verify(testSupport.useScenario(scenario));
        if (tab().state === DeviceTabViewModel.ChoosingDevice)
            verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        tab().connection.connectDevice();
        tryVerify(() => find("probeFirmwareBanner") !== null, 10000);
    }

    function waitIdle() {
        tryVerify(() => AppViewModel.probeFirmware.state === ProbeFirmwareViewModel.Idle
                  && !find("probeFirmwareTitle"), 10000);
    }

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
        window.requestActivate();
        tryVerify(() => window.active);
    }

    function cleanupTestCase() {
        tab().connection.disconnectDevice();
        window.destroy();
        wait(50);
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "light";
        window.width = 1280;
        window.height = 800;
    }

    function cleanup() {
        if (AppViewModel.probeFirmware.state !== ProbeFirmwareViewModel.Updating)
            AppViewModel.probeFirmware.cancel();
        waitIdle();
    }

    function test_1_banner_and_busy_tooltip() {
        connectWithBanner();
        const text = find("probeFirmwareText");
        compare(text.text, "The J-Link firmware is outdated (2023-09-21, available: 2024-10-01).");
        verify(find("connectionWarning") === null); // not the plain warning banner
        const button = find("probeFirmwareUpdate");
        verify(button.enabled);
        // A busy page: disabled with the reason.
        testSupport.testPage(0).startWork();
        tryVerify(() => !button.enabled);
        compare(tab().connection.firmwareBlockedReason, "Wait for the current operation to finish.");
        compare(button.Accessible.description, "Wait for the current operation to finish.");
        testSupport.finishWork(0);
        tryVerify(() => button.enabled);
    }

    function test_2_dialog_keyboard_esc_cancels() {
        if (!find("probeFirmwareBanner"))
            connectWithBanner();
        mouseClick(find("probeFirmwareUpdate"));
        tryVerify(() => find("probeFirmwareTitle") !== null);
        verify(tab().state !== DeviceTabViewModel.Connected); // disconnected first (ADR-0246)
        tryVerify(() => AppViewModel.probeFirmware.state === ProbeFirmwareViewModel.Confirm, 10000);
        compare(find("probeFirmwareCurrent").text, "J-Link V11 compiled Sep 21 2023 10:38:43");
        compare(find("probeFirmwareNew").text, "J-Link V11 compiled Oct  1 2024 09:00:00");
        compare(find("probeFirmwareProbe").text, "Test Probe · SN 801000001");
        // No default button: the focus starts on Cancel.
        tryVerify(() => find("probeFirmwareCancel").activeFocus);
        keyClick(Qt.Key_Escape);
        waitIdle();
        compare(tab().state, DeviceTabViewModel.Disconnected);
        verify(find("probeFirmwareBanner") === null);
    }

    function test_3_update_keyboard_only_and_no_esc() {
        AppViewModel.theme = "dark";
        connectWithBanner();
        mouseClick(find("probeFirmwareUpdate"));
        tryVerify(() => AppViewModel.probeFirmware.state === ProbeFirmwareViewModel.Confirm, 10000);
        tryVerify(() => find("probeFirmwareCancel").activeFocus);
        keyClick(Qt.Key_Tab);
        tryVerify(() => find("probeFirmwareConfirm").activeFocus);
        keyClick(Qt.Key_Space);
        tryVerify(() => AppViewModel.probeFirmware.state === ProbeFirmwareViewModel.Updating);
        verify(find("probeFirmwareUnplugWarning") !== null);
        verify(find("probeFirmwareCancel") === null); // no Cancel while updating
        keyClick(Qt.Key_Escape); // ignored
        compare(AppViewModel.probeFirmware.state, ProbeFirmwareViewModel.Updating);
        verify(find("probeFirmwareTitle") !== null);
        waitIdle();
        // ADR-0256: the tab connects again.
        tryVerify(() => tab().state === DeviceTabViewModel.Connected
                  || tab().state === DeviceTabViewModel.Connecting, 10000);
    }

    function test_4_layout_960x600_de_cs() {
        for (const language of ["de", "cs"]) {
            AppViewModel.setLanguage(language);
            window.width = 960;
            window.height = 600;
            if (!find("probeFirmwareBanner"))
                connectWithBanner();
            const update = find("probeFirmwareUpdate");
            const banner = find("probeFirmwareBanner");
            tryVerify(() => rect(banner).x + banner.width <= window.width + 0.5);
            const notNow = find("probeFirmwareNotNow");
            checkInside(update);
            checkInside(notNow);
            checkNoOverlap(update, notNow);
            checkNoOverlap(find("probeFirmwareText"), update);

            mouseClick(update);
            tryVerify(() => AppViewModel.probeFirmware.state === ProbeFirmwareViewModel.Confirm, 10000);
            const cancel = find("probeFirmwareCancel");
            const confirm = find("probeFirmwareConfirm");
            tryVerify(() => cancel.width > 0 && confirm.width > 0);
            checkInside(find("probeFirmwareTitle"));
            checkInside(cancel);
            checkInside(confirm);
            checkNoOverlap(cancel, confirm);
            checkInside(find("probeFirmwareUnplugWarning"));
            AppViewModel.probeFirmware.cancel();
            waitIdle();
        }
    }

    function test_5_not_now() {
        if (!find("probeFirmwareBanner"))
            connectWithBanner();
        mouseClick(find("probeFirmwareNotNow"));
        tryVerify(() => find("probeFirmwareBanner") === null);
        tab().connection.disconnectDevice();
        tab().connection.connectDevice();
        tryVerify(() => tab().state === DeviceTabViewModel.Connected, 10000);
        wait(200);
        verify(find("probeFirmwareBanner") === null);
    }
}
