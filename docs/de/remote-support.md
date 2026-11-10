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
5. Befehle des Supports erscheinen in der Konsole, jeweils mit einer vorangestellten Zeile *— Remote: <Befehl> —*.
6. Bittet der Support darum, das Gerät zu flashen, zu löschen, wiederherzustellen oder zurückzusetzen, sehen Sie genau,
   worum er bittet, zum Beispiel *Der Support bittet darum, die Firmware CHESTER Clime v3.2.1 (ID …) in den
   Anwendungskern zu flashen.* **Ablehnen** ist die Vorgabe; eine Anfrage ohne Antwort wird nach 60 Sekunden
   abgelehnt. Die Firmware stammt nur aus dem HARDWARIO-Katalog, und Ihr HARDWARIO Monitor lädt sie herunter und
   prüft sie.
7. Klicken Sie auf **Freigabe beenden**, wenn Sie fertig sind.

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
5. Bei einem Kunden mit HARDWARIO Monitor bietet die Seite **Flashen** **Flashen anfragen** (eine Firmware-ID aus dem
   Katalog), **Löschen anfragen**, **Recover anfragen** und **Reset anfragen** für den Anwendungs- oder den LTE-Kern.
   Jede Anfrage wartet auf die Bestätigung des Kunden; das Ergebnis meldet der Monitor des Kunden. Bei einem Kunden
   mit dem Web-Terminal ist nur die Konsole verfügbar.

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
```

`hwio remote host` fragt im Terminal nach, bevor es einen Beitritt oder eine Aktion erlaubt. Ohne Terminal an stdin
werden Beitritte nie erlaubt. Siehe [Kommandozeile](../en/cli.md#remote-support-hwio-remote).

<a id="settings"></a>
## Einstellungen

**Einstellungen** › **Erweitert** › **Support-Server** ändert den Server (zum Beispiel einen Testserver). Es werden nur
`https`-Adressen akzeptiert. Lassen Sie das Feld leer für `https://terminal.hardwario.com`.
