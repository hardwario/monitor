# Flashování

[Dokumentace](index.md) › Flashování

Stránka **Nahrát FW** (<kbd>Ctrl</kbd>+<kbd>2</kbd>) zapisuje firmware do zařízení CHESTER přes J-Link a maže,
obnovuje (recover) a resetuje jeho MCU. Postup má tři kroky a spouští ho tlačítko **Nahrát** ve spodní části stránky
(<kbd>Ctrl</kbd>+<kbd>Enter</kbd>). Když je tlačítko zakázané, text vedle něj uvádí proč, například **Vyberte soubor s
firmwarem nebo zadejte ID z katalogu**.

Během flashování je konzole karty pozastavená a poté se sama obnoví.

## 1. Co chcete nahrát?

| Volba | MCU | Firmware |
|---|---|---|
| **Aplikační firmware** | aplikační MCU | soubor `.hex` nebo ID firmwaru z katalogu |
| **LTE firmware** | LTE MCU a jeho modem | LTE firmware HARDWARIO, nebo váš vlastní `.hex` / `.zip` |

Karta ukazuje firmware, který je teď v zařízení, pokud ho zařízení hlásí.

Karta vždy pracuje s aplikačním MCU. **LTE firmware** se z této stránky nenahrává: spustí průvodce [aktualizací
firmwaru LTE](#aktualizace-firmwaru-lte), který vám řekne, kdy přepojit kabel J-Linku.

## 2. Firmware

### Aplikační firmware

- Přetáhněte soubor `.hex` kamkoli na stránku, nebo stiskněte **Procházet…** (<kbd>Ctrl</kbd>+<kbd>O</kbd>).
- Nebo napište **ID firmwaru z katalogu** (32 znaků, `0`–`9` a `a`–`f`) a stiskněte **Stáhnout**. Stránka
  ukáže název firmwaru, verzi a datum nahrání (nebo **Neznámý firmware — přesto ho lze nahrát.**).
  **Najít ID firmwaru ↗** otevře seznam katalogových aplikací CHESTER na docs.hardwario.com.

Stažený firmware se uchovává v mezipaměti a znovu se nestahuje. **Nedávné** obsahuje posledních 10 souborů a ID.

### LTE firmware

Volíte ho v prvním kroku [aktualizace firmwaru LTE](#aktualizace-firmwaru-lte):

- **HARDWARIO LTE firmware 1.7.0 (doporučeno)**: aplikace LTE a firmware modemu, které HARDWARIO otestoval se
  zařízením CHESTER. Stahuje se z docs.hardwario.com a jeho kontrolní součet se ověřuje.
- **Použít vlastní soubor…**: aplikační `.hex`, balíček CHESTER LTE `.zip` (firmware modemu a aplikace LTE), nebo
  `.zip` s firmwarem modemu od Nordic. HARDWARIO Monitor posuzuje obsah souboru, ne jeho název, a zip, který není ani
  jedním z nich, odmítne.

Firmware modemu potřebuje **nRF Util** ([Instalace](install.md#nordic-nrf-util-jen-firmware-modemu-lte)). Aktualizace
modemu trvá 1–3 minuty a konzole je mezitím odpojená.

Pokud vyberete soubor jiného druhu, stránka nabídne přepnutí (**Toto je firmware LTE — přepnout na firmware LTE?**).

## 3. Možnosti

- **Po nahrání zastavit procesor**: procesor nespustí nový firmware, dokud ho neresetujete nebo nenecháte běžet.
- **Povolit zápis UICR**: zobrazí se, jen když obraz obsahuje záznamy UICR; viz níže.

### UICR a blok informací o produktu

UICR je malá oblast paměti flash s konfigurací zařízení. U CHESTER obsahuje také **blok informací o produktu** (PIB,
Product Information Block): sériové číslo, variantu a revizi hardwaru, claim token a přístupový klíč BLE.

- Flashování nikdy nezapisuje do UICR, pokud to nepovolíte (**Povolit zápis UICR**, nebo `--allow-uicr` v příkazové
  řádce), a nikdy nemění PIB.
- **Smazat aplikaci** zachová UICR i PIB.
- **Smazat vše** a **Obnovit** smažou i PIB. HARDWARIO Monitor ho nedokáže zapsat zpět, proto se nejprve zeptají.

`hwio chester app pib read` ukáže PIB.

## Průběh a výsledek

Karta průběhu ukazuje krok (**Připojování**, **Mazání**, **Programování**, **Ověřování**, **Reset** a u modemu
**Aktualizace modemu**), bajty a čas. **Zrušit** ho zastaví; zrušené nebo přerušené flashování zanechá zařízení bez
funkčního firmwaru, dokud ho znovu nenaflashujete.

Na konci stránka ukáže například **Firmware nahrán a ověřen za 12 s.** s tlačítky **Otevřít konzoli** a
**Nahrát znovu**.

<a id="lte-firmware-update"></a>
## Aktualizace firmwaru LTE

U CHESTER-M má LTE MCU vlastní programovací konektor a aplikační firmware resetuje LTE MCU, když modem neodpovídá, což
flashování přeruší. **Aktualizovat firmware LTE…** (na kartě **LTE firmware**, nebo v banneru, když je J-Link na
konektoru LTE) vás proto provede celým postupem:

1. **Firmware**: LTE firmware HARDWARIO, nebo váš vlastní soubor.
2. **Konektor APP**: připojte J-Link ke konektoru APP. Průvodce pokračuje sám, jakmile aplikační MCU odpoví; na
   nesprávném konektoru ukáže **Toto je konektor LTE. Přepojte kabel ke konektoru APP.**
   Než se cokoli změní, krok **Připraveno k aktualizaci LTE MCU** řekne, co se stane: aplikace se smaže, přepojíte
   kabel J-Link na konektor LTE a na konci zpět na APP a aplikace se zapíše zpět. **Pokračovat** to spustí;
   **Zrušit** průvodce ukončí a nic se nezmění.
3. **Zastavení aplikace**: uloží se kopie aplikačního firmwaru a potom se aplikace smaže (**Smazat aplikaci**: UICR i
   PIB zůstanou). Pokud kopii nelze vytvořit, například u MCU chráněného proti čtení, můžete **Pokračovat bez kopie**
   a na konci nahrát aplikaci sami.
4. **Konektor LTE**: přepojte J-Link ke konektoru LTE.
5. **Aktualizace**: aktualizuje se LTE MCU a jeho modem (u balíčku ve dvou krocích).
6. **Konektor APP**: přepojte J-Link zpět.
7. **Aplikace**: zapíše se zpět kopie (výchozí), nebo zvolte firmware z katalogu či ze souboru.
8. **Hotovo**: aplikace znovu běží a konzole se znovu připojí.

Během průvodce je konzole karty pozastavená (**Pozastaveno kvůli aktualizaci firmwaru LTE**). Zrušit lze kdykoli kromě
zápisu do MCU. Po kroku 3 se průvodce nejprve zeptá: aplikační MCU je smazaný a zařízení neběží, dokud nenahrajete
aplikaci. Kopie zůstane ve složce `backups` ([Soukromí a uložená data](privacy.md)).

## Údržba

**Údržba — MCU aplikace** ve spodní části stránky je ve výchozím stavu sbalená:

| Akce | Funkce | Ptá se předem |
|---|---|---|
| **Reset** | Resetuje MCU | ne |
| **Reset a zastavit** | Resetuje MCU a ponechá procesor zastavený | ne |
| **Smazat aplikaci** | Smaže firmware; zachová UICR i PIB (jen aplikační MCU) | ne |
| **Smazat vše…** | Smaže paměť flash, UICR i PIB (jen režim Expertní) | ano |
| **Obnovit (recover)…** | Viz níže (jen režim Expertní) | ano |

**Smazat vše…**, **Obnovit (recover)…** a údržba **LTE MCU** (volba **Aplikační MCU | LTE MCU** nad tlačítky) se
zobrazí jen v [režimu](getting-started.md#modes) **Expertní** (**Nastavení › Obecné › Režim**), protože mohou smazat
sériové číslo a konfiguraci zařízení. U LTE MCU potřebují J-Link na konektoru LTE.

<a id="recover"></a>
### Obnovení (recover)

Když je MCU chráněný proti čtení (approtect), J-Link do něj nemůže číst ani zapisovat a flashování selže s chybou
[target.protected](troubleshooting.md#target.protected). **Obnovit (recover)…** ochranu odstraní tím, že MCU úplně
smaže, včetně UICR a PIB. U LTE MCU potom nahraje malý obraz, díky kterému zůstane MCU laditelný. Po obnovení firmware
znovu naflashujte. Obnovení se nikdy nespouští samo. Když vás průvodce řešením problémů u chráněného MCU pošle na
stránku **Nahrát FW**, zobrazí se tam **Obnovit (recover)…** jednou, v každém režimu.

## Příkazová řádka

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

Viz [Příkazová řádka](../en/cli.md#flashing).
