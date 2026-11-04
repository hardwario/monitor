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

Die Karte zeigt die Firmware, die sich gerade im Gerät befindet, sofern das Gerät sie meldet.

Ein Tab arbeitet immer mit der Anwendungs-MCU. **LTE firmware** (LTE-Firmware) wird nicht auf dieser Seite geflasht:
Es startet das geführte [LTE-Firmware-Update](#lte-firmware-update), das Ihnen sagt, wann Sie das J-Link-Kabel
umstecken.

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
der LTE-MCU brauchen sie den J-Link am LTE-Anschluss.

<a id="recover"></a>
### Recover

Ist die MCU gegen Auslesen geschützt (approtect), kann der J-Link sie weder lesen noch beschreiben, und das Flashen
schlägt mit [target.protected](troubleshooting.md#target.protected) fehl. **Recover…** entfernt den Schutz, indem es
die MCU vollständig löscht, einschließlich des UICR und des PIB. Auf der LTE-MCU programmiert es danach ein kleines
Image, das die MCU debugbar hält. Flashen Sie nach einem Recover die Firmware erneut. Recover läuft nie von selbst.
Schickt Sie die Fehlerbehebungsanleitung für eine geschützte MCU auf die Seite **Flash**, wird **Recover…** dort
einmal angezeigt, auch ohne die Experteneinstellung.

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
```

Siehe [Kommandozeile](../en/cli.md#flashing).
