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
| **LoRaWAN module** | the Murata Type ABZ module (CHESTER with LoRaWAN) | lora-modem for CHESTER, or your own `.hex` / `.bin` |

The card shows the firmware that is in the device now, when the device reports it.

A tab always works with the application MCU. **LTE firmware** is not flashed from this page: it starts the guided
[LTE firmware update](#lte-firmware-update), which tells you when to move the J-Link cable. **LoRaWAN module** works
the same way: **Flash** opens a guide that tells you when to move the cable ([LoRaWAN module](#lorawan-module)).

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
the serial number and the configuration of the device. On the LTE MCU they need the J-Link on the LTE connector. On a
CHESTER with LoRaWAN the choice also lists **LoRaWAN module** ([LoRaWAN module](#lorawan-module)).

<a id="recover"></a>
### Recover

When the MCU is read-back protected (approtect), the J-Link cannot read or write it, and flashing fails with
[target.protected](troubleshooting.md#target.protected). **Recover…** removes the protection by erasing the MCU
completely, including the UICR and the PIB. On the LTE MCU it then programs a small image that keeps the MCU
debuggable. After a recover, flash the firmware again. Recover never runs by itself. When the troubleshooting guide
for a protected MCU sends you to the Flash page, **Recover…** is shown there once, without the expert setting.

<a id="lorawan-module"></a>
## LoRaWAN module

A CHESTER with LoRaWAN has the Murata Type ABZ module (an STM32L072CZ with the LoRa radio) and its own programming
connector, **LoRaWAN**. The module runs its own firmware, which HARDWARIO Monitor flashes over the J-Link. The module
has no console in HARDWARIO Monitor: its LoRaWAN settings (keys, region, …) are set with the `lrw config …` commands in
the console of the application MCU.

**Firmware:** choose **LoRaWAN module** in step 1:

- **lora-modem for CHESTER 1.4.1 (recommended)**: the open LoRaWAN modem firmware by HARDWARIO, project
  [hardwario/lora-modem](https://github.com/hardwario/lora-modem), licence BSD-3-Clause. It is not part of HARDWARIO
  Monitor: the app downloads the release file from GitHub on first use, checks its SHA-256 checksum and keeps it in
  the cache.
- **Use your own file…**: a `.hex` or `.bin` file; a `.bin` file needs its **Start address** (default `0x08000000`).

The options **Erase the whole chip first** and **Keep data EEPROM** work as for [STM32 chips](#stm32-chips). When you
choose a lora-modem file on **Application firmware**, the page offers **This is LoRaWAN module firmware — switch?**.

**Why the cable moves:** the application MCU controls the power and the reset of the module, so the module answers on
its connector only while the application MCU is stopped. **Flash** therefore opens a guide, like the LTE firmware
update:

1. **APP connector**: connect the J-Link to the APP connector.
2. **Stop the application**: the application MCU is reset and stays halted.
3. **LoRaWAN connector**: move the J-Link to the LoRaWAN connector. The guide continues by itself as soon as the
   module answers.
4. **Flash**: the module is written and verified.
5. **APP connector**: move the J-Link back; the application MCU starts again, and with it the module.
6. **Done**: **LoRaWAN module flashed with lora-modem for CHESTER 1.4.1.**; the console reconnects.

While the guide runs, the tab's console is paused (**Paused for the LoRaWAN module**). Cancel works while the guide
waits for the cable. After step 2 the application MCU stays stopped until CHESTER is reset or powered off and on.

**A factory module** comes with Murata's original firmware and read-out protection (RDP level 1). In step 4 the
guide then asks first, **Unlock the LoRaWAN module?**: **Unlock and erase** removes the protection, and the module
erases itself completely. The module's original Murata firmware is removed; it cannot be backed up first, and
HARDWARIO Monitor cannot write it back. The flash continues after the unlock.

**Maintenance:** with **Settings › Advanced › Show expert maintenance actions** the choice above the maintenance
buttons also lists **LoRaWAN module**, with **Erase chip…** (flash and data EEPROM) and **Unlock (erase)…**. Each
runs the same guide with the cable moves (step 4 is then **Erase** or **Unlock**) and asks first. There is no
**Reset** for the module: the guide's last step starts the application MCU again, which restarts the module.

On the command line: `hwio chester lrw` ([Command line](cli.md#lrw)).

<a id="stm32-chips"></a>
## Flashing STM32 chips

HARDWARIO Monitor also flashes STM32L0 chips over the J-Link, for example the controller of a CHESTER extension
board.

**Connect:** wire the J-Link's SWD pins to the chip: **SWDIO**, **SWCLK**, **GND** and **VTref** (the chip's supply
voltage, so that the J-Link knows the board is powered). The board needs its own power. In the **New device** tab
choose the probe, then **STM32 chip** under **Device**, choose the part under **Chip**, and press **Connect**.

**Chip** lists the supported STM32L0 parts by category and flash size, for example **STM32L0 cat. 3, 64 KiB**; the
hint names the chips the entry covers. Chips of one category with the same flash size have the same ID and the app
cannot tell them apart, so one entry covers them all. The STM32L010 value line is not covered. `hwio stm32 chips`
prints the list.

**Flash page:** step 1 has one card, **Firmware for &lt;chip&gt;**, which takes a **.hex or .bin file**. A `.bin` file has
no addresses: enter its **Start address** (default `0x08000000`, the start of the flash). Options:

- **Erase the whole chip first**: erases the flash and the data EEPROM before writing. Off by default; a normal flash
  erases only the sectors it writes.
- **Keep data EEPROM** (on by default): the data EEPROM records of the file are not written. Turn it off when the
  firmware is meant to change the data EEPROM.
- **Halt the CPU after flashing**.

Before writing, HARDWARIO Monitor reads the chip's ID and flash size. When the chip is another than the one you
chose, flashing stops with [target.wrong_chip](troubleshooting.md#target.wrong_chip); if the found chip is in the list,
**Use &lt;found chip&gt;** switches the tab to it.

**Read-out protection (RDP):** at level 1 the J-Link cannot read or write the flash, and flashing stops with
[target.protected](troubleshooting.md#target.protected) and an **Unlock…** button. Unlocking removes the protection,
and the chip then erases itself completely, including the data EEPROM. This cannot be undone, and the firmware on a
protected chip cannot be backed up first. After the unlock, start the flash again. Level 2 is permanent: the chip
cannot be reprogrammed ([target.permanently_protected](troubleshooting.md#target.permanently_protected)).

**Maintenance — &lt;chip&gt;** has **Reset**, **Reset and halt**, **Erase chip…** (flash and data EEPROM) and **Unlock
(erase)…**; the last two ask first. The **Console** page of the tab says that the chip has no console in HARDWARIO
Monitor.

### CHESTER-S1 (Clime IAQ)

The controller of the second board of CHESTER Clime IAQ (CHESTER-S1) is **STM32L0 cat. 3, 64 KiB**. HARDWARIO has no
prepared firmware for it yet, and the sensors do not work without its firmware: **back it up before you flash or
erase it**. `hwio stm32 info --chip stm32l0-cat3-64k` shows the read-out protection level; at RDP 0 save the flash and
the data EEPROM with J-Link Commander (`JLinkExe`, on Windows `JLink.exe`):

```text
JLinkExe -device STM32L053R8 -if SWD -speed 4000 -autoconnect 1
J-Link> halt
J-Link> savebin s1-flash.bin 0x08000000 0x10000
J-Link> savebin s1-eeprom.bin 0x08080000 0x800
J-Link> exit
```

To restore it: `hwio stm32 flash --chip stm32l0-cat3-64k s1-flash.bin`, then
`hwio stm32 flash --chip stm32l0-cat3-64k --address 0x08080000 --allow-eeprom s1-eeprom.bin`.

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
hwio chester lrw flash                                    # lora-modem for CHESTER (the LoRaWAN module, guided)
hwio stm32 flash --chip stm32l0-cat3-64k firmware.hex     # an STM32 chip (hwio stm32 chips)
```

See [Command line](cli.md#flashing).
