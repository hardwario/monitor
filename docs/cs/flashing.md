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
| **Modul LoRaWAN** | modul Murata Type ABZ (CHESTER s LoRaWAN) | lora-modem pro CHESTER, nebo váš vlastní `.hex` / `.bin` |

Karta ukazuje firmware, který je teď v zařízení, pokud ho zařízení hlásí.

Karta vždy pracuje s aplikačním MCU. **LTE firmware** se z této stránky nenahrává: spustí průvodce [aktualizací
firmwaru LTE](#aktualizace-firmwaru-lte), který vám řekne, kdy přepojit kabel J-Linku. **Modul LoRaWAN** funguje
stejně: **Nahrát** otevře průvodce, který vám řekne, kdy přepojit kabel ([Modul LoRaWAN](#lorawan-module)).

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
| **Smazat vše…** | Smaže paměť flash, UICR i PIB (jen odborné akce) | ano |
| **Obnovit (recover)…** | Viz níže (jen odborné akce) | ano |

**Smazat vše…**, **Obnovit (recover)…** a údržba **LTE MCU** (volba **Aplikační MCU | LTE MCU** nad tlačítky) se
zobrazí jen se zapnutou volbou **Nastavení › Pokročilé › Zobrazit odborné akce údržby**, protože mohou smazat sériové
číslo a konfiguraci zařízení. U LTE MCU potřebují J-Link na konektoru LTE. U CHESTER s LoRaWAN nabízí volba také
**Modul LoRaWAN** ([Modul LoRaWAN](#lorawan-module)).

<a id="recover"></a>
### Obnovení (recover)

Když je MCU chráněný proti čtení (approtect), J-Link do něj nemůže číst ani zapisovat a flashování selže s chybou
[target.protected](troubleshooting.md#target.protected). **Obnovit (recover)…** ochranu odstraní tím, že MCU úplně
smaže, včetně UICR a PIB. U LTE MCU potom nahraje malý obraz, díky kterému zůstane MCU laditelný. Po obnovení firmware
znovu naflashujte. Obnovení se nikdy nespouští samo. Když vás průvodce řešením problémů u chráněného MCU pošle na
stránku **Nahrát FW**, zobrazí se tam **Obnovit (recover)…** jednou, i bez odborného nastavení.

<a id="lorawan-module"></a>
## Modul LoRaWAN

CHESTER s LoRaWAN má modul Murata Type ABZ (STM32L072CZ s rádiem LoRa) s vlastním programovacím konektorem
**LoRaWAN**. Modul má vlastní firmware, který HARDWARIO Monitor nahrává přes J-Link. Modul nemá v HARDWARIO Monitor
konzoli: jeho nastavení LoRaWAN (klíče, region, …) se zadává příkazy `lrw config …` v konzoli MCU aplikace.

**Firmware:** v kroku 1 zvolte **Modul LoRaWAN**:

- **lora-modem for CHESTER 1.4.1 (doporučeno)**: otevřený firmware LoRaWAN modemu od HARDWARIO, projekt
  [hardwario/lora-modem](https://github.com/hardwario/lora-modem), licence BSD-3-Clause. Není součástí HARDWARIO
  Monitor: aplikace soubor vydání při prvním použití stáhne z GitHubu, ověří jeho kontrolní součet SHA-256 a uloží ho
  do mezipaměti.
- **Použít vlastní soubor…**: soubor `.hex` nebo `.bin`; soubor `.bin` potřebuje **Počáteční adresu** (výchozí
  `0x08000000`).

Možnosti **Nejprve smazat celý čip** a **Zachovat datovou EEPROM** fungují jako u [čipů STM32](#stm32-chips). Když na
kartě **Aplikační firmware** zvolíte soubor lora-modem, stránka nabídne **Toto je firmware modulu LoRaWAN —
přepnout?**.

**Proč se přepojuje kabel:** MCU aplikace ovládá napájení a reset modulu, takže modul na svém konektoru odpovídá jen
tehdy, když je MCU aplikace zastavený. **Nahrát** proto otevře průvodce, podobně jako aktualizace firmwaru LTE:

1. **Konektor APP**: připojte J-Link ke konektoru APP.
2. **Zastavení aplikace**: MCU aplikace se resetuje a zůstane zastavený.
3. **Konektor LoRaWAN**: přepojte J-Link ke konektoru LoRaWAN. Průvodce pokračuje sám, jakmile modul odpoví.
4. **Nahrání**: firmware se zapíše do modulu a ověří se.
5. **Konektor APP**: přepojte J-Link zpět; MCU aplikace se znovu spustí a s ním i modul.
6. **Hotovo**: **Do modulu LoRaWAN je nahrán lora-modem for CHESTER 1.4.1.**; konzole se znovu připojí.

Během průvodce je konzole karty pozastavená (**Pozastaveno kvůli modulu LoRaWAN**). Zrušit lze, dokud průvodce čeká na
kabel. Po kroku 2 zůstane MCU aplikace zastavený, dokud CHESTER neresetujete nebo nevypnete a znovu nezapnete.

**Modul z výroby** má původní firmware Murata a ochranu proti čtení (RDP úroveň 1). V kroku 4 se pak průvodce nejprve
zeptá **Odemknout modul LoRaWAN?**: **Odemknout a smazat** ochranu odstraní a modul se celý smaže. Původní firmware
Murata v modulu se odstraní; nejde ho předem zálohovat a HARDWARIO Monitor ho neumí zapsat zpět. Po odemčení nahrávání
pokračuje.

**Údržba:** se zapnutou volbou **Nastavení › Pokročilé › Zobrazit odborné akce údržby** nabízí volba nad tlačítky
údržby také **Modul LoRaWAN** s akcemi **Smazat čip…** (paměť flash i datová EEPROM) a **Odemknout (smazat)…**.
Každá z nich spustí stejného průvodce s přepojením kabelu (krok 4 je pak **Smazání** nebo **Odemčení**) a nejprve se
zeptá. **Reset** modulu nabízen není: poslední krok průvodce znovu spustí MCU aplikace, a tím se restartuje i modul.

Na příkazové řádce: `hwio chester lrw` ([Příkazová řádka](../en/cli.md#lrw)).

<a id="stm32-chips"></a>
## Flashování čipů STM32

HARDWARIO Monitor přes J-Link nahrává také čipy STM32L0, například řídicí čip rozšiřující desky CHESTER.

**Připojení:** propojte piny SWD J-Linku s čipem: **SWDIO**, **SWCLK**, **GND** a **VTref** (napájecí napětí čipu,
podle kterého J-Link pozná, že je deska napájená). Deska potřebuje vlastní napájení. Na kartě **Nové zařízení** vyberte
programátor, v části **Zařízení** zvolte **Čip STM32**, v části **Čip** vyberte typ a stiskněte **Připojit**.

**Čip** nabízí podporované čipy STM32L0 podle kategorie a velikosti paměti flash, například **STM32L0 cat. 3, 64 KiB**;
nápověda uvádí čipy, které položka pokrývá. Čipy jedné kategorie se stejnou velikostí paměti flash mají stejné ID a
aplikace je nerozliší, proto je pokrývá jedna položka. Řada STM32L010 (value line) podporovaná není. Seznam vypíše
`hwio stm32 chips`.

**Stránka Nahrát FW:** krok 1 má jedinou kartu **Firmware pro &lt;čip&gt;**, která přijímá **soubor .hex nebo .bin**. Soubor
`.bin` neobsahuje adresy: zadejte jeho **Počáteční adresu** (výchozí `0x08000000`, začátek paměti flash). Možnosti:

- **Nejprve smazat celý čip**: před zápisem smaže paměť flash i datovou EEPROM. Ve výchozím stavu vypnuto; běžné
  nahrání smaže jen sektory, do kterých zapisuje.
- **Zachovat datovou EEPROM** (ve výchozím stavu zapnuto): záznamy datové EEPROM ze souboru se nezapíšou. Vypněte ji,
  když má firmware datovou EEPROM změnit.
- **Po nahrání zastavit procesor**.

Před zápisem HARDWARIO Monitor přečte ID čipu a velikost paměti flash. Když je připojený jiný čip, než jaký jste
zvolili, nahrávání skončí chybou [target.wrong_chip](troubleshooting.md#target.wrong_chip); pokud je nalezený čip v
seznamu, tlačítko **Použít &lt;nalezený čip&gt;** na něj kartu přepne.

**Ochrana proti čtení (RDP):** na úrovni 1 J-Link nemůže paměť flash číst ani zapisovat a nahrávání skončí chybou
[target.protected](troubleshooting.md#target.protected) s tlačítkem **Odemknout…**. Odemčení ochranu odstraní a čip se
pak celý smaže, včetně datové EEPROM. To nelze vrátit zpět a firmware chráněného čipu nejde předem zálohovat. Po
odemčení spusťte nahrávání znovu. Úroveň 2 je trvalá: čip už nejde přeprogramovat
([target.permanently_protected](troubleshooting.md#target.permanently_protected)).

**Údržba — &lt;čip&gt;** nabízí **Reset**, **Reset a zastavit**, **Smazat čip…** (paměť flash i datová EEPROM) a
**Odemknout (smazat)…**; poslední dvě akce se nejprve zeptají. Stránka **Konzole** karty uvádí, že čip v HARDWARIO
Monitor konzoli nemá.

### CHESTER-S1 (Clime IAQ)

Řídicí čip druhé desky CHESTER Clime IAQ (CHESTER-S1) je **STM32L0 cat. 3, 64 KiB**. HARDWARIO pro něj zatím nemá
připravený firmware a senzory bez jeho firmwaru nefungují: **před nahráním nebo smazáním ho zálohujte**.
`hwio stm32 info --chip stm32l0-cat3-64k` ukáže úroveň ochrany proti čtení; při RDP 0 uložte paměť flash a datovou
EEPROM pomocí J-Link Commanderu (`JLinkExe`, ve Windows `JLink.exe`):

```text
JLinkExe -device STM32L053R8 -if SWD -speed 4000 -autoconnect 1
J-Link> halt
J-Link> savebin s1-flash.bin 0x08000000 0x10000
J-Link> savebin s1-eeprom.bin 0x08080000 0x800
J-Link> exit
```

Obnovení: `hwio stm32 flash --chip stm32l0-cat3-64k s1-flash.bin`, potom
`hwio stm32 flash --chip stm32l0-cat3-64k --address 0x08080000 --allow-eeprom s1-eeprom.bin`.

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
hwio chester lrw flash                                    # lora-modem for CHESTER (the LoRaWAN module, guided)
hwio stm32 flash --chip stm32l0-cat3-64k firmware.hex     # an STM32 chip (hwio stm32 chips)
```

Viz [Příkazová řádka](../en/cli.md#flashing).
