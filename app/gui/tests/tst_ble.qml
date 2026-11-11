// Plan 0012: Bluetooth in the shell against the fake BLE API (tests/ble_scenarios/chester.json):
// the Connect via switch, the Bluetooth list, keyboard-only connect, the Bluetooth tab (device chip,
// no target picker, tab icon, Flash unavailable, no Reset, shell only), the pairing ErrorPanel and
// the Bluetooth view at 960 x 600 in Czech.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    // A click that is never merged with the previous one into a double click.
    function click(item) {
        mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton, Qt.NoModifier,
                   Qt.styleHints.mouseDoubleClickInterval + 50);
    }

    function findAll(parent, name) {
        let result = [];
        if (!parent)
            return result;
        const children = parent.children ? parent.children : [];
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

    function visibleAll(name) {
        return findAll(window.contentItem, name).filter(effectivelyVisible);
    }

    function visibleOne(name) {
        return visibleAll(name)[0];
    }

    function tab() {
        return AppViewModel.currentTabViewModel;
    }

    function showBluetooth() {
        const button = visibleOne("transportBluetooth");
        verify(button !== undefined, "the Connect via switch is shown");
        click(button);
        tryCompare(tab(), "transport", "ble");
        tryVerify(() => visibleAll("bleRow").length === 2, 15000, "two CHESTERs are listed");
    }

    function insideWindow(item) {
        const point = item.mapToItem(window.contentItem, 0, 0);
        return point.x >= 0 && point.y >= 0 && point.x + item.width <= window.width + 0.5
            && point.y + item.height <= window.height + 0.5;
    }

    function init() {
        AppViewModel.setLanguage("en");
        testSupport.resetTabs();
        tab().transport = "jlink"; // the remembered last transport of an earlier test
        window.width = 1280;
        window.height = 800;
        waitForRendering(window.contentItem);
    }

    function initTestCase() {
        const component = Qt.createComponent("HardwarioMonitor", "Main");
        verify(component.status === Component.Ready, component.errorString());
        window = component.createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        testSupport.resetTabs();
        tab().transport = "jlink";
        window.destroy();
        wait(50);
    }

    function test_transportSwitch() {
        verify(visibleOne("jlinkView") !== undefined);
        compare(visibleAll("bleDeviceList").length, 0);
        showBluetooth();
        compare(visibleAll("jlinkView").length, 0);
        const names = visibleAll("bleName").map(label => label.text);
        compare(names, ["CHESTER-ABC123", "CHESTER-XYZ789"]);
        compare(visibleAll("bleRssi")[0].text, "-58 dBm");
        // A new tab opens in the last transport (ADR-0081).
        AppViewModel.newTab();
        tryCompare(tab(), "transport", "ble");
        click(visibleOne("transportJLink"));
        tryCompare(tab(), "transport", "jlink");
        verify(visibleOne("jlinkView") !== undefined);
    }

    function test_keyboardOnlyConnect() {
        showBluetooth();
        const filter = visibleOne("bleFilter");
        filter.forceActiveFocus();
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_B);
        keyClick(Qt.Key_C);
        tryCompare(tab().blePicker, "count", 1);
        tryCompare(tab().blePicker, "selectedRow", 0); // exactly one match: preselected
        keyClick(Qt.Key_Return);
        tryCompare(tab(), "state", DeviceTabViewModel.Connected, 10000);
        compare(tab().title, "CHESTER · CHESTER-ABC123");
    }

    // The arrow keys move the selection between the rows; Down in the filter enters the list and
    // Up from the first row returns to the filter.
    function test_arrowKeys() {
        showBluetooth();
        const picker = tab().blePicker;
        const filter = visibleOne("bleFilter");
        const rows = () => visibleAll("bleRow");
        filter.forceActiveFocus();
        picker.selectedRow = -1;
        keyClick(Qt.Key_Down);
        compare(picker.selectedRow, 0);
        verify(rows()[0].activeFocus, "Down in the filter focuses the first row");
        keyClick(Qt.Key_Down);
        compare(picker.selectedRow, 1);
        verify(rows()[1].activeFocus);
        keyClick(Qt.Key_Down); // the last row: stays
        compare(picker.selectedRow, 1);
        verify(rows()[1].activeFocus);
        keyClick(Qt.Key_Up);
        compare(picker.selectedRow, 0);
        verify(rows()[0].activeFocus);
        keyClick(Qt.Key_Up); // back to the filter, the selection is kept
        verify(filter.activeFocus, "Up from the first row returns to the filter");
        compare(picker.selectedRow, 0);
        keyClick(Qt.Key_Down); // enters at the selected row
        verify(rows()[0].activeFocus);
        keyClick(Qt.Key_Down);
        const second = rows()[1].name;
        keyClick(Qt.Key_Return);
        tryCompare(tab(), "state", DeviceTabViewModel.Connected, 10000);
        compare(tab().title, "CHESTER · " + second);
    }

    // Plan 0033 (ADR-0235): the order is frozen while the pointer is over the list or a row has
    // the keyboard focus.
    function test_orderFrozenWhilePointedAt() {
        showBluetooth();
        const picker = tab().blePicker;
        const row = visibleAll("bleRow")[0];
        mouseMove(row, row.width / 2, row.height / 2);
        tryVerify(() => picker.freezeOrder);
        mouseMove(window.contentItem, 2, 2);
        tryVerify(() => !picker.freezeOrder);
        row.forceActiveFocus();
        tryVerify(() => picker.freezeOrder);
        visibleOne("bleFilter").forceActiveFocus();
        tryVerify(() => !picker.freezeOrder);
    }

    function test_bluetoothTab() {
        showBluetooth();
        click(visibleAll("bleRow")[0]);
        click(visibleOne("bleConnectButton"));
        tryCompare(tab(), "state", DeviceTabViewModel.Connected, 10000);
        waitForRendering(window.contentItem);

        // Device bar: the device chip, no target picker.
        verify(visibleOne("probeChip") !== undefined);
        verify(visibleOne("probeLabel").text.indexOf("CHESTER-ABC123") === 0);
        compare(visibleAll("targetBox").length, 0);
        verify(visibleOne("tabBluetooth") !== undefined);

        // Rail: Flash is unavailable with the reason; a click keeps the console.
        const flash = visibleAll("railItem").filter(item => item.text === "Flash")[0];
        verify(flash !== undefined);
        verify(flash.unavailableReason.indexOf("J-Link") >= 0);
        click(flash);
        compare(tab().currentPage, "console");

        // Console: no Reset, the shell only (no log lines yet).
        compare(visibleAll("resetButton").length, 0);
        compare(visibleAll("logPane").length, 0);
        compare(visibleAll("splitHandle").length, 0);
        verify(visibleOne("shellPane") !== undefined);
    }

    function test_pairingErrorPanel() {
        verify(testSupport.configureBleTab(0, "F1:22:33:44:55:66", "CHESTER-ABC123"));
        testSupport.setConnectionError(0, "ble.not_paired");
        waitForRendering(window.contentItem);
        const panel = visibleOne("connectionError");
        verify(panel !== undefined);
        const primary = findAll(panel, "errorPrimary")[0];
        if (Qt.platform.os === "osx")
            compare(primary.text, "Reconnect");
        else
            compare(primary.text, "Pair");
        const note = findAll(panel, "errorNote")[0];
        verify(effectivelyVisible(note));
        verify(note.text.indexOf("123456") >= 0);
        testSupport.setConnectionError(0, "ble.device_not_found");
        waitForRendering(window.contentItem);
        compare(findAll(visibleOne("connectionError"), "errorSecondary")[0].text, "Choose another device");
    }

    function test_czechAt960x600() {
        AppViewModel.setLanguage("cs");
        window.width = 960;
        window.height = 600;
        showBluetooth();
        waitForRendering(window.contentItem);
        for (const name of ["transportSwitch", "bleFilter", "bleShowAll", "bleProfileBox", "bleConnectButton"]) {
            const item = visibleOne(name);
            verify(item !== undefined, name);
            verify(insideWindow(item), name + " is inside the window");
        }
        for (const row of visibleAll("bleRow"))
            verify(row.width <= window.width, "the rows fit");
    }
}
