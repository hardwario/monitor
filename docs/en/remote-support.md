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
7. Click **End sharing** when you are done.

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
5. With a HARDWARIO Monitor customer, the **Flash** page offers **Ask to flash** (a catalog firmware ID), **Ask to
   erase**, **Ask to recover** and **Ask to reset** for the application or the LTE core. Each request waits for the
   customer's confirmation; the result is reported by the customer's Monitor. With a web Terminal customer only the
   console is available.

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
```

`hwio remote host` asks on the terminal before it allows a join or an operation. Without a terminal on stdin joins are
never allowed. See [Command line](cli.md#remote-support-hwio-remote).

<a id="settings"></a>
## Settings

**Settings** › **Advanced** › **Support server** changes the server (for example a test server). Only `https` addresses
are accepted. Leave it empty for `https://terminal.hardwario.com`.
