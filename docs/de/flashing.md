# Flashen

[Dokumentation](index.md) › Flashen

Die Seite **Flash** (Flashen, <kbd>Ctrl</kbd>+<kbd>2</kbd>) schreibt Firmware über den J-Link in den CHESTER und
löscht, stellt wieder her (Recover) und setzt seine MCUs zurück. Sie arbeitet in drei Schritten, und die Schaltfläche
**Flash** unten startet den Vorgang (<kbd>Ctrl</kbd>+<kbd>Enter</kbd>). Ist die Schaltfläche deaktiviert, sagt der
Text daneben, warum, zum Beispiel **Choose a firmware file or enter a catalog ID** (Firmware-Datei wählen oder
Katalog-ID eingeben).

Während ein Flash-Vorgang läuft, ist die Konsole des Tabs angehalten und wird danach selbstständig fortgesetzt.

## 1. Was möchten Sie flashen?

| Auswahl | MCU | Firmware |
|---|---|---|
| **Application firmware** (Anwendungsfirmware) | die Anwendungs-MCU | eine `.hex`-Datei oder eine Firmware-ID aus dem Katalog |
| **LTE firmware** (LTE-Firmware) | die LTE-MCU und ihr Modem | die HARDWARIO-LTE-Firmware oder Ihre eigene `.hex` / `.zip` |
| **LoRaWAN module** (LoRaWAN-Modul) | das Modul Murata Type ABZ (CHESTER mit LoRaWAN) | lora-modem für CHESTER oder Ihre eigene `.hex` / `.bin` |

Die Karte zeigt die Firmware, die sich gerade im Gerät befindet, sofern das Gerät sie meldet.

