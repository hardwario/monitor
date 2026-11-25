# Začíname

[Dokumentácia](index.md) › Začíname

HARDWARIO Monitor zobrazuje konzolu a log zariadení HARDWARIO CHESTER a nahráva do nich firmvér, a to cez sondu
SEGGER J-Link. Konzola funguje aj cez Bluetooth LE ([Bluetooth LE](bluetooth.md)). MCP server pre AI asistentov je
dostupný v príkazovom riadku (`hwio`, pozrite si [MCP server](../en/mcp.md)).

Skôr než začnete, nainštalujte aplikáciu a SEGGER J-Link Software ([Inštalácia](install.md)).

## Zariadenie

CHESTER má dva MCU, ktoré sa nazývajú **ciele** (targets):

| Cieľ | MCU | Čo môžete robiť |
|---|---|---|
| `app` | aplikačný MCU (nRF52840) | konzola a log (RTT), nahratie firmvéru, vymazanie, reset, obnovenie |
| `lte` | LTE MCU (nRF9160 alebo nRF9151) | nahratie LTE firmvéru a firmvéru modemu, vymazanie, reset, obnovenie; nemá konzolu |

Pripojte J-Link k počítaču cez USB a k programovaciemu konektoru zariadenia CHESTER a CHESTER napájajte z batérií
alebo cez jeho napájací vstup USB-C.

## Pripojenie

1. Spustite **HARDWARIO Monitor**. Prvá karta, **New device** (nové zariadenie), zobrazuje **Choose a device to
   connect** (vyberte zariadenie na pripojenie) so sondami J-Link pripojenými k tomuto počítaču. Zoznam sa obnovuje
   každé dve sekundy.
2. Vyberte sondu. Sonda, ktorá je už otvorená v inej karte, má označenie **In use (tab N)**; jej výberom sa prepnete
   do tejto karty. Sonda, ktorú drží iný program, má označenie **In use by another program** a nedá sa vybrať.
3. V časti **Device** ponechajte **CHESTER** a zvoľte cieľ: **app** alebo **lte**.
4. Stlačte **Connect** (pripojiť).

Lišta zariadenia v hornej časti karty zobrazuje sondu, cieľ a stav: **Connecting…**, **Connected · 3.30 V** (s
napätím cieľa), **Reconnecting…** alebo **Disconnected — reason**. **Connect** a **Disconnect**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>) sú vpravo a <kbd>F4</kbd> pripojí znova.

