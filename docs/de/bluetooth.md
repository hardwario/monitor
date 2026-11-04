# Bluetooth LE

[Dokumentation](index.md) › Bluetooth LE

CHESTER bietet seine Befehls-Shell über Bluetooth LE an (den Nordic UART Service). HARDWARIO Monitor erreicht sie in der
Desktop-App und auf der Kommandozeile mit `hwio ble`; dafür sind weder J-Link noch Kabel nötig.

Die Shell über Bluetooth funktioniert wie die Konsole über den J-Link: Sie senden Befehle und lesen ihre Antworten,
und Log-Meldungen, die die Firmware sendet, erscheinen als Log-Zeilen.

## In der Desktop-App

1. Öffnen Sie einen neuen Tab (<kbd>Ctrl</kbd>+<kbd>T</kbd>) und stellen Sie **Connect via** (Verbinden über) auf
   **Bluetooth** um. Die App merkt sich die Wahl für den nächsten Tab.
2. Die Liste zeigt die CHESTER-Geräte in der Nähe, das stärkste Signal zuerst, und aktualisiert sich selbst. Das
   Signal wird über einige Sekunden gemittelt, und ein Gerät rückt nur auf, wenn es deutlich stärker ist (6 dB), damit
   die Liste ruhig bleibt; solange der Mauszeiger über der Liste ist (oder eine Zeile den Tastaturfokus hat), ändert
   sich die Reihenfolge gar nicht, und neue Geräte werden am Ende angefügt. Filtern Sie sie nach Namen, oder setzen
   Sie das Häkchen bei **Show all devices** (alle Geräte anzeigen), um jedes Bluetooth-Gerät zu sehen (CHESTER-Geräte
   zuerst). Ein Gerät, das ein anderer Tab verwendet, ist mit **In use (tab N)** (in Verwendung, Tab N) markiert; wenn
   Sie es wählen, wechseln Sie zu diesem Tab. Ein Gerät, das ein anderes Programm verwendet, ist mit **In use by
   another program** (von einem anderen Programm verwendet) markiert und kann nicht gewählt werden.
3. Wählen Sie das Gerät und drücken Sie **Connect** (Verbinden), oder doppelklicken Sie es, oder drücken Sie
   <kbd>Enter</kbd>.

Der Tab funktioniert wie ein J-Link-Tab: die Konsole mit Vorschlägen, Verlauf, Suche, Batch-Dateien und
Konsolendateien ([Konsole](console.md)). Der Log-Bereich erscheint nur, wenn das Gerät Log-Meldungen sendet. Was sich
unterscheidet:

- Flashen braucht einen J-Link: **Flash** (Flashen) ist in einem Bluetooth-Tab deaktiviert. Öffnen Sie einen
  J-Link-Tab, um das Gerät zu flashen.
- **Auto-reconnect** (automatisch neu verbinden) befindet sich im Menü **⋯** der Geräteleiste und ist standardmäßig
  aus; die App merkt es sich pro Gerät.
