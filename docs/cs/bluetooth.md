# Bluetooth LE

[Dokumentace](index.md) › Bluetooth LE

CHESTER nabízí svůj příkazový shell přes Bluetooth LE (Nordic UART Service). HARDWARIO Monitor se k němu připojuje
v desktopové aplikaci a z příkazové řádky příkazem `hwio ble`; J-Link ani kabel nejsou potřeba.

Shell přes Bluetooth funguje stejně jako konzole přes J-Link: odesíláte příkazy a čtete jejich odpovědi a zprávy
logu, které firmware posílá, se zobrazují jako řádky logu.

## V desktopové aplikaci

1. Otevřete novou kartu (<kbd>Ctrl</kbd>+<kbd>T</kbd>) a přepněte **Připojit přes** na **Bluetooth**. Aplikace si volbu
   zapamatuje pro další kartu.
2. Seznam ukazuje zařízení CHESTER v okolí, nejsilnější signál první, a obnovuje se sám. Signál se průměruje za
   několik sekund a zařízení se posune výš, jen když je zřetelně silnější (o 6 dB), takže seznam zůstává klidný; když
   je ukazatel myši nad seznamem (nebo má řádek fokus klávesnice), pořadí se vůbec nemění a nová zařízení se přidávají
   na konec. Filtrujte ho podle názvu, nebo zaškrtněte **Zobrazit všechna zařízení**, abyste viděli všechna zařízení
   Bluetooth (zařízení CHESTER první). Zařízení, které používá jiná karta, je označené **Používá se (karta N)**; jeho
   volbou se přepnete na tuto kartu. Zařízení, které používá jiný program, je označené **Používá jiný program** a
   nelze ho zvolit.
3. Zvolte zařízení a stiskněte **Připojit** (nebo na něj dvakrát klikněte, nebo stiskněte <kbd>Enter</kbd>).

Karta funguje jako karta J-Link: konzole s návrhy, historií, hledáním, dávkovými soubory a soubory konzole
([Konzole](console.md)). Panel logu se zobrazí, jen když zařízení posílá zprávy logu. Co se liší:

- Flashování potřebuje J-Link: **Nahrát FW** je v kartě Bluetooth zakázané. Pro flashování zařízení otevřete kartu
  J-Link.
- **Automaticky znovu připojit** je v nabídce **⋯** lišty zařízení a ve výchozím stavu je vypnuté; aplikace si ho
  pamatuje pro každé zařízení.
