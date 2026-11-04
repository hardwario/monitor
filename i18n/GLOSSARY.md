# Translation glossary

Terminology of the HARDWARIO Monitor GUI catalogs (`i18n/hwio_<lang>.ts`, ADR-0027, plan 0030). Use these terms
in new translations and corrections so that every screen speaks the same language. The German, Polish and Slovak
catalogs were made by an AI agent and have not yet been reviewed by native speakers (ADR-0209); reviewers who
change a term here should change it in the whole catalog.

## Rules for all languages

- Keep placeholders (`%1`, `%2`, `%n`, `%Ln`, `%L1`) exactly; reorder them as the grammar needs.
- Keep markup (`<b>`, `<a href=...>`, `<br>`), keyboard shortcut names (`Ctrl+F`), `&&`, file names, commands
  (`bluetoothctl`, `nrfutil`, `hwio ...`), paths and URLs untranslated.
- Never translate product and technology names: HARDWARIO, HARDWARIO Monitor, CHESTER, J-Link, SEGGER, nRF Util,
  Nordic, RTT, UICR, PIB, MCU, LTE, LoRaWAN, BLE, Bluetooth, MCP, USB, udev, GitHub, Zephyr, CPU, SWD.
- Ellipsis `…` on menu items that open a dialog stays; keep sentence punctuation of the source.
- Keep translations about as long as the English text in buttons, tabs, combo boxes and column headers.
- Address the user formally in German (`Sie`); in Polish use the informal 2nd-person singular imperative in
  messages and buttons (`Wybierz`, `Sprawdź`, `Połącz`; `Pan/Pani` is avoided); imperative plural in Slovak as in
  Czech (`Pripojte`, `Skontrolujte`).

## Terms

| English | Czech (cs) | German (de) | Polish (pl) | Slovak (sk) |
|---|---|---|---|---|
| device | zařízení | Gerät | urządzenie | zariadenie |
| probe (J-Link) | programátor | Programmieradapter | programator | programátor |
| flash (verb) | nahrát (firmware) | flashen / Firmware aufspielen | wgrać (firmware) | nahrať (firmvér) |
| flashing | nahrávání | Flashen | wgrywanie | nahrávanie |
| firmware | firmware | Firmware | firmware | firmvér |
| erase | smazat | löschen | wymazać | vymazať |
| recover | obnovit (recover) | wiederherstellen (Recover) | odzyskać (recover) | obnoviť (recover) |
| reset | resetovat | zurücksetzen (Reset) | zresetować | resetovať |
| console | konzole | Konsole | konsola | konzola |
| command | příkaz | Befehl | polecenie | príkaz |
| batch | dávka | Batch | plik wsadowy (wsad) | dávka |
| tab | karta | Tab | karta | karta |
| wizard | průvodce | Assistent | kreator | sprievodca |
| troubleshoot | řešit potíže | Fehlerbehebung | rozwiązywanie problemów | riešiť problémy |
| settings | nastavení | Einstellungen | ustawienia | nastavenia |
| connect / disconnect | připojit / odpojit | verbinden / trennen | połączyć / rozłączyć | pripojiť / odpojiť |
| pair / pairing | spárovat / párování | koppeln / Kopplung | sparować / parowanie | spárovať / párovanie |
| passkey | přístupový klíč | Passkey | klucz dostępu (passkey) | prístupový kľúč |
| catalog (firmware) | katalog | Katalog | katalog | katalóg |
| application firmware | aplikační firmware | Anwendungs-Firmware | firmware aplikacji | aplikačný firmvér |
| modem firmware | firmware modemu | Modem-Firmware | firmware modemu | firmvér modemu |
| library (J-Link) | knihovna | Bibliothek | biblioteka | knižnica |
| diagnostic report | diagnostická zpráva | Diagnosebericht | raport diagnostyczny | diagnostická správa |
| shortcut | klávesová zkratka | Tastenkürzel | skrót klawiszowy | klávesová skratka |
| history (commands) | historie | Verlauf | historia | história |
| filter | filtr | Filter | filtr | filter |
| log | log / záznam | Protokoll | dziennik | záznam |
| target (MCU) | cíl | Ziel | cel | cieľ |
| speed (kHz) | rychlost | Geschwindigkeit | prędkość | rýchlosť |
| update (app) | aktualizace | Update / Aktualisierung | aktualizacja | aktualizácia |
| cable (flat cable) | kabel (plochý kabel) | Kabel (Flachbandkabel) | kabel (taśma) | kábel (plochý kábel) |
| permission | oprávnění | Berechtigung | uprawnienie | oprávnenie |
| connector (APP / LTE) | konektor (APP / LTE) | Anschluss (APP-Anschluss) | złącze (APP / LTE) | konektor (APP / LTE) |
| application MCU / LTE MCU | MCU aplikace / LTE MCU | Anwendungs-MCU / LTE-MCU | MCU aplikacji / MCU LTE | MCU aplikácie / LTE MCU |
| guide (LTE update) | průvodce | Assistent | kreator | sprievodca |
| move the cable (to a connector) | přepojit | umstecken | przełożyć | prepojiť |
| expert maintenance actions | odborné akce údržby | Experten-Wartungsaktionen | zaawansowane czynności serwisowe | odborné akcie údržby |
| Copy / Copied to clipboard. | Kopírovat / Zkopírováno do schránky. | Kopieren / In die Zwischenablage kopiert. | Kopiuj / Skopiowano do schowka. | Kopírovať / Skopírované do schránky. |
| Cancel / Close / Retry | Zrušit / Zavřít / Zkusit znovu | Abbrechen / Schließen / Erneut versuchen | Anuluj / Zamknij / Spróbuj ponownie | Zrušiť / Zavrieť / Skúsiť znova |
| System (language/theme) | Systém | System | Systemowy | Systém |
