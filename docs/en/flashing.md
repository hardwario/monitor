# Flashing

[Documentation](index.md) › Flashing

The **Flash** page (<kbd>Ctrl</kbd>+<kbd>2</kbd>) writes firmware into the CHESTER over the J-Link and erases,
recovers and resets its MCUs. It works in three steps, and the **Flash** button at the bottom starts it
(<kbd>Ctrl</kbd>+<kbd>Enter</kbd>). When the button is disabled, the text next to it says why, for example **Choose a
firmware file or enter a catalog ID**.

While a flash runs, the console of the tab is paused and resumes by itself afterwards.

## 1. What do you want to flash?

| Choice | MCU | Firmware |
|---|---|---|
| **Application firmware** | the application MCU | a `.hex` file or a firmware ID from the catalog |
| **LTE firmware** | the LTE MCU and its modem | the HARDWARIO LTE firmware, or your own `.hex` / `.zip` |

The card shows the firmware that is in the device now, when the device reports it.

A tab always works with the application MCU. **LTE firmware** is not flashed from this page: it starts the guided
[LTE firmware update](#lte-firmware-update), which tells you when to move the J-Link cable.

## 2. Firmware

### Application firmware

- Drop a `.hex` file anywhere on the page, or press **Browse…** (<kbd>Ctrl</kbd>+<kbd>O</kbd>).
- Or type a **firmware ID from the catalog** (32 characters, `0`–`9` and `a`–`f`) and press **Download**. The page
  shows the firmware's name, version and upload date (or **Unknown firmware — it can still be flashed.**).
  **Find firmware IDs ↗** opens the list of CHESTER catalog applications on docs.hardwario.com.

Downloaded firmware is kept in a cache and is not downloaded again. **Recent** lists the last 10
files and IDs.

### LTE firmware

You choose it in the first step of the [LTE firmware update](#lte-firmware-update):

- **HARDWARIO LTE firmware 1.7.0 (recommended)**: the LTE application and the modem firmware HARDWARIO tested with
  CHESTER. It is downloaded from docs.hardwario.com and its checksum is verified.
- **Use your own file…**: an application `.hex`, a CHESTER LTE bundle `.zip` (modem firmware and LTE application), or
  a Nordic modem firmware `.zip`. HARDWARIO Monitor looks at what is inside the file, not at its name, and rejects a
  zip that is neither.

The modem firmware needs **nRF Util** ([Installing](install.md#nordic-nrf-util-lte-modem-firmware-only)). Updating the
modem takes 1–3 minutes, and the console is disconnected meanwhile.

If you choose a file of the other kind, the page offers to switch (**This is LTE firmware — switch to LTE
firmware?**).

## 3. Options

- **Halt the CPU after flashing**: the CPU does not start the new firmware until you reset it or let it run.
- **Allow writing UICR**: shown only when the image contains UICR records; see below.

### UICR and the Product Information Block

The UICR is a small flash area with the device's configuration. On CHESTER it also holds the **Product Information
Block** (PIB): the serial number, the hardware variant and revision, the claim token and the BLE passkey.

- Flashing never writes the UICR unless you allow it (**Allow writing UICR**, or `--allow-uicr` on the command line),
  and it never changes the PIB.
- **Erase application** keeps the UICR and the PIB.
- **Erase everything** and **Recover** erase the PIB too. HARDWARIO Monitor cannot write it back, so they ask first.

`hwio chester app pib read` shows the PIB.

## Progress and result

The progress card shows the step (**Connecting**, **Erasing**, **Programming**, **Verifying**, **Resetting** and, for
the modem, **Updating the modem**), the bytes and the time. **Cancel** stops it; a cancelled or interrupted flash
leaves the device without working firmware until you flash it again.

At the end the page says, for example, **Firmware flashed and verified in 12 s.**, with **Open console** and
**Flash again**.

<a id="lte-firmware-update"></a>
## LTE firmware update

On a CHESTER-M the LTE MCU has its own programming connector, and the application firmware resets the LTE MCU when the
modem does not answer, which breaks a flash. **Update LTE firmware…** (on the **LTE firmware** card, or in the banner
when the J-Link is on the LTE connector) therefore guides you through the whole procedure:

1. **Firmware**: the HARDWARIO LTE firmware or your own file.
2. **APP connector**: connect the J-Link to the APP connector. The guide continues by itself when the application MCU
   answers; on the wrong connector it says **This is the LTE connector. Move the cable to the APP connector.**
3. **Stop the application**: a copy of the application firmware is saved, then the application is erased (**Erase
   application**: the UICR and the PIB stay). If no copy can be made, for example on a read-back protected MCU, you
   can **Continue without a copy** and flash an application yourself at the end.
4. **LTE connector**: move the J-Link to the LTE connector.
5. **Update**: the LTE MCU and its modem are updated (two steps for a bundle).
6. **APP connector**: move the J-Link back.
7. **Application**: the copy is written back (default), or choose firmware from the catalog or a file.
8. **Done**: the application runs again and the console reconnects.

While the guide runs, the tab's console is paused (**Paused for the LTE firmware update**). Cancel works except while
the MCU is being written. After step 3 it asks first: the application MCU is erased and the device does not run until
you flash an application. The copy stays in the backups folder ([Privacy and stored data](privacy.md)).

## Maintenance

**Maintenance — application MCU** at the bottom of the page is folded by default:

| Action | Does | Asks first |
|---|---|---|
| **Reset** | Resets the MCU | no |
| **Reset and halt** | Resets the MCU and keeps the CPU halted | no |
| **Erase application** | Erases the firmware; keeps the UICR and the PIB (application MCU only) | no |
| **Erase everything…** | Erases the flash, the UICR and the PIB (expert actions only) | yes |
| **Recover…** | See below (expert actions only) | yes |

**Erase everything…**, **Recover…** and the maintenance of the **LTE MCU** (a choice **Application MCU | LTE MCU**
above the buttons) appear only with **Settings › Advanced › Show expert maintenance actions**, because they can erase
the serial number and the configuration of the device. On the LTE MCU they need the J-Link on the LTE connector.

<a id="recover"></a>
### Recover

When the MCU is read-back protected (approtect), the J-Link cannot read or write it, and flashing fails with
[target.protected](troubleshooting.md#target.protected). **Recover…** removes the protection by erasing the MCU
completely, including the UICR and the PIB. On the LTE MCU it then programs a small image that keeps the MCU
debuggable. After a recover, flash the firmware again. Recover never runs by itself. When the troubleshooting guide
for a protected MCU sends you to the Flash page, **Recover…** is shown there once, without the expert setting.

## Command line

```sh
hwio chester app flash firmware.hex
hwio chester app flash 0123456789abcdef0123456789abcdef   # a catalog ID
hwio chester app flash                                    # the build output in build/ or build/zephyr/
hwio chester lte flash                                    # the HARDWARIO LTE firmware
hwio chester lte flash mfw_nrf91x1_2.0.2.zip              # a modem firmware zip
hwio chester lte flash --guided                           # the guided LTE update (as in the app)
hwio chester app erase --all
hwio chester app recover
```

See [Command line](cli.md#flashing).
