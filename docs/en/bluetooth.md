# Bluetooth LE

[Documentation](index.md) › Bluetooth LE

CHESTER offers its command shell over Bluetooth LE (the Nordic UART Service). HARDWARIO Monitor reaches it in the
desktop app and from the command line with `hwio ble`; no J-Link or cable is needed.

The shell over Bluetooth works like the console over the J-Link: you send commands and read their answers, and log
messages the firmware sends appear as log lines.

## In the desktop app

1. Open a new tab (<kbd>Ctrl</kbd>+<kbd>T</kbd>) and switch **Connect via** to **Bluetooth**. The app remembers the
   choice for the next tab.
2. The list shows the CHESTER devices nearby, the strongest signal first, and refreshes by itself. The signal is
   averaged over a few seconds and a device moves up only when it is clearly stronger (6 dB), so the list stays
   steady; while the pointer is over the list (or a row has the keyboard focus) the order does not change at all and
   new devices are added at the end. Filter it by name, or tick **Show all devices** to see every Bluetooth device
   (CHESTER devices first). A device another tab uses is marked **In use (tab N)**;
   choosing it switches to that tab. A device another program uses is marked **In use by another program** and cannot
   be chosen.
3. Choose the device and press **Connect** (or double-click it, or press <kbd>Enter</kbd>).

The tab works like a J-Link tab: the console with suggestions, history, find, batch files and console files
([Console](console.md)). The log pane appears only when the device sends log messages. What differs:

- Flashing needs a J-Link: **Flash** is disabled in a Bluetooth tab. Open a J-Link tab to flash the device.
- **Auto-reconnect** is in the **⋯** menu of the device bar and is off by default; the app remembers it per device.
- The first connect to a device that is not paired yet shows **Waiting for pairing — enter the passkey in the system
  dialog**, and if needed **Pair** (Linux, Windows). See [Pairing](#pairing).
- If the device cannot be found, **Choose another device** goes back to the list.

Bluetooth tabs come back after a restart disconnected; press **Connect**. The app starts Bluetooth only when you first
open the Bluetooth view, so on macOS it asks for Bluetooth access only then.

## Find the device (command line)

```sh
hwio ble scan
```

The scan takes 5 seconds (`--timeout S`, 1 to 60) and lists the CHESTER devices nearby: those whose advertised name
contains `CHESTER`. `--all` lists every Bluetooth device.

Each device has an **ID**:

- Linux and Windows: its Bluetooth address, for example `F1:22:33:44:55:66`.
- macOS: a UUID that macOS assigns to the device. It is not the Bluetooth address and is valid only on that Mac.

## Connect (command line)

Choose the device by its ID (`-a`) or by a part of its name (`--name`, not case-sensitive; exactly one device found
by a 5-second scan must match):

```sh
hwio ble console -a F1:22:33:44:55:66      # interactive console
hwio ble console --name "CHESTER Clime"
hwio ble command -a F1:22:33:44:55:66 "info show"
```

A connection can take up to 30 seconds; failed attempts are retried within that time. `ble command` prints the answer
and ends at the shell prompt, or after `-t` seconds (3 by default).

In the interactive console <kbd>F4</kbd> reconnects, and <kbd>Ctrl</kbd>+<kbd>C</kbd> on an empty line or
<kbd>Ctrl</kbd>+<kbd>D</kbd> quits. With `--auto-reconnect` the console connects again after the link drops, while the
device is in range. The console writes a console file and keeps a command history, like the J-Link console
([Console](console.md#console-files)).

Only one program can be connected to a device. If another `hwio` is connected to it, you get
[ble.in_use_by_other_process](troubleshooting.md#ble.in_use_by_other_process).

<a id="pairing"></a>
## Pairing

CHESTER protects its Bluetooth shell with a passkey, so the computer has to pair with it (bond) once. The passkey is 123456 unless it was changed when
the device was configured. HARDWARIO Monitor never stores the passkey and never types it for you: the operating system
asks for it.

| System | How |
|---|---|
| Linux | On the first connect, with **Pair** in the app, or with `hwio ble pair -a ID`. The desktop's Bluetooth settings ask for the passkey. |
| Windows | On the first connect, with **Pair** in the app, or with `hwio ble pair -a ID`. Windows asks for the passkey. |
| macOS | On the first connect only; macOS asks for the passkey. `hwio ble pair` and **Pair** are not available on macOS. |

When the device waits for the passkey, the connection waits up to 60 seconds.

**Linux without a pairing agent.** Some desktops (GNOME) ask for the passkey only while their Bluetooth settings are
open. If nothing asks, you get [ble.no_pairing_agent](troubleshooting.md#ble.no_pairing_agent). Pair the device in the
system's Bluetooth settings, or with `bluetoothctl`:

```text
$ bluetoothctl
[bluetooth]# agent KeyboardOnly
[bluetooth]# default-agent
[bluetooth]# pair F1:22:33:44:55:66
```

If pairing fails after the passkey was changed on the device, remove the device in the system's Bluetooth settings and
pair it again ([ble.pairing_failed](troubleshooting.md#ble.pairing_failed)).

## Permissions and adapter

- **Linux:** Bluetooth uses BlueZ. If it is not running, start it with `sudo systemctl start bluetooth`.
- **macOS:** the first scan makes macOS ask for Bluetooth access: for HARDWARIO Monitor in the app, for the app you
  run `hwio` from (usually Terminal) on the command line. If you declined, allow it in **System Settings → Privacy &
  Security → Bluetooth**; in the app, the error offers **Open Privacy settings**.
- **All systems:** turn the Bluetooth adapter on. `hwio doctor` shows whether it is ready.

## JSON

With `--json`, `ble scan` prints the devices, and `ble console` prints one JSON line per console line, as the J-Link
console does ([Command line](cli.md#json-output)).
