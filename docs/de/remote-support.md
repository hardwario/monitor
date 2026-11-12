# Fernsupport

Der HARDWARIO-Support kann sich die Konsole Ihres Geräts aus seinem Büro ansehen, Befehle senden und, wenn Sie es
jedes Mal erlauben, Katalog-Firmware flashen sowie das Gerät löschen, wiederherstellen (Recover) oder zurücksetzen. Sie
geben die Konsole mit einem Klick frei und lesen dem Support am Telefon einen 6-stelligen Code vor. Der Fernsupport
arbeitet mit dem Server von [HARDWARIO Terminal](https://terminal.hardwario.com/) und ist mit dem Web-Terminal
kompatibel: Der Support kann vom Web-Terminal aus (nur Konsole) oder aus HARDWARIO Monitor beitreten.

Andere Sprachen: [English](../en/remote-support.md) · [Čeština](../cs/remote-support.md) ·
[Slovenčina](../sk/remote-support.md) · [Polski](../pl/remote-support.md)

<a id="share-your-console"></a>
## Die Konsole freigeben

1. Verbinden Sie das Gerät (J-Link oder Bluetooth) und öffnen Sie die Seite **Konsole**.
2. Klicken Sie auf **Mit dem Support teilen…**. Der Dialog erklärt, was der Support tun kann. Klicken Sie auf
   **Freigabe starten**.
3. Ein Banner zeigt **Für den Support freigegeben — Code 482 913**. Lesen Sie den Code dem HARDWARIO-Support vor.
   **Link kopieren** kopiert einen Link mit dem Code.
4. Wenn jemand beitritt, fragt HARDWARIO Monitor: *Jemand ist der Sitzung 482 913 beigetreten. Erlauben Sie ihm, Befehle
   an das Gerät zu senden?* Klicken Sie nur dann auf **Erlauben**, wenn Ihnen der HARDWARIO-Support gerade gesagt hat,
   dass er beitritt. **Sitzung beenden** (die Vorgabe) beendet die Sitzung für alle. Ohne Antwort endet die Sitzung
   nach 2 Minuten.
5. Befehle des Supports erscheinen in der Konsole, jeweils mit einer vorangestellten Zeile *— Remote: Befehl <Befehl> —*.
6. Bittet der Support darum, das Gerät zu flashen, zu löschen, wiederherzustellen oder zurückzusetzen, sehen Sie genau,
   worum er bittet, zum Beispiel *Der Support bittet darum, die Firmware CHESTER Clime v3.2.1 (ID …) in den
   Anwendungskern zu flashen.* **Ablehnen** ist die Vorgabe; eine Anfrage ohne Antwort wird nach 60 Sekunden
   abgelehnt. Die Firmware stammt nur aus dem HARDWARIO-Katalog, und Ihr HARDWARIO Monitor lädt sie herunter und
   prüft sie.
7. Bittet der Support um ein Update der LTE-Firmware, nennt die Frage die Version, die Ihr HARDWARIO Monitor
   empfiehlt: *Der Support bittet, die LTE-Firmware auf HARDWARIO LTE firmware 1.7.0 zu aktualisieren. Sie müssen das
   J-Link-Kabel zwischen dem APP- und dem LTE-Anschluss umstecken.* Nach **Erlauben** öffnet sich **Flashen** ›
   **LTE-Firmware aktualisieren…** mit dieser Firmware und startet von selbst: Folgen Sie den Schritten und stecken
   Sie das Kabel um, wenn es verlangt wird. Die Freigabe bleibt aktiv, während sich das Gerät trennt und wieder
   verbindet, und der Support sieht jeden Schritt und das Ergebnis.
   Bevor etwas geändert wird, bestätigt der HARDWARIO Support das Update; Sie sehen nur, was geschieht, und folgen
   den Anweisungen zum Umstecken (es gibt nichts zu klicken). Kann keine Kopie der Anwendung erstellt werden,
   entscheidet der Support, ob ohne sie fortgefahren wird. Die Kopie wird am Ende immer zurückgeschrieben. Antwortet
   der Support nicht innerhalb von 60 Sekunden, wird das Update abgebrochen, bevor etwas geändert wird.
8. Klicken Sie auf **Freigabe beenden**, wenn Sie fertig sind.

Die Freigabe endet auch, wenn Sie das Gerät trennen, den Tab schließen oder die App beenden, nach 30 Minuten ohne
Aktivität (ein Banner warnt Sie 2 Minuten vorher, **Weiter freigeben** verlängert sie) und spätestens nach 8 Stunden.

<a id="what-support-sees"></a>
## Was der Support sieht

**Jeder, der den Code kennt, sieht die Konsolenausgabe** ab dem Moment, in dem er sich verbindet, noch bevor Sie es
ihm erlauben. Lesen Sie den Code nur dem HARDWARIO-Support vor. Befehle und Aktionen brauchen immer Ihren Klick: Die
Konsolenausgabe ist sichtbar, aber ohne Ihre Erlaubnis kann niemand das Gerät steuern.

Die Sitzung läuft über eine verschlüsselte Verbindung über den HARDWARIO-Server (`terminal.hardwario.com`). Befehle
mit geheimen Argumenten und geheime Werte, die das Gerät ausgibt (etwa der Claim-Token), werden verborgen gesendet,
wie in der Konsolendatei.

Alles, was aus der Ferne geschieht, wird in die Konsolendatei geschrieben: Beginn und Ende der Freigabe, jeder Beitritt
und Ihre Antwort, jeder entfernte Befehl und jede Anfrage nach einer Aktion mit ihrem Ergebnis. Siehe
[Konsolendateien](console.md).

Für die Autovervollständigung des Supports sendet Ihr HARDWARIO Monitor dem Support die gelernte Liste der
Gerätebefehle, und wenn der Support die Unterbefehle eines Befehls braucht, liest er dessen Hilfe selbst. Diese
Hilfeabfragen erscheinen nicht in Ihrer Konsole; die Konsolendatei hält fest, wie viele es waren.

<a id="join-a-session"></a>
## Einer Sitzung beitreten (Support)

1. Klicken Sie auf der Seite **Gerät wählen** eines Tabs auf **Support-Sitzung beitreten…**.
2. Geben Sie den Code ein, den Ihnen der Kunde vorliest (Leerzeichen sind erlaubt), oder fügen Sie den Link ein. Ein
   Link zu einem anderen als dem eingestellten Server erfordert eine zusätzliche Bestätigung.
3. Ein neuer Tab öffnet sich mit dem Titel **Sitzung 482 913** und danach *CHESTER-M 2159018247 — remote* (oder *Sitzung
   482 913 — web Terminal*, wenn der Kunde das web Terminal verwendet). Bis der Kunde es Ihnen erlaubt, zeigt das Banner
   *Warten, bis der Kunde es Ihnen erlaubt…*. Die Live-Ausgabe kann bereits erscheinen.
4. Sobald es Ihnen erlaubt ist, erhalten Sie den Konsolenverlauf des Kunden und können wie gewohnt in der Konsole
   arbeiten: Verlauf, Suche, Filter und Ansicht speichern.
   Bei einem Kunden mit HARDWARIO Monitor verwenden Vorschläge und die Tab-Vervollständigung die Befehlsliste seines
   Geräts.
   Hexdump-Zeilen des Geräteprotokolls sind standardmäßig ausgeblendet, wie in einem lokalen Tab (Chip **H**).
5. Bei einem Kunden mit HARDWARIO Monitor bietet die Seite **Flashen** **Flashen anfragen** (eine Firmware-ID aus dem
   Katalog), **Löschen anfragen**, **Recover anfragen** und **Reset anfragen** für den Anwendungs- oder den LTE-Kern.
   Jede Anfrage wartet auf die Bestätigung des Kunden; das Ergebnis meldet der Monitor des Kunden. Bei einem Kunden
   mit dem Web-Terminal ist nur die Konsole verfügbar.
   Das Ergebnis nennt den Vorgang und die Uhrzeit, zum Beispiel *Gerät des Kunden zurückgesetzt (14:02).*, und wird
   auch als Zeile in die Konsole geschrieben. Ein Erfolg verschwindet, wenn Sie die Seite **Flashen** verlassen oder
   einen weiteren Vorgang anfragen; ein Fehler bleibt, bis Sie ihn schließen (×).
6. Mit **LTE-Modemkern** ausgewählt fordert **LTE-Firmware-Update anfragen** das geführte LTE-Update auf die Version
   an, die der HARDWARIO Monitor des Kunden empfiehlt (keine Firmware-ID). Der Kunde steckt das J-Link-Kabel zwischen
   dem APP- und dem LTE-Anschluss um; Sie sehen jeden Schritt (zum Beispiel *Warten auf den J-Link am LTE-Anschluss*)
   und das Ergebnis. Endet das Update, ohne die Anwendung zurückzuschreiben, ist das Ergebnis
   `remote.application_not_restored`.
   Bevor die Anwendung gelöscht wird, fragt die Karte Sie: *Die Anwendung auf dem CHESTER des Kunden wird gelöscht. …
   Fortfahren?* (**Fortfahren** · **Abbrechen**); kann keine Kopie der Anwendung erstellt werden, fragt sie *Die
   Anwendung konnte nicht kopiert werden. Ohne Kopie fortfahren?*. Nein, oder keine Antwort innerhalb von 60 Sekunden,
   bricht das Update ab, bevor etwas geändert wird. Die Kopie wird automatisch zurückgeschrieben. Ein Kunde mit einem
   älteren HARDWARIO Monitor beantwortet diese Fragen selbst.

Wenn der Kunde die Freigabe beendet, kann HARDWARIO Monitor das nicht immer erkennen: Der Server kündigt es nicht an.
Nach 30 Minuten ohne jede Aktivität weist das Banner darauf hin und bietet **Verlassen** an. Der Tab bleibt offen,
sodass Sie die Konsole noch speichern können.

<a id="command-line"></a>
## Kommandozeile

```
hwio remote host                          # share the J-Link console; prints the code and the link
hwio remote host --ble -a F1:22:33:44:55:66
hwio remote join 482913                   # interactive console
hwio remote join 482913 --command "info show"
hwio remote join 482913 --flash 0123456789abcdef0123456789abcdef
hwio remote join 482913 --reset --target lte
hwio remote join 482913 --lte-update      # das geführte LTE-Update (der Kunde steckt das Kabel um)
```

`hwio remote host` fragt im Terminal nach, bevor es einen Beitritt oder eine Aktion erlaubt. Ohne Terminal an stdin
werden Beitritte nie erlaubt. Siehe [Kommandozeile](../en/cli.md#remote-support-hwio-remote).

In einem Terminal stellt `hwio remote join --lte-update` Ihnen die Fragen des LTE-Updates mit `[y/N]`.

<a id="settings"></a>
## Einstellungen

**Einstellungen** › **Experte** › **Support-Server** (im [Modus](getting-started.md#modes) Experte) ändert den Server
(zum Beispiel einen Testserver). Es werden nur `https`-Adressen akzeptiert. Lassen Sie das Feld leer für
`https://terminal.hardwario.com`.
