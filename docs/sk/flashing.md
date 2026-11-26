# Nahrávanie firmvéru

[Dokumentácia](index.md) › Nahrávanie firmvéru

Stránka **Flash** (nahrávanie firmvéru, <kbd>Ctrl</kbd>+<kbd>2</kbd>) zapisuje firmvér do zariadenia CHESTER cez J-Link
a vymazáva, obnovuje a resetuje jeho MCU. Postup má tri kroky a spúšťa ho tlačidlo **Flash** v dolnej časti
(<kbd>Ctrl</kbd>+<kbd>Enter</kbd>). Keď je tlačidlo vypnuté, text vedľa neho uvádza dôvod, napríklad **Choose a
firmware file or enter a catalog ID**.

Počas nahrávania je konzola karty pozastavená a potom sa sama obnoví.

Na nahranie aplikačného firmvéru so sprievodcom (programátor, kontrola kábla, súbor, potvrdenie) použite **Pomocník** ›
**Nahrať firmvér (so sprievodcom)…**, **Previesť ma** na tejto stránke alebo **Nahrať firmvér so sprievodcom** na
stránke výberu zariadenia. Sprievodca sa otvorí v ploche stránky karty ([Sprievodcovia a ďalšie
zobrazenia](getting-started.md#guides-and-views)); na karte relácie podpory nie je k dispozícii.

## 1. Čo chcete nahrať?

| Voľba | MCU | Firmvér |
|---|---|---|
| **Application firmware** | aplikačný MCU | súbor `.hex` alebo ID firmvéru z katalógu |
| **LTE firmware** | LTE MCU a jeho modem | LTE firmvér HARDWARIO alebo váš vlastný `.hex` / `.zip` |

Karta zobrazuje firmvér, ktorý je teraz v zariadení, ak ho zariadenie hlási.

Karta vždy pracuje s aplikačným MCU. **LTE firmware** sa z tejto stránky nenahráva: spustí sprievodcu [aktualizáciou
LTE firmvéru](#aktualizácia-lte-firmvéru), ktorý vám povie, kedy prepojiť kábel J-Linku.

## 2. Firmvér

### Aplikačný firmvér

- Pustite súbor `.hex` kamkoľvek na stránku alebo stlačte **Browse…** (prehľadávať, <kbd>Ctrl</kbd>+<kbd>O</kbd>).
- Alebo zadajte **ID firmvéru z katalógu** (32 znakov, `0`–`9` a `a`–`f`) a stlačte **Download** (stiahnuť).
  Stránka zobrazí názov firmvéru, verziu a dátum nahratia (alebo **Unknown firmware — it can still be flashed.**).
  **Find firmware IDs ↗** otvorí zoznam katalógových aplikácií CHESTER na docs.hardwario.com.

Stiahnutý firmvér sa uchováva vo vyrovnávacej pamäti a znova sa nesťahuje. **Recent** (nedávne) uvádza posledných 10
súborov a ID.

### LTE firmvér

Vyberáte ho v prvom kroku [aktualizácie LTE firmvéru](#aktualizácia-lte-firmvéru):

- **HARDWARIO LTE firmware 1.7.0 (recommended)**: LTE aplikácia a firmvér modemu, ktoré HARDWARIO otestovalo so
  zariadením CHESTER. Sťahuje sa z docs.hardwario.com a overuje sa jeho kontrolný súčet.
- **Use your own file…** (použiť vlastný súbor): aplikačný `.hex`, balík CHESTER LTE `.zip` (firmvér modemu a LTE
  aplikácia) alebo `.zip` s firmvérom modemu od spoločnosti Nordic. HARDWARIO Monitor sa pozerá na obsah súboru, nie
  na jeho názov, a odmietne zip, ktorý nie je ani jedným z nich.

Firmvér modemu potrebuje **nRF Util** ([Inštalácia](install.md#nordic-nrf-util-len-firmvér-lte-modemu)). Aktualizácia
modemu trvá 1–3 minúty a konzola je počas nej odpojená.

Ak zvolíte súbor druhého druhu, stránka ponúkne prepnutie (**This is LTE firmware — switch to LTE firmware?**).

## 3. Možnosti

- **Halt the CPU after flashing** (zastaviť CPU po nahratí): CPU nespustí nový firmvér, kým ho neresetujete alebo
  nenecháte bežať.
- **Allow writing UICR** (povoliť zápis UICR): zobrazí sa len vtedy, keď obraz obsahuje záznamy UICR; pozrite nižšie.

### UICR a blok informácií o produkte

UICR je malá oblasť flash pamäte s konfiguráciou zariadenia. Na zariadení CHESTER obsahuje aj **Product Information
Block** (PIB, blok informácií o produkte): sériové číslo, variant a revíziu hardvéru, claim token a BLE passkey.

- Nahrávanie firmvéru nikdy nezapisuje UICR, ak to nepovolíte (**Allow writing UICR** alebo `--allow-uicr`
  v príkazovom riadku), a nikdy nemení PIB.
- **Erase application** zachová UICR aj PIB.
- **Erase everything** a **Recover** vymažú aj PIB. HARDWARIO Monitor ho nedokáže zapísať späť, preto sa najprv
  spýtajú.

`hwio chester app pib read` zobrazí PIB.

## Priebeh a výsledok

Karta priebehu zobrazuje krok (**Connecting**, **Erasing**, **Programming**, **Verifying**, **Resetting** a pri modeme
**Updating the modem**), bajty a čas. **Cancel** (zrušiť) ho zastaví; zrušené alebo prerušené nahrávanie zanechá
zariadenie bez funkčného firmvéru, kým ho znova nenahráte.

Na konci stránka zobrazí napríklad **Firmware flashed and verified in 12 s.** s tlačidlami **Open console** a
**Flash again**.

<a id="lte-firmware-update"></a>
## Aktualizácia LTE firmvéru

Na zariadení CHESTER-M má LTE MCU vlastný programovací konektor a aplikačný firmvér resetuje LTE MCU, keď modem
neodpovedá, čo preruší nahrávanie. **Update LTE firmware…** (aktualizovať LTE firmvér; na karte **LTE firmware** alebo
v banneri, keď je J-Link na konektore LTE) vás preto prevedie celým postupom:

1. **Firmware**: LTE firmvér HARDWARIO alebo váš vlastný súbor.
2. **APP connector** (konektor APP): pripojte J-Link ku konektoru APP. Sprievodca pokračuje sám, keď aplikačný MCU
   odpovie; na nesprávnom konektore zobrazí **This is the LTE connector. Move the cable to the APP connector.** (toto
   je konektor LTE, prepojte kábel ku konektoru APP).
   Skôr než sa čokoľvek zmení, krok **Ready to update the LTE MCU** (pripravené na aktualizáciu LTE MCU) povie, čo sa
   stane: aplikácia sa vymaže, prepojíte kábel J-Link na konektor LTE a na konci späť na APP a aplikácia sa zapíše
   späť. **Continue** (pokračovať) to spustí; **Cancel** (zrušiť) sprievodcu ukončí bez akejkoľvek zmeny.
3. **Stop the application** (zastavenie aplikácie): uloží sa kópia aplikačného firmvéru a potom sa aplikácia vymaže
   (**Erase application**: UICR aj PIB zostanú). Ak sa kópia nedá vytvoriť, napríklad pri MCU chránenom proti spätnému
   čítaniu, môžete zvoliť **Continue without a copy** (pokračovať bez kópie) a na konci nahrať aplikáciu sami.
4. **LTE connector** (konektor LTE): prepojte J-Link ku konektoru LTE.
5. **Update** (aktualizácia): aktualizuje sa LTE MCU a jeho modem (pri balíku v dvoch krokoch).
6. **APP connector**: prepojte J-Link späť.
7. **Application** (aplikácia): zapíše sa späť kópia (predvolené), alebo zvoľte firmvér z katalógu alebo zo súboru.
8. **Done** (hotovo): aplikácia znova beží a konzola sa znova pripojí.

Sprievodca nahradí plochu stránky karty; medzitým sa môžete pozrieť do konzoly a vrátiť sa cez **Wizard** (sprievodca)
hore v lište ([Sprievodcovia a ďalšie zobrazenia](getting-started.md#sprievodcovia-a-ďalšie-zobrazenia)). Kým
sprievodca beží, konzola karty je pozastavená (**Paused for the LTE firmware update**). Zrušiť sa dá kedykoľvek
okrem zápisu do MCU. Po kroku 3 sa sprievodca najprv spýta: **The application MCU is erased. Write the application
back first?** (Aplikačný MCU je vymazaný. Najprv zapísať aplikáciu späť?)

- **Write back** (zapísať späť): sprievodca požiada o J-Link na konektore APP, zapíše kópiu späť a skončí správou
  **LTE firmware not updated. The application was written back.** (LTE firmvér nebol aktualizovaný, aplikácia bola
  zapísaná späť).
- **Leave it erased** (nechať vymazané): sprievodca sa zavrie; zariadenie nebeží, kým nenahráte aplikáciu. Správa
  povie, kde je kópia.
- **Back** (späť): sprievodca pokračuje.

Bez kópie (prázdna aplikácia) Cancel oznámi, že aplikačný MCU je vymazaný. Kópie zostávajú v priečinku `backups`
([Súkromie a uložené údaje](privacy.md)); uchováva sa 10 najnovších.

<a id="application-copies"></a>
### Kópie aplikácie

Keď priečinok `backups` obsahuje kópie pripojeného zariadenia CHESTER, karta **Application firmware** (aplikačný
firmvér; stránka **Flash** a krok 7 sprievodcu) zobrazí **Application copy from** (kópia aplikácie z) s dátumom a
časom najnovšej kópie; šípka vypíše všetky kópie zariadenia (až 10). Zvolená kópia sa načíta ako súbor. **Open backups
folder** (otvoriť priečinok záloh) priečinok otvorí a dialóg výberu súboru v ňom začína. Kým sériové číslo zariadenia
nie je známe (napríklad keď je aplikácia vymazaná), karta vypíše všetky kópie s ich sériovými číslami. V termináli:
`hwio chester app backups` a `hwio chester app flash --backup latest` ([Príkazový riadok](../en/cli.md)).

Keď krok sprievodcu zlyhá, panel chyby namiesto tlačidiel stránky Flash zobrazí **Troubleshooting** (riešenie
problémov, dokumentácia chyby); **Try again** (skúsiť znova) a **Cancel** (zrušiť) v dolnej časti sprievodcu
pokračujú.

## Údržba

Časť **Maintenance — application MCU** v dolnej časti stránky je predvolene zbalená:

| Akcia | Funkcia | Pýta sa najprv |
|---|---|---|
| **Reset** | Resetuje MCU | nie |
| **Reset and halt** | Resetuje MCU a ponechá CPU zastavený | nie |
| **Erase application** | Vymaže firmvér; zachová UICR aj PIB (len aplikačný MCU) | nie |
| **Erase everything…** | Vymaže flash pamäť, UICR aj PIB (len v režime Expert) | áno |
| **Recover…** | Pozrite nižšie (len v režime Expert) | áno |

**Erase everything…**, **Recover…** a údržba **LTE MCU** (voľba **Application MCU | LTE MCU** nad tlačidlami) sa
zobrazia len v [režime](getting-started.md#modes) **Expert** (**Settings › General › Mode**), pretože môžu vymazať
sériové číslo a konfiguráciu zariadenia. Na LTE MCU potrebujú J-Link na konektore LTE.

<a id="recover"></a>
### Obnovenie

Keď je MCU chránený proti spätnému čítaniu (approtect), J-Link ho nemôže čítať ani zapisovať a nahrávanie zlyhá s
chybou [target.protected](troubleshooting.md#target.protected). **Recover…** (obnoviť) odstráni ochranu úplným
vymazaním MCU vrátane UICR a PIB. Na LTE MCU potom nahrá malý obraz, ktorý ponechá MCU dostupný pre ladenie. Po
obnovení firmvér znova nahrajte. Obnovenie sa nikdy nespustí samo. Keď vás sprievodca riešením problémov pri chránenom
MCU pošle na stránku **Flash**, **Recover…** sa tam zobrazí raz, v každom režime.

## Príkazový riadok

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

Pozrite si [Príkazový riadok](../en/cli.md#flashing).
