// Screenshots of Bluetooth in the shell (plan 0012, G7) in both themes and both languages, at
// 1280 x 800 and 960 x 600; only when HWIO_SCREENSHOT_DIR is set (QT_QUICK_BACKEND=software for the
// offscreen platform). The fake BLE API lists tests/ble_scenarios/chester.json.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function initTestCase() {
        if (testSupport.screenshotDir === "")
            skip("HWIO_SCREENSHOT_DIR is not set");
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        if (window)
            window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
        testSupport.resetTabs();
        AppViewModel.currentTabViewModel.transport = "jlink";
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function save(name) {
        waitForRendering(window.contentItem, 2000);
        wait(250);
        const image = grabImage(window.contentItem);
        verify(image.width > 0, name);
        const file = testSupport.screenshotDir + "/" + name + "-" + window.width + "x" + window.height + "-"
                     + AppViewModel.theme + "-" + AppViewModel.language + ".png";
        image.save(file); // throws on failure
    }

    function test_shots_data() {
        let rows = [];
        for (const size of [[1280, 800], [960, 600]]) {
            for (const theme of ["light", "dark"]) {
                for (const language of ["en", "cs"])
                    rows.push({ tag: size[0] + "-" + theme + "-" + language, width: size[0], height: size[1],
                                theme: theme, language: language });
            }
        }
        return rows;
    }

    function test_shots(data) {
        if (testSupport.screenshotDir === "")
            skip("HWIO_SCREENSHOT_DIR is not set");
        AppViewModel.theme = data.theme;
        AppViewModel.setLanguage(data.language);
        testSupport.resetTabs();
        window.width = data.width;
        window.height = data.height;
        const tab = AppViewModel.currentTabViewModel;
        tab.transport = "ble";
        tryVerify(() => tab.blePicker.count === 2, 15000);
        tab.blePicker.choose(0);
        save("ble-01-choose");

        tab.blePicker.connectSelected();
        tryCompare(tab, "state", DeviceTabViewModel.Connected, 10000);
        verify(tab.pageViewModel("console").send("version"));
        wait(300);
        save("ble-02-console");

        tab.connection.disconnectDevice();
        testSupport.setConnectionError(0, "ble.not_paired");
        save("ble-03-pairing-error");
    }
}