Ak sa nenájde žiadna sonda, stránka zobrazí **No J-Link probe found** a informáciu, či sa našiel J-Link Software.
Skontrolujte kábel USB a v Linuxe pravidlo udev ([Inštalácia](install.md#prístup-k-j-linku-cez-usb)). `hwio doctor`
skontroluje všetko z toho.

**Previesť ma** na tejto stránke alebo **Neviete, ako pripojiť? Previesť ma** pod zoznamom
programátorov otvorí sprievodcu pripojením: pomôže vybrať programátor, pripojiť kábel a napájanie, skontroluje
pripojenie a potom kartu pripojí.

### Možnosti pripojenia

**⋯** (**Advanced**, rozšírené) v lište zariadenia obsahuje možnosti tejto sondy. HARDWARIO Monitor si ich pamätá podľa
sériového čísla sondy. Uplatnia sa pri ďalšom pripojení.

| Možnosť | Predvolené | Význam |
|---|---|---|
| **Automatic speed** | zapnuté | Rýchlosť zvolí J-Link. Vypnuté: **J-Link speed (kHz)**, 5 až 50000 |
| **RTT latency (ms)** | 0 (hodnota zariadenia, pre CHESTER 50 ms) | Ako často sa konzola číta |
| **Auto-reconnect** | zapnuté | Po výpadku spojenia sa pripojí znova sám |
| **Reset on connect** | vypnuté | Pri pripájaní resetuje zariadenie |
| **Save console to log file** | zapnuté | Zapisuje [súbor konzoly](console.md#súbory-konzoly) |

Sondy bez vlastných možností používajú predvolené hodnoty v **Settings → J-Link**.

## Karty

Každé zariadenie je karta; otvorených môže byť až 8. <kbd>Ctrl</kbd>+<kbd>T</kbd> alebo **+** otvorí novú kartu,
<kbd>Ctrl</kbd>+<kbd>W</kbd> ju zatvorí (rovnako aj kliknutie stredným tlačidlom na kartu), <kbd>Ctrl</kbd>+<kbd>Tab</kbd>
a <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> prepínajú. Ak v karte prebieha operácia, pred zatvorením sa aplikácia
najprv spýta.

Lišta vľavo prepína stránky karty: **Console** (konzola, <kbd>Ctrl</kbd>+<kbd>1</kbd>) a **Flash** (nahrávanie
firmvéru, <kbd>Ctrl</kbd>+<kbd>2</kbd>). **Settings** (nastavenia, <kbd>Ctrl</kbd>+<kbd>,</kbd>) sa otvorí ako samostatná
karta.

Aplikácia sa vždy spustí s jednou kartou **New device** (**Choose a device to connect**); karty z minulého spustenia
sa znova neotvárajú.

Naraz beží len jedno okno aplikácie. Jej opätovné spustenie prenesie otvorené okno do popredia.

## Stránky

- [Konzola](console.md): shell a log zariadenia, vyhľadávanie, filter, história, dávkové súbory, súbory konzoly.
- [Nahrávanie firmvéru](flashing.md): aplikačný a LTE firmvér, vymazanie, obnovenie, reset.

## Nastavenia

| Sekcia | Čo obsahuje |
|---|---|
| **General** | **Mode** (Basic, Advanced, Expert), **Language** (System, English, Čeština, Deutsch, Polski, Slovenčina), **Theme** (System, Light, Dark) |
| **Console** | veľkosť písma, časové značky, zalamovanie riadkov, návrhy príkazov, história príkazov, súbory konzoly a ich priečinok |
| **J-Link** | knižnica J-Link (**Automatic** alebo **Custom path**) a predvolené možnosti pripojenia |
| **nRF Util** | program nRF Util (**Automatic** alebo **Custom path**), potrebný len pre firmvér LTE modemu |
| **AI assistants (MCP)** | [server MCP](../en/mcp.md) pre AI asistentov |
| **Expert** | [server podpory](remote-support.md#settings) |
| **About** | verzia (s tlačidlom **Copy** pre podporu), [kontrola aktualizácií](privacy.md#kontrola-aktualizácií), príkaz `hwio` tejto inštalácie, licencie, odkazy |

Zmeny sa uplatnia okamžite. V slovenskom rozhraní sa režimy volajú **Základný**, **Pokročilý** a **Expertný**.

<a id="modes"></a>
### Režimy

**Mode** (režim) určuje, koľko toho nastavenia ukazujú. Pri prvom spustení (a raz po aktualizácii z verzie 2.0.0-rc.3
alebo staršej) sa v prvom kroku sprievodcu nastavením, **Vitajte**, aplikácia opýta **Ako chcete HARDWARIO Monitor
používať?**: kliknite na režim alebo ho vyberte pomocou <kbd>↑</kbd> / <kbd>↓</kbd>. Predvybraný je **Advanced**; ak ste
v staršej verzii mali zapnutú voľbu **Show expert maintenance actions**, je to **Expert**. Vybraný režim sa uloží, nech
krok opustíte akokoľvek: **Ďalej**, **Preskočiť nastavenie**, **Zrušiť**, **×** alebo <kbd>Esc</kbd>. Zmeniť ho môžete
kedykoľvek v **Settings › General › Mode**.

| Zobrazené | Basic | Advanced | Expert |
|---|---|---|---|
| **General**: Mode, Language, Theme | ✓ | ✓ | ✓ |
| **Console**: veľkosť písma, časové značky, zalamovanie riadkov, súbory konzoly, priečinok záznamov | ✓ | ✓ | ✓ |
| **Console**: návrhy príkazov, načítanie zoznamu príkazov, **Remember commands**, **Clear history** | | ✓ | ✓ |
| **J-Link**: Library, Default speed, Auto-reconnect | | ✓ | ✓ |
| **J-Link**: RTT latency, Reset on connect | | | ✓ |
| **nRF Util**, **AI assistants (MCP)** | | ✓ | ✓ |
| **Expert**: Support server | | | ✓ |
| **About**, v režimoch Advanced a Expert s **Include pre-releases** | ✓ | ✓ | ✓ |
| **Flash › Maintenance**: **Erase everything…**, **Recover…**, LTE MCU | | | ✓ |

Režim nastavenia len skrýva: skryté nastavenie si ponechá svoju hodnotu a ďalej sa používa (napríklad vlastná cesta ku
knižnici J-Link v režime Basic). Keď chyba alebo sprievodca otvorí nastavenie, ktoré režim skrýva (**Open Settings**),
zobrazí sa počas tejto návštevy s poznámkou "Shown because it was needed."; režim sa nezmení.

## Pomocník

**Help** (pomocník) v dolnej časti lišty:

- **Keyboard shortcuts (F1)**: všetky klávesové skratky aplikácie (aj <kbd>Ctrl</kbd>+<kbd>/</kbd>).
- **Sprievodca nastavením…**: skontroluje J-Link Software, prístup k USB (Linux), nRF Util a programátory.
- **Pripojiť zariadenie…**: sprievodca pripojením (programátor, kábel, napájanie, živá kontrola, pripojenie).
- **Riešiť problémy s pripojením…**: vyberte problém; sprievodca spustí kontroly a prevedie vás krok za krokom.
- **Nahrať firmvér (so sprievodcom)…**: nahrá aplikačný firmvér krok za krokom ([Nahrávanie](flashing.md)).

Tieto tri pracujú s aktuálnou kartou zariadenia (z karty nastavení: s naposledy zobrazenou kartou zariadenia). Pracujú
so zariadením pripojeným k tomuto počítaču: na karte [relácie podpory](remote-support.md#join-a-session) sa neotvoria a
oznámia to.
- **Create diagnostic report…**: súbor zip pre podporu HARDWARIO ([Súkromie](privacy.md#diagnostická-správa)).
- **Documentation ↗**: tieto stránky na GitHube, pre nainštalovanú verziu aplikácie a v jazyku aplikácie (anglicky,
  ak stránka nie je preložená).

<a id="guides-and-views"></a>
<a id="sprievodcovia-a-ďalšie-zobrazenia"></a>
### Sprievodcovia a ďalšie zobrazenia

Sprievodcovia (sprievodca nastavením, sprievodca pripojením, sprievodca riešením problémov, nahrávanie so sprievodcom a
**Update LTE firmware…**), diagnostická správa, klávesové skratky, aktualizácia firmvéru J-Linku, **Join support
session…** a **Share with support…** sa otvoria v ploche stránky karty vedľa lišty; karty a lišta zostanú. **×** alebo
<kbd>Esc</kbd> ich zatvorí a vráti sa predchádzajúca stránka. Prepnutie na inú kartu ich zatvorí tiež, okrem času, keď
pracujú (zber diagnostickej správy, zápis firmvéru J-Linku).

Kým sprievodca beží, môžete otvoriť ďalšie stránky karty, napríklad konzolu: **Wizard** (sprievodca) hore v lište (a
malý odznak na karte zariadenia) vráti sprievodcu tam, kde bol. Každá karta zariadenia môže mať vlastného sprievodcu.
Kým je otvorený sprievodca pripojením, sprievodca riešením problémov alebo nahrávanie so sprievodcom, **Vybrať iné
zariadenie** tejto karty počká, kým ho nezatvoríte: zariadenie karty vyberá sprievodca. Pripojenie a odpojenie zostanú
dostupné a sprievodca ich sleduje.

Sprievodca nastavením, kým je otvorený, zakrýva plochu stránky všetkých kariet. Sprievodca, ktorý beží v karte,
pokračuje pod ním (jeho odznak zostane) a znova sa ukáže, keď sa sprievodca nastavením zatvorí. Ostatní sprievodcovia sa
spustia až po zatvorení sprievodcu nastavením (**Najprv dokončite alebo zatvorte sprievodcu nastavením.**).

Keď niečo zlyhá, aplikácia ukáže, čo sa stalo, pravdepodobnú príčinu (**Probably:**) a čo robiť (**What to do:**).
**Details** zobrazí kód chyby; [Riešenie problémov](troubleshooting.md) uvádza všetky kódy.
**Learn more ↗** otvorí v prehliadači opis kódu chyby na tejto stránke.

## Príkazový riadok

Všetko, čo aplikácia robí s J-Linkom, je dostupné aj ako príkaz:

```sh
hwio jlink list                       # the connected probes
hwio chester app console              # the console of the application MCU
hwio chester app flash firmware.hex
hwio doctor                           # check the installation
```

Pozrite si [Príkazový riadok](../en/cli.md).
