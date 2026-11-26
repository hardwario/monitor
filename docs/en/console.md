# Console

[Documentation](index.md) › Console

The **Console** page (<kbd>Ctrl</kbd>+<kbd>1</kbd>) shows what the application MCU of the CHESTER sends over the
J-Link (SEGGER RTT), in two panes:

- **Shell**: the device's command shell. You type commands below it, and their answers appear here. Sent commands
  are shown as `> command`.
- **Log**: the device's log messages, with a level column: **D** debug, **I** info, **W** warning, **E** error, and
  **H** for hexdump lines.

The LTE MCU has no console; with the **lte** target the page says **This target has no console**.

Rows between the device lines mark what happened to the connection: **— connected —**, **— disconnected —**,
**— paused: … —** while a flash runs, **— resumed —**.

## Layout

Drag the handle between the panes to resize them (or focus it and use the arrow keys). Below 1000 pixels of window
width the panes are stacked; **More** in the toolbar has **Stacked layout** and **Swap panes**, as well as
**Show timestamps** and **Wrap lines**. <kbd>Ctrl</kbd>+<kbd>=</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> and
<kbd>Ctrl</kbd>+<kbd>0</kbd> change the font size.

A view follows new lines as they come. Scroll up and it stays where you are; a **▼ N new lines** button takes you
back, as does <kbd>End</kbd> or **Follow**. **Pause** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) freezes the view while lines
keep arriving; **Resume** shows them. Each pane keeps the last 100 000 lines.

## Toolbar