- První připojení k zařízení, které ještě není spárované, ukáže **Čeká se na spárování — zadejte přístupový klíč
  v dialogu systému** a v případě potřeby **Spárovat** (Linux, Windows). Viz [Párování](#párování).
- Pokud zařízení nelze najít, **Vybrat jiné zařízení** vás vrátí do seznamu.

Karty Bluetooth se po restartu obnoví odpojené; stiskněte **Připojit**. Aplikace spustí Bluetooth, až když poprvé
otevřete zobrazení Bluetooth, takže v macOS se na přístup k Bluetooth zeptá teprve tehdy.

## Vyhledání zařízení (příkazová řádka)

```sh
hwio ble scan
```

Vyhledávání trvá 5 sekund (`--timeout S`, 1 až 60) a vypíše zařízení CHESTER v okolí: ta, jejichž inzerovaný název
obsahuje `CHESTER`. `--all` vypíše všechna zařízení Bluetooth.

Každé zařízení má **ID**:

- Linux a Windows: jeho adresa Bluetooth, například `F1:22:33:44:55:66`.
- macOS: UUID, které zařízení přidělí macOS. Není to adresa Bluetooth a platí jen na daném Macu.

## Připojení (příkazová řádka)

Zařízení zvolte podle jeho ID (`-a`) nebo podle části názvu (`--name`, nerozlišuje velikost písmen; musí odpovídat
právě jedno zařízení nalezené pětisekundovým vyhledáváním):

```sh
hwio ble console -a F1:22:33:44:55:66      # interactive console
hwio ble console --name "CHESTER Clime"
hwio ble command -a F1:22:33:44:55:66 "info show"
```

Připojení může trvat až 30 sekund; neúspěšné pokusy se v této době opakují. `ble command` vypíše odpověď a skončí
u výzvy shellu, nebo po `-t` sekundách (výchozí 3).

V interaktivní konzoli se klávesou <kbd>F4</kbd> připojíte znovu a <kbd>Ctrl</kbd>+<kbd>C</kbd> na prázdném řádku
nebo <kbd>Ctrl</kbd>+<kbd>D</kbd> konzoli ukončí. S `--auto-reconnect` se konzole po přerušení spojení znovu připojí,
dokud je zařízení v dosahu. Konzole zapisuje soubor konzole a uchovává historii příkazů stejně jako konzole přes
J-Link ([Konzole](console.md#soubory-konzole)).

K zařízení může být připojený jen jeden program. Pokud je k němu připojený jiný `hwio`, dostanete chybu
[ble.in_use_by_other_process](troubleshooting.md#ble.in_use_by_other_process).

## Párování

CHESTER chrání svůj shell přes Bluetooth přístupovým klíčem, proto se s ním počítač musí jednou spárovat (bond).
Přístupový klíč je 123456, pokud nebyl změněn při konfiguraci zařízení. HARDWARIO Monitor přístupový klíč nikdy
neukládá a nikdy ho za vás nezadává: zeptá se na něj operační systém.

| Systém | Jak |
|---|---|
| Linux | Při prvním připojení, tlačítkem **Spárovat** v aplikaci, nebo příkazem `hwio ble pair -a ID`. Na přístupový klíč se zeptá nastavení Bluetooth v desktopovém prostředí. |
| Windows | Při prvním připojení, tlačítkem **Spárovat** v aplikaci, nebo příkazem `hwio ble pair -a ID`. Na přístupový klíč se zeptá Windows. |
| macOS | Jen při prvním připojení; na přístupový klíč se zeptá macOS. `hwio ble pair` ani **Spárovat** v macOS nejsou k dispozici. |

Když zařízení čeká na přístupový klíč, čeká připojení až 60 sekund.

**Linux bez párovacího agenta.** Některá desktopová prostředí (GNOME) se na přístupový klíč ptají, jen když je otevřené
jejich nastavení Bluetooth. Pokud se nic nezeptá, dostanete chybu
[ble.no_pairing_agent](troubleshooting.md#ble.no_pairing_agent). Spárujte zařízení v systémovém nastavení Bluetooth,
nebo pomocí `bluetoothctl`:

```text
$ bluetoothctl
[bluetooth]# agent KeyboardOnly
[bluetooth]# default-agent
[bluetooth]# pair F1:22:33:44:55:66
```

Pokud párování selže poté, co byl přístupový klíč v zařízení změněn, odeberte zařízení v systémovém nastavení
Bluetooth a spárujte ho znovu ([ble.pairing_failed](troubleshooting.md#ble.pairing_failed)).

## Oprávnění a adaptér

- **Linux:** Bluetooth používá BlueZ. Pokud neběží, spusťte ho příkazem `sudo systemctl start bluetooth`.
- **macOS:** při prvním vyhledávání se macOS zeptá na přístup k Bluetooth: v aplikaci pro HARDWARIO Monitor,
  v příkazové řádce pro aplikaci, ze které `hwio` spouštíte (obvykle Terminál). Pokud jste přístup odmítli, povolte ho
  v **Nastavení systému → Soukromí a zabezpečení → Bluetooth**; v aplikaci chyba nabídne **Otevřít nastavení
  soukromí**.
- **Všechny systémy:** zapněte adaptér Bluetooth. `hwio doctor` ukáže, zda je připravený.

## JSON

S `--json` vypíše `ble scan` zařízení a `ble console` vypíše jeden řádek JSON na každý řádek konzole, stejně jako
konzole přes J-Link ([Příkazová řádka](../en/cli.md#json-output)).
