# Remote support

HARDWARIO Support can look at your device's console from their office, send commands and, if you allow it each time,
flash catalog firmware, erase, recover or reset the device. You share the console with one click and read a 6-digit
code to support over the phone. Remote support works with the [HARDWARIO Terminal](https://terminal.hardwario.com/)
server and is compatible with the web Terminal: support can join from the web Terminal (console only) or from
HARDWARIO Monitor.

Other languages: [Čeština](../cs/remote-support.md) · [Deutsch](../de/remote-support.md) ·
[Slovenčina](../sk/remote-support.md) · [Polski](../pl/remote-support.md)

<a id="share-your-console"></a>
## Share your console

1. Connect the device (J-Link or Bluetooth) and open the **Console** page.
2. Click **Share with support…**. The dialog explains what support can do. Click **Start sharing**.
3. A banner shows **Shared with support — code 482 913**. Read the code to HARDWARIO Support. **Copy link** copies a
   link with the code.
4. When someone joins, HARDWARIO Monitor asks: *Someone joined session 482 913. Allow them to send commands to the
   device?* Click **Allow** only if HARDWARIO Support has just told you they are joining. **End session** (the default)
   ends the session for everyone. Without an answer the session ends after 2 minutes.
5. Commands from support appear in the console, and a line *— Remote: command <command> —* precedes each of them.
6. If support asks to flash, erase, recover or reset the device, you see what exactly they ask for, for example
   *Support asks to flash firmware CHESTER Clime v3.2.1 (ID …) to the application core.* **Deny** is the default; a
   request without an answer is denied after 60 seconds. Firmware comes only from the HARDWARIO catalog, and your
   HARDWARIO Monitor downloads and verifies it.
7. If support asks to update the LTE firmware, the question names the version your HARDWARIO Monitor recommends:
   *Support asks to update the LTE firmware to HARDWARIO LTE firmware 1.7.0. You will have to move the J-Link cable
   between the APP and LTE connectors.* After **Allow**, **Flash** › **Update LTE firmware…** opens with that firmware
   and starts by itself: follow its steps and move the cable when it asks. Sharing stays on while the device
   disconnects and reconnects, and support sees each step and the result.
   Before anything is changed, HARDWARIO Support confirms the update; you only see what will happen and follow the
   cable instructions (there is no button to click). If no copy of the application can be made, support decides
   whether to continue without it. The copy is always written back at the end. If support answers No, you see *Cancelled by support. Nothing was changed.* Without an answer from support within
   60 seconds the update is cancelled before anything changes.
8. Click **End sharing** when you are done.

Sharing also ends when you disconnect the device, close the tab or quit, after 30 minutes without activity (a banner
warns you 2 minutes before, **Keep sharing** extends it) and after 8 hours at most.

<a id="what-support-sees"></a>
## What support sees

**Anyone who knows the code sees the console output** from the moment they connect, even before you allow them. Read
the code only to HARDWARIO Support. Commands and operations always need your click: the console output is visible,
but nobody can control the device without your permission.

The session passes through the HARDWARIO server (`terminal.hardwario.com`) over an encrypted connection. Commands with
secret arguments and secret values printed by the device (such as the claim token) are sent hidden, as in the
console file.

Everything remote is written into the console file: the start and end of sharing, every join and your answer, every
remote command, and every operation request with its result. See [Console files](console.md).

For support's autocomplete, your HARDWARIO Monitor sends support the list of device commands it has learned, and when
support needs the subcommands of a command, it reads that command's help itself. These help queries are not shown in
your console; the console file records how many there were.

<a id="join-a-session"></a>
## Join a session (support)

1. On the **Choose a device** page of a tab, click **Join support session…**.
2. Type the code the customer reads to you (spaces are allowed) or paste the link. A link to another server than the
   configured one needs an extra confirmation.
3. A new tab opens, titled **Session 482 913** and then *CHESTER-M 2159018247 — remote* (or *Session 482 913 — web
   Terminal* when the customer uses the web Terminal). Until the customer allows you, the banner says *Waiting for the
   customer to allow you…*. The live output may already appear.
4. Once allowed, you get the customer's console history and can work in the console as usual: history, search,
   filter and save view.
   With a HARDWARIO Monitor customer, suggestions and Tab completion use the customer's list of device commands.
   Hexdump lines of the device log are hidden by default, as in a local tab (chip **H**).
5. With a HARDWARIO Monitor customer, the **Flash** page offers **Ask to flash** (a catalog firmware ID), **Ask to
   erase**, **Ask to recover** and **Ask to reset** for the application or the LTE core. Each request waits for the
   customer's confirmation; the result is reported by the customer's Monitor. With a web Terminal customer only the
   console is available.
   The result names the operation and the time, for example *Customer's device reset (14:02).*, and is also written
   into the console as a line. A success disappears when you leave the **Flash** page or ask for another operation;
   an error stays until you close it (×).
6. With **LTE modem core** selected, **Ask to update LTE firmware** asks for the guided LTE update to the version the
   customer's HARDWARIO Monitor recommends (no firmware ID). The customer moves the J-Link cable between the APP and
   LTE connectors; you see each step (for example *waiting for the J-Link on the LTE connector*) and the result. If the
   update ends without writing the application back, the result is `remote.application_not_restored`.
   Before the application is erased, the card asks you: *The application on the customer's CHESTER will be erased. …
   Continue?* (**Continue** · **Cancel**); if no copy of the application can be made it asks *The application could
   not be copied. Continue without a copy?*. No, or no answer within 60 seconds, cancels the update before anything
   changes. The copy is written back automatically. After your No the card says *LTE firmware update cancelled by you (14:02). Nothing was changed.* A customer with an older HARDWARIO Monitor answers these questions
   themselves.

When the customer ends sharing, HARDWARIO Monitor cannot always tell: the server does not announce it. After 30 minutes
without any activity the banner says so and offers **Leave**. The tab stays open, so you can still save the console.

<a id="command-line"></a>
## Command line

```
hwio remote host                          # share the J-Link console; prints the code and the link
hwio remote host --ble -a F1:22:33:44:55:66
hwio remote join 482913                   # interactive console
hwio remote join 482913 --command "info show"
hwio remote join 482913 --flash 0123456789abcdef0123456789abcdef
hwio remote join 482913 --reset --target lte
hwio remote join 482913 --lte-update      # the guided LTE update (the customer moves the cable)
```

`hwio remote host` asks on the terminal before it allows a join or an operation. Without a terminal on stdin joins are
never allowed. See [Command line](cli.md#remote-support-hwio-remote).

On a terminal, `hwio remote join --lte-update` asks you the questions of the LTE update with `[y/N]`.

<a id="settings"></a>
## Settings

**Settings** › **Expert** › **Support server** (in the Expert [mode](getting-started.md#modes)) changes the server (for
example a test server). Only `https` addresses are accepted. Leave it empty for `https://terminal.hardwario.com`.