| Button | Does |
|---|---|
| **Reset** | Resets the device, or with **Reset and halt** resets it and keeps the CPU halted |
| **Reconnect** | Attaches to the console again (<kbd>F4</kbd>) |
| **Batch…** | Runs a [batch file](#batch-files) (<kbd>Ctrl</kbd>+<kbd>B</kbd>); ⓘ next to it opens the documentation |
| **Clear** | Clears the **Shell**, the **Log** or **Both** (<kbd>Ctrl</kbd>+<kbd>K</kbd> clears the focused pane) |
| **Save view…** | Saves the focused pane to a text file (<kbd>Ctrl</kbd>+<kbd>S</kbd>) |
| **Follow**, **Pause** | See above |
| **More** | **Save device information…** ([below](#save-device-information)), **Open log file**, **Open log folder** (the [console files](#console-files)) and the layout options |

## Sending commands

Click the input under the shell (or press <kbd>Ctrl</kbd>+<kbd>E</kbd>), type a command and press
<kbd>Enter</kbd>. When the input is disabled, its placeholder says why (**Not connected**, **Connecting…**, or an
operation such as a flash that is running).

- <kbd>↑</kbd> and <kbd>↓</kbd> walk through the command history.
- <kbd>Ctrl</kbd>+<kbd>R</kbd> searches the history: type part of a command, <kbd>Enter</kbd> puts it into the
  input.
- While you type, a list suggests the device's own commands (with their help text) and matching history entries.
  <kbd>↑</kbd>/<kbd>↓</kbd> choose, <kbd>Tab</kbd> or <kbd>→</kbd> accepts, <kbd>Esc</kbd> closes the list.
- Grey text after the cursor completes the line; <kbd>→</kbd> or <kbd>End</kbd> accepts it.
- <kbd>Tab</kbd> completes the command. It never leaves the input; use <kbd>Shift</kbd>+<kbd>Tab</kbd> to move on.

The device's commands are read with `help` when the console connects. Turn this off in **Settings → Console → Read
the command list from the device on connect (sends “help”)**, and the suggestions with **Suggest commands while
typing**.

### Command history

The history keeps the last 1000 commands per device type and target (for example CHESTER app). It is shared by all
tabs and by the command line (`hwio chester app console`). Commands that carry secrets (such as `lte config password`,
`lrw config appkey` or `ble config passkey`) are never saved, and neither is a command you start with a space.
**Settings → Console** turns saving off (**Remember commands**) and deletes the history (**Clear history**).

## Search and filter

The bar under the log has two modes:

- **Search** (<kbd>Ctrl</kbd>+<kbd>F</kbd>): highlights the matches and shows **3 / 17**. <kbd>Enter</kbd> or
  <kbd>F3</kbd> goes to the next match, <kbd>Shift</kbd>+<kbd>Enter</kbd> or <kbd>Shift</kbd>+<kbd>F3</kbd> to the
  previous one; in the log, <kbd>N</kbd> and <kbd>Shift</kbd>+<kbd>N</kbd> work too.
- **Filter** (<kbd>Ctrl</kbd>+<kbd>L</kbd>): shows only the matching lines (**Showing 40 of 2 000 lines**).

**Aa** matches case, **.\*** treats the text as a regular expression. The level buttons **D**, **I**, **W**, **E**
and **H** show or hide the levels; hexdump lines (**H**) are hidden until you turn them on. <kbd>Esc</kbd> closes the
search, a second <kbd>Esc</kbd> clears the filter.

The shell has a search of its own: press <kbd>Ctrl</kbd>+<kbd>F</kbd> while the shell has the focus.

## Selecting and copying

Click a line to select it, <kbd>Shift</kbd>+click or drag to select more, <kbd>Ctrl</kbd>+<kbd>A</kbd> selects all.
<kbd>Ctrl</kbd>+<kbd>C</kbd> or the right-click menu copies the lines ("Copied to clipboard."); at most 100 000 lines
are copied at once.

## Save view

**Save view…** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) saves the focused pane as it is shown, with timestamps if they are
on. The name is prefilled, for example `chester-app-clime-v3.2.0-20261103-101500-log.txt`, and the folder is the one
you used last (Documents the first time). Secrets are written as `<redacted>`, as in the
[console files](#console-files) (for example `claim token: <redacted>`); the screen and **Copy** still show them. On
Linux and macOS only your user can read the saved file.

<a id="save-device-information"></a>
## Save device information

**More → Save device information…** writes what the device reports about itself into one text file named by its
serial number, for example `2159018267_2026-11-25_141502.txt`. It works in a J-Link or Bluetooth tab while the
device is connected; it is not offered in a remote support tab.

1. The app asks the device `info show` and `config show`. These commands do not appear in the console. A line under
   the toolbar shows **Reading device information… (1 of 2)** with **Cancel**.
2. When both are read, the line offers **Include secrets** (off) and **Save…**; **Discard** throws the result away.
3. **Save…** opens the save dialog with the name prefilled, in the folder you used last (the first time
   Documents/HARDWARIO Monitor).
4. **Device information saved to …** with **Show in folder** confirms it.

The file is plain text: a header with the device name, serial number, firmware, date and the HARDWARIO Monitor
version, then for each command a line `### info show` and the device's answer as it printed it. The output of
`config show` stays a list of commands, so you can copy lines from it into a console or a [batch file](#batch-files).

- **Secrets** (the claim token, the BLE passkey, LoRaWAN keys, LTE passwords) are written as `<redacted>`, as in the
  [console files](#console-files). With **Include secrets** they are written as the device prints them: such a file
  is as sensitive as the device's keys, so keep it safe. On Linux and macOS only your user can read the file either
  way.
- A command the device does not answer within 10 seconds is written as `(no answer)`, and the confirmation names
  it. An answer longer than 2000 lines or 256 KiB is cut with a marker line.
- A device that reports no serial number gets the name `unknown-<J-Link serial number or Bluetooth address>_…`.
- If the device disconnects while it is read, nothing is written: *The device disconnected while its information was
  read.*

The command line does the same with `hwio chester app info save` and `hwio ble info save` (see
[Command line](cli.md)).

<a id="batch-files"></a>
## Batch files

A batch file is a UTF-8 text file with one shell command per line. **Batch…** (<kbd>Ctrl</kbd>+<kbd>B</kbd>) sends
the lines one by one, one every 200 ms. Empty lines and lines that start with `#` are skipped.

```text
# what the device is and how it is configured
info show
app config show
```

The whole file is checked before the first line is sent: at most 10 000 commands, 1 MiB per file and 4096 bytes per
line. While it runs, the toolbar shows **Batch 3 / 4** with a stop button. A batch stops when the device disconnects
or another operation (such as a flash) starts.

## Console files

Unless you turn it off, every connection is written to a console file: the shell, the log, the commands you sent and
the connection events, with timestamps. The files are in the **Log folder** shown in **Settings → Console**:

| System | Folder |
|---|---|
| Windows | `%LOCALAPPDATA%\HARDWARIO\Monitor\console` |
| macOS | `~/Library/Application Support/HARDWARIO/Monitor/console` |
| Linux | `~/.local/share/HARDWARIO/Monitor/console` |

A file is named after the device, the firmware, the time (UTC) and the probe, for example
`chester-app-clime-v3.2.0-20261103-101500-<serial>.log`. A file over 10 MiB continues in `….part2.log`. The folder
keeps the newest 50 files, at most 200 MB together. Secrets are written as `<redacted>` (the same commands as in the
history, and the claim token and BLE passkey of `info show`). On Linux and macOS only your user can read the files.

Turn console files off in **Settings → Console → Save console to log files**, or per probe in **⋯** (**Advanced**).

## Reconnecting

When the link drops (the device was reset, lost power, or the cable was pulled), the console reconnects by itself,
and the device bar shows **Reconnecting…** with the reason. Turn this off with **Auto-reconnect** in **⋯**
(**Advanced**) or **Settings → J-Link**; then <kbd>F4</kbd> or **Reconnect** attaches again.

If the device sends faster than the app can show, a banner says how many lines were dropped.

## Command line

The same console in a terminal:

```sh
hwio chester app console                   # interactive: F4 reconnects, Ctrl+C on an empty line or Ctrl+D quits
hwio chester app command "info show"       # one command, print the answer
hwio chester app info save                 # the device information file (also: hwio ble info save -a ID)
echo "app config show" | hwio chester app console
```

See [Command line](cli.md#console).