- Das erste Verbinden mit einem Gerät, das noch nicht gekoppelt ist, zeigt **Waiting for pairing — enter the passkey
  in the system dialog** (warte auf Kopplung — geben Sie den Passkey im Systemdialog ein) und bei Bedarf **Pair**
  (Koppeln; Linux, Windows). Siehe [Kopplung](#kopplung).
- Wird das Gerät nicht gefunden, kehrt **Choose another device** (anderes Gerät wählen) zur Liste zurück.

Bluetooth-Tabs kommen nach einem Neustart getrennt zurück; drücken Sie **Connect**. Die App startet Bluetooth erst,
wenn Sie die Bluetooth-Ansicht zum ersten Mal öffnen, daher fragt macOS erst dann nach dem Bluetooth-Zugriff.

## Gerät finden (Kommandozeile)

```sh
hwio ble scan
```

Der Scan dauert 5 Sekunden (`--timeout S`, 1 bis 60) und listet die CHESTER-Geräte in der Nähe auf: diejenigen, deren
angekündigter Name `CHESTER` enthält. `--all` listet jedes Bluetooth-Gerät auf.

Jedes Gerät hat eine **ID**:

- Linux und Windows: seine Bluetooth-Adresse, zum Beispiel `F1:22:33:44:55:66`.
- macOS: eine UUID, die macOS dem Gerät zuweist. Sie ist nicht die Bluetooth-Adresse und gilt nur auf diesem Mac.

## Verbinden (Kommandozeile)

Wählen Sie das Gerät über seine ID (`-a`) oder über einen Teil seines Namens (`--name`, ohne Beachtung der
Groß-/Kleinschreibung; genau ein Gerät, das ein 5-Sekunden-Scan findet, muss passen):

```sh
hwio ble console -a F1:22:33:44:55:66      # interactive console
hwio ble console --name "CHESTER Clime"
hwio ble command -a F1:22:33:44:55:66 "info show"
```

Eine Verbindung kann bis zu 30 Sekunden dauern; fehlgeschlagene Versuche werden innerhalb dieser Zeit wiederholt.
`ble command` gibt die Antwort aus und endet beim Shell-Prompt oder nach `-t` Sekunden (standardmäßig 3).

In der interaktiven Konsole verbindet <kbd>F4</kbd> neu, und <kbd>Ctrl</kbd>+<kbd>C</kbd> in einer leeren Zeile oder
<kbd>Ctrl</kbd>+<kbd>D</kbd> beendet sie. Mit `--auto-reconnect` verbindet sich die Konsole nach einem
Verbindungsabbruch erneut, solange das Gerät in Reichweite ist. Die Konsole schreibt eine Konsolendatei und führt
einen Befehlsverlauf, wie die J-Link-Konsole ([Konsole](console.md#konsolendateien)).

Nur ein Programm kann mit einem Gerät verbunden sein. Ist ein anderes `hwio` damit verbunden, erhalten Sie
[ble.in_use_by_other_process](troubleshooting.md#ble.in_use_by_other_process).

## Kopplung

CHESTER schützt seine Bluetooth-Shell mit einem Passkey, daher muss der Computer sich einmal mit ihm koppeln (Bond).
Der Passkey ist 123456, sofern er bei der Konfiguration des Geräts nicht geändert wurde. HARDWARIO Monitor speichert
den Passkey nie und gibt ihn nie für Sie ein: Das Betriebssystem fragt danach.

| System | Wie |
|---|---|
| Linux | Beim ersten Verbinden, mit **Pair** in der App oder mit `hwio ble pair -a ID`. Die Bluetooth-Einstellungen des Desktops fragen nach dem Passkey. |
| Windows | Beim ersten Verbinden, mit **Pair** in der App oder mit `hwio ble pair -a ID`. Windows fragt nach dem Passkey. |
| macOS | Nur beim ersten Verbinden; macOS fragt nach dem Passkey. `hwio ble pair` und **Pair** sind unter macOS nicht verfügbar. |

Wartet das Gerät auf den Passkey, wartet die Verbindung bis zu 60 Sekunden.

**Linux ohne Pairing-Agent.** Manche Desktops (GNOME) fragen nur nach dem Passkey, solange ihre
Bluetooth-Einstellungen geöffnet sind. Fragt nichts, erhalten Sie
[ble.no_pairing_agent](troubleshooting.md#ble.no_pairing_agent). Koppeln Sie das Gerät in den
Bluetooth-Einstellungen des Systems oder mit `bluetoothctl`:

```text
$ bluetoothctl
[bluetooth]# agent KeyboardOnly
[bluetooth]# default-agent
[bluetooth]# pair F1:22:33:44:55:66
```

Schlägt die Kopplung fehl, nachdem der Passkey auf dem Gerät geändert wurde, entfernen Sie das Gerät in den
Bluetooth-Einstellungen des Systems und koppeln Sie es erneut
([ble.pairing_failed](troubleshooting.md#ble.pairing_failed)).

## Berechtigungen und Adapter

- **Linux:** Bluetooth verwendet BlueZ. Wenn es nicht läuft, starten Sie es mit `sudo systemctl start bluetooth`.
- **macOS:** Beim ersten Scan fragt macOS nach dem Bluetooth-Zugriff: in der App für HARDWARIO Monitor, auf der
  Kommandozeile für die App, aus der Sie `hwio` starten (meist Terminal). Wenn Sie abgelehnt haben, erlauben Sie es in
  **System Settings → Privacy & Security → Bluetooth** (Systemeinstellungen → Datenschutz & Sicherheit → Bluetooth);
  in der App bietet der Fehler **Open Privacy settings** (Datenschutz-Einstellungen öffnen) an.
- **Alle Systeme:** Schalten Sie den Bluetooth-Adapter ein. `hwio doctor` zeigt, ob er bereit ist.

## JSON

Mit `--json` gibt `ble scan` die Geräte aus, und `ble console` gibt eine JSON-Zeile pro Konsolenzeile aus, wie die
J-Link-Konsole ([Kommandozeile](../en/cli.md#json-output)).