Ein Tab arbeitet immer mit der Anwendungs-MCU. **LTE firmware** (LTE-Firmware) wird nicht auf dieser Seite geflasht:
Es startet das geführte [LTE-Firmware-Update](#lte-firmware-update), das Ihnen sagt, wann Sie das J-Link-Kabel
umstecken. **LoRaWAN module** (LoRaWAN-Modul) funktioniert genauso: **Flash** öffnet einen Assistenten, der Ihnen
sagt, wann Sie das Kabel umstecken ([LoRaWAN-Modul](#lorawan-module)).

## 2. Firmware

### Anwendungsfirmware

- Legen Sie eine `.hex`-Datei an beliebiger Stelle auf der Seite ab, oder drücken Sie **Browse…** (Durchsuchen…,
  <kbd>Ctrl</kbd>+<kbd>O</kbd>).
- Oder geben Sie eine **firmware ID from the catalog** (Firmware-ID aus dem Katalog; 32 Zeichen, `0`–`9` und
  `a`–`f`) ein und drücken Sie **Download**. Die Seite zeigt Name, Version und Upload-Datum der Firmware (oder
  **Unknown firmware — it can still be flashed.**, unbekannte Firmware, sie kann trotzdem geflasht werden).
  **Find firmware IDs ↗** öffnet die Liste der CHESTER-Katalog-Anwendungen auf docs.hardwario.com.

Heruntergeladene Firmware wird in einem Cache aufbewahrt und nicht erneut heruntergeladen. **Recent** (Zuletzt
verwendet) listet die letzten 10 Dateien und IDs.

### LTE-Firmware

Sie wählen sie im ersten Schritt des [LTE-Firmware-Updates](#lte-firmware-update):

- **HARDWARIO LTE firmware 1.7.0 (recommended)** (empfohlen): die LTE-Anwendung und die Modem-Firmware, die
  HARDWARIO mit CHESTER getestet hat. Sie wird von docs.hardwario.com heruntergeladen, und ihre Prüfsumme wird
  verifiziert.
- **Use your own file…** (eigene Datei verwenden…): eine Anwendungs-`.hex`, ein CHESTER-LTE-Bundle `.zip`
  (Modem-Firmware und LTE-Anwendung) oder eine Nordic-Modem-Firmware `.zip`. HARDWARIO Monitor prüft den Inhalt der
  Datei, nicht ihren Namen, und lehnt eine ZIP-Datei ab, die keines von beiden ist.

Die Modem-Firmware benötigt **nRF Util**
([Installation](install.md#nordic-nrf-util-nur-für-die-lte-modem-firmware)). Das Aktualisieren des Modems dauert
1–3 Minuten, und die Konsole ist währenddessen getrennt.

Wählen Sie eine Datei der anderen Art, bietet die Seite an, umzuschalten (**This is LTE firmware — switch to LTE
firmware?**).

## 3. Optionen

- **Halt the CPU after flashing** (CPU nach dem Flashen anhalten): Die CPU startet die neue Firmware erst, wenn Sie
  sie zurücksetzen oder laufen lassen.
- **Allow writing UICR** (Schreiben des UICR erlauben): wird nur angezeigt, wenn das Image UICR-Einträge enthält;
  siehe unten.

### UICR und der Product Information Block

Der UICR ist ein kleiner Flash-Bereich mit der Konfiguration des Geräts. Bei CHESTER enthält er außerdem den
**Product Information Block** (PIB): die Seriennummer, die Hardware-Variante und -Revision, den Claim-Token und den
BLE-Passkey.

- Flashen schreibt den UICR nie, es sei denn, Sie erlauben es (**Allow writing UICR** oder `--allow-uicr` auf der
  Kommandozeile), und es ändert nie den PIB.
- **Erase application** (Anwendung löschen) behält den UICR und den PIB.
- **Erase everything** (Alles löschen) und **Recover** löschen auch den PIB. HARDWARIO Monitor kann ihn nicht
  zurückschreiben, daher fragen sie zuerst nach.

`hwio chester app pib read` zeigt den PIB.

## Fortschritt und Ergebnis

Die Fortschrittskarte zeigt den Schritt (**Connecting**, **Erasing**, **Programming**, **Verifying**, **Resetting**
und, für das Modem, **Updating the modem**), die Bytes und die Zeit. **Cancel** (Abbrechen) stoppt den Vorgang; ein
abgebrochener oder unterbrochener Flash-Vorgang hinterlässt das Gerät ohne funktionierende Firmware, bis Sie es
erneut flashen.

Am Ende zeigt die Seite zum Beispiel **Firmware flashed and verified in 12 s.**, mit **Open console** (Konsole
öffnen) und **Flash again** (Erneut flashen).

<a id="lte-firmware-update"></a>
## LTE-Firmware-Update

Bei einem CHESTER-M hat die LTE-MCU ihren eigenen Programmieranschluss, und die Anwendungsfirmware setzt die LTE-MCU
zurück, wenn das Modem nicht antwortet, was einen Flash-Vorgang abbricht. **Update LTE firmware…** (LTE-Firmware
aktualisieren…; auf der Karte **LTE firmware** oder im Banner, wenn der J-Link am LTE-Anschluss steckt) führt Sie
deshalb durch den ganzen Ablauf:

1. **Firmware**: die HARDWARIO-LTE-Firmware oder Ihre eigene Datei.
2. **APP connector** (APP-Anschluss): Schließen Sie den J-Link an den APP-Anschluss an. Der Assistent geht von selbst
   weiter, sobald die Anwendungs-MCU antwortet; am falschen Anschluss meldet er **This is the LTE connector. Move the
   cable to the APP connector.** (das ist der LTE-Anschluss, stecken Sie das Kabel an den APP-Anschluss um).
3. **Stop the application** (Anwendung anhalten): Eine Kopie der Anwendungsfirmware wird gespeichert, dann wird die
   Anwendung gelöscht (**Erase application**: der UICR und der PIB bleiben erhalten). Lässt sich keine Kopie
   erstellen, zum Beispiel bei einer gegen Auslesen geschützten MCU, können Sie mit **Continue without a copy** (ohne
   Kopie fortfahren) weitermachen und am Ende selbst eine Anwendung flashen.
4. **LTE connector** (LTE-Anschluss): Stecken Sie den J-Link an den LTE-Anschluss um.
5. **Update** (Aktualisierung): Die LTE-MCU und ihr Modem werden aktualisiert (bei einem Bundle in zwei Schritten).
6. **APP connector**: Stecken Sie den J-Link zurück.
7. **Application** (Anwendung): Die Kopie wird zurückgeschrieben (Standard), oder Sie wählen Firmware aus dem Katalog
   oder eine Datei.
8. **Done** (Fertig): Die Anwendung läuft wieder, und die Konsole verbindet sich neu.

Während der Assistent läuft, ist die Konsole des Tabs angehalten (**Paused for the LTE firmware update**). Abbrechen
ist jederzeit möglich, außer während die MCU beschrieben wird. Nach Schritt 3 fragt der Assistent zuerst nach: Die
Anwendungs-MCU ist gelöscht, und das Gerät läuft erst wieder, wenn Sie eine Anwendung flashen. Die Kopie bleibt im
Ordner `backups` ([Datenschutz und gespeicherte Daten](privacy.md)).

## Wartung

**Maintenance — application MCU** (Wartung — Anwendungs-MCU) unten auf der Seite ist standardmäßig eingeklappt:

| Aktion | Funktion | Fragt zuerst |
|---|---|---|
| **Reset** | Setzt die MCU zurück | nein |
| **Reset and halt** | Setzt die MCU zurück und hält die CPU angehalten | nein |
| **Erase application** | Löscht die Firmware; behält den UICR und den PIB (nur Anwendungs-MCU) | nein |
| **Erase everything…** | Löscht den Flash, den UICR und den PIB (nur Expertenaktionen) | ja |
| **Recover…** | Siehe unten (nur Expertenaktionen) | ja |

**Erase everything…**, **Recover…** und die Wartung der **LTE MCU** (eine Auswahl **Application MCU | LTE MCU** über
den Schaltflächen) erscheinen nur mit **Settings › Advanced › Show expert maintenance actions**
(Experten-Wartungsaktionen anzeigen), weil sie die Seriennummer und die Konfiguration des Geräts löschen können. Auf
der LTE-MCU brauchen sie den J-Link am LTE-Anschluss. Bei einem CHESTER mit LoRaWAN bietet die Auswahl auch
**LoRaWAN module** ([LoRaWAN-Modul](#lorawan-module)).

<a id="recover"></a>
### Recover

Ist die MCU gegen Auslesen geschützt (approtect), kann der J-Link sie weder lesen noch beschreiben, und das Flashen
schlägt mit [target.protected](troubleshooting.md#target.protected) fehl. **Recover…** entfernt den Schutz, indem es
die MCU vollständig löscht, einschließlich des UICR und des PIB. Auf der LTE-MCU programmiert es danach ein kleines
Image, das die MCU debugbar hält. Flashen Sie nach einem Recover die Firmware erneut. Recover läuft nie von selbst.
Schickt Sie die Fehlerbehebungsanleitung für eine geschützte MCU auf die Seite **Flash**, wird **Recover…** dort
einmal angezeigt, auch ohne die Experteneinstellung.

<a id="lorawan-module"></a>
## LoRaWAN-Modul

Ein CHESTER mit LoRaWAN hat das Modul Murata Type ABZ (ein STM32L072CZ mit dem LoRa-Funkteil) mit einem eigenen
Programmieranschluss, **LoRaWAN**. Das Modul hat seine eigene Firmware, die HARDWARIO Monitor über den J-Link flasht.
In HARDWARIO Monitor hat das Modul keine Konsole: Seine LoRaWAN-Einstellungen (Schlüssel, Region, …) setzen Sie mit
den Befehlen `lrw config …` in der Konsole der Anwendungs-MCU.

**Firmware:** Wählen Sie in Schritt 1 **LoRaWAN module** (LoRaWAN-Modul):

- **lora-modem for CHESTER 1.4.1 (recommended)** (empfohlen): die offene LoRaWAN-Modem-Firmware von HARDWARIO, Projekt
  [hardwario/lora-modem](https://github.com/hardwario/lora-modem), Lizenz BSD-3-Clause. Sie ist nicht Teil von
  HARDWARIO Monitor: Die App lädt die Release-Datei bei der ersten Verwendung von GitHub herunter, prüft ihre
  SHA-256-Prüfsumme und legt sie im Cache ab.
- **Use your own file…** (Eigene Datei verwenden…): eine `.hex`- oder `.bin`-Datei; eine `.bin`-Datei braucht ihre
  **Start address** (Startadresse, Standard `0x08000000`).

Die Optionen **Erase the whole chip first** und **Keep data EEPROM** wirken wie bei [STM32-Chips](#stm32-chips).
Wählen Sie auf **Application firmware** eine lora-modem-Datei, bietet die Seite **This is LoRaWAN module firmware —
switch?** (Dies ist Firmware für das LoRaWAN-Modul — wechseln?) an.

**Warum das Kabel umgesteckt wird:** Die Anwendungs-MCU steuert die Versorgung und den Reset des Moduls, deshalb
antwortet das Modul an seinem Anschluss nur, solange die Anwendungs-MCU gestoppt ist. **Flash** öffnet daher einen
Assistenten, wie das LTE-Firmware-Update:

1. **APP connector** (APP-Anschluss): Schließen Sie den J-Link an den APP-Anschluss an.
2. **Stop the application** (Anwendung stoppen): Die Anwendungs-MCU wird zurückgesetzt und bleibt angehalten.
3. **LoRaWAN connector** (LoRaWAN-Anschluss): Stecken Sie den J-Link an den LoRaWAN-Anschluss um. Der Assistent geht
   von selbst weiter, sobald das Modul antwortet.
4. **Flash** (Flashen): Das Modul wird beschrieben und geprüft.
5. **APP connector**: Stecken Sie den J-Link zurück; die Anwendungs-MCU startet wieder und mit ihr das Modul.
6. **Done** (Fertig): **LoRaWAN module flashed with lora-modem for CHESTER 1.4.1.**; die Konsole verbindet sich neu.

Während der Assistent läuft, ist die Konsole des Tabs angehalten (**Paused for the LoRaWAN module**). Abbrechen geht,
solange der Assistent auf das Kabel wartet. Nach Schritt 2 bleibt die Anwendungs-MCU gestoppt, bis CHESTER
zurückgesetzt oder aus- und wieder eingeschaltet wird.

**Ein Modul ab Werk** hat die ursprüngliche Murata-Firmware und einen Ausleseschutz (RDP-Stufe 1). In Schritt 4 fragt
der Assistent dann zuerst **Unlock the LoRaWAN module?** (LoRaWAN-Modul entsperren?): **Unlock and erase** (Entsperren
und löschen) entfernt den Schutz, und das Modul löscht sich vollständig. Die ursprüngliche Murata-Firmware des Moduls
wird entfernt; sie lässt sich vorher nicht sichern, und HARDWARIO Monitor kann sie nicht zurückschreiben. Nach dem
Entsperren geht das Flashen weiter.

**Wartung:** Mit **Settings › Advanced › Show expert maintenance actions** (Experten-Wartungsaktionen anzeigen) bietet
die Auswahl über den Wartungsschaltflächen auch **LoRaWAN module**, mit **Reset**, **Erase chip…** (Chip löschen:
Flash und Daten-EEPROM) und **Unlock (erase)…** (Entsperren und löschen). Jede Aktion führt durch denselben
Assistenten mit dem Umstecken (Schritt 4 heißt dann **Reset**, **Erase** oder **Unlock**); Löschen und Entsperren
fragen zuerst.

Auf der Kommandozeile: `hwio chester lrw` ([Kommandozeile](../en/cli.md#lrw)).

<a id="stm32-chips"></a>
## STM32-Chips flashen

HARDWARIO Monitor flasht über den J-Link auch STM32L0-Chips, zum Beispiel den Controller einer CHESTER-Erweiterungsplatine.

**Anschließen:** Verbinden Sie die SWD-Pins des J-Link mit dem Chip: **SWDIO**, **SWCLK**, **GND** und **VTref** (die
Versorgungsspannung des Chips, an der der J-Link erkennt, dass die Platine versorgt ist). Die Platine braucht eine
eigene Stromversorgung. Wählen Sie im Tab **New device** (Neues Gerät) die Probe, dann unter **Device** (Gerät) **STM32
chip** (STM32-Chip), unter **Chip** den Baustein, und drücken Sie **Connect** (Verbinden).

**Chip** listet die unterstützten STM32L0-Bausteine nach Kategorie und Flash-Größe, zum Beispiel **STM32L0 cat. 3, 64
KiB**; der Hinweis nennt die Chips, die der Eintrag abdeckt. Chips einer Kategorie mit gleicher Flash-Größe haben
dieselbe ID und sind für die App nicht unterscheidbar, deshalb deckt ein Eintrag sie alle ab. Die Value Line
STM32L010 wird nicht abgedeckt. `hwio stm32 chips` gibt die Liste aus.

**Seite Flash:** Schritt 1 hat eine einzige Karte, **Firmware for &lt;chip&gt;** (Firmware für &lt;Chip&gt;), die eine **.hex or
.bin file** (`.hex`- oder `.bin`-Datei) annimmt. Eine `.bin`-Datei enthält keine Adressen: Geben Sie ihre **Start
address** (Startadresse) ein (Standard `0x08000000`, der Anfang des Flash). Optionen:

- **Erase the whole chip first** (Zuerst den ganzen Chip löschen): löscht vor dem Schreiben den Flash und das
  Daten-EEPROM. Standardmäßig aus; ein normaler Flash-Vorgang löscht nur die Sektoren, die er beschreibt.
- **Keep data EEPROM** (Daten-EEPROM behalten, standardmäßig an): Die Daten-EEPROM-Records der Datei werden nicht
  geschrieben. Schalten Sie die Option aus, wenn die Firmware das Daten-EEPROM ändern soll.
- **Halt the CPU after flashing** (CPU nach dem Flashen anhalten).

Vor dem Schreiben liest HARDWARIO Monitor die ID und die Flash-Größe des Chips. Ist es ein anderer Chip als der
gewählte, bricht das Flashen mit [target.wrong_chip](troubleshooting.md#target.wrong_chip) ab; steht der gefundene Chip
in der Liste, schaltet **Use &lt;found chip&gt;** (&lt;gefundenen Chip&gt; verwenden) den Tab auf ihn um.

**Ausleseschutz (RDP):** Auf Stufe 1 kann der J-Link den Flash weder lesen noch beschreiben, und das Flashen bricht mit
[target.protected](troubleshooting.md#target.protected) und einer Schaltfläche **Unlock…** (Entsperren) ab. Das
Entsperren entfernt den Schutz, und der Chip löscht sich danach vollständig, einschließlich des Daten-EEPROM. Das lässt
sich nicht rückgängig machen, und die Firmware eines geschützten Chips kann vorher nicht gesichert werden. Starten Sie
das Flashen nach dem Entsperren erneut. Stufe 2 ist dauerhaft: Der Chip lässt sich nicht mehr neu programmieren
([target.permanently_protected](troubleshooting.md#target.permanently_protected)).

**Maintenance — &lt;chip&gt;** (Wartung — &lt;Chip&gt;) bietet **Reset**, **Reset and halt**, **Erase chip…** (Chip löschen: Flash
und Daten-EEPROM) und **Unlock (erase)…** (Entsperren und löschen); die beiden letzten fragen zuerst. Die Seite
**Console** (Konsole) des Tabs erklärt, dass der Chip in HARDWARIO Monitor keine Konsole hat.

### CHESTER-S1 (Clime IAQ)

Der Controller der zweiten Platine von CHESTER Clime IAQ (CHESTER-S1) ist ein **STM32L0 cat. 3, 64 KiB**. HARDWARIO
hat dafür noch keine vorbereitete Firmware, und ohne seine Firmware funktionieren die Sensoren nicht: **Sichern Sie
ihn, bevor Sie ihn flashen oder löschen.** `hwio stm32 info --chip stm32l0-cat3-64k` zeigt die Stufe des
Ausleseschutzes; bei RDP 0 sichern Sie den Flash und das Daten-EEPROM mit dem J-Link Commander (`JLinkExe`, unter
Windows `JLink.exe`):

```text
JLinkExe -device STM32L053R8 -if SWD -speed 4000 -autoconnect 1
J-Link> halt
J-Link> savebin s1-flash.bin 0x08000000 0x10000
J-Link> savebin s1-eeprom.bin 0x08080000 0x800
J-Link> exit
```

Wiederherstellen: `hwio stm32 flash --chip stm32l0-cat3-64k s1-flash.bin`, danach
`hwio stm32 flash --chip stm32l0-cat3-64k --address 0x08080000 --allow-eeprom s1-eeprom.bin`.

## Kommandozeile

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

Siehe [Kommandozeile](../en/cli.md#flashing).
