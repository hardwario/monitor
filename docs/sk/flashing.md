# Nahrávanie firmvéru

[Dokumentácia](index.md) › Nahrávanie firmvéru

Stránka **Flash** (nahrávanie firmvéru, <kbd>Ctrl</kbd>+<kbd>2</kbd>) zapisuje firmvér do zariadenia CHESTER cez J-Link
a vymazáva, obnovuje a resetuje jeho MCU. Postup má tri kroky a spúšťa ho tlačidlo **Flash** v dolnej časti
(<kbd>Ctrl</kbd>+<kbd>Enter</kbd>). Keď je tlačidlo vypnuté, text vedľa neho uvádza dôvod, napríklad **Choose a
firmware file or enter a catalog ID**.

Počas nahrávania je konzola karty pozastavená a potom sa sama obnoví.

## 1. Čo chcete nahrať?

| Voľba | MCU | Firmvér |
|---|---|---|
| **Application firmware** | aplikačný MCU | súbor `.hex` alebo ID firmvéru z katalógu |
| **LTE firmware** | LTE MCU a jeho modem | LTE firmvér HARDWARIO alebo váš vlastný `.hex` / `.zip` |
| **LoRaWAN module** | modul Murata Type ABZ (CHESTER s LoRaWAN) | lora-modem pre CHESTER alebo váš vlastný `.hex` / `.bin` |

Karta zobrazuje firmvér, ktorý je teraz v zariadení, ak ho zariadenie hlási.

Karta vždy pracuje s aplikačným MCU. **LTE firmware** sa z tejto stránky nenahráva: spustí sprievodcu [aktualizáciou
LTE firmvéru](#aktualizácia-lte-firmvéru), ktorý vám povie, kedy prepojiť kábel J-Linku. **LoRaWAN module** funguje
rovnako: **Flash** otvorí sprievodcu, ktorý vám povie, kedy prepojiť kábel ([Modul LoRaWAN](#lorawan-module)).

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

Kým sprievodca beží, konzola karty je pozastavená (**Paused for the LTE firmware update**). Zrušiť sa dá kedykoľvek
okrem zápisu do MCU. Po kroku 3 sa sprievodca najprv spýta: aplikačný MCU je vymazaný a zariadenie nebeží, kým
nenahráte aplikáciu. Kópia zostane v priečinku `backups` ([Súkromie a uložené údaje](privacy.md)).

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
Na CHESTER s LoRaWAN ponúka voľba aj **LoRaWAN module** ([Modul LoRaWAN](#lorawan-module)).

<a id="recover"></a>
### Obnovenie

Keď je MCU chránený proti spätnému čítaniu (approtect), J-Link ho nemôže čítať ani zapisovať a nahrávanie zlyhá s
chybou [target.protected](troubleshooting.md#target.protected). **Recover…** (obnoviť) odstráni ochranu úplným
vymazaním MCU vrátane UICR a PIB. Na LTE MCU potom nahrá malý obraz, ktorý ponechá MCU dostupný pre ladenie. Po
obnovení firmvér znova nahrajte. Obnovenie sa nikdy nespustí samo. Keď vás sprievodca riešením problémov pri chránenom
MCU pošle na stránku **Flash**, **Recover…** sa tam zobrazí raz, v každom režime.

<a id="lorawan-module"></a>
## Modul LoRaWAN

CHESTER s LoRaWAN má modul Murata Type ABZ (STM32L072CZ s rádiom LoRa) s vlastným programovacím konektorom
**LoRaWAN**. Modul má vlastný firmvér, ktorý HARDWARIO Monitor nahráva cez J-Link. Modul nemá v HARDWARIO Monitor
konzolu: jeho nastavenia LoRaWAN (kľúče, región, …) sa zadávajú príkazmi `lrw config …` v konzole MCU aplikácie.

**Firmvér:** v kroku 1 zvoľte **LoRaWAN module** (modul LoRaWAN):

- **lora-modem for CHESTER 1.4.1 (recommended)** (odporúčané): otvorený firmvér LoRaWAN modemu od HARDWARIO, projekt
  [hardwario/lora-modem](https://github.com/hardwario/lora-modem), licencia BSD-3-Clause. Nie je súčasťou HARDWARIO
  Monitor: aplikácia pri prvom použití stiahne súbor vydania z GitHubu, overí jeho kontrolný súčet SHA-256 a uloží ho
  do vyrovnávacej pamäte.
- **Use your own file…** (použiť vlastný súbor): súbor `.hex` alebo `.bin`; súbor `.bin` potrebuje **Start address**
  (počiatočná adresa, predvolene `0x08000000`).

Voľby **Erase the whole chip first** a **Keep data EEPROM** fungujú ako pri [čipoch STM32](#stm32-chips). Keď na karte
**Application firmware** zvolíte súbor lora-modem, stránka ponúkne **This is LoRaWAN module firmware — switch?** (toto
je firmvér modulu LoRaWAN — prepnúť?).

**Prečo sa prepája kábel:** MCU aplikácie ovláda napájanie a reset modulu, takže modul na svojom konektore odpovedá
len vtedy, keď je MCU aplikácie zastavený. **Flash** preto otvorí sprievodcu, podobne ako aktualizácia LTE firmvéru:

1. **APP connector** (konektor APP): pripojte J-Link ku konektoru APP.
2. **Stop the application** (zastavenie aplikácie): MCU aplikácie sa resetuje a zostane zastavený.
3. **LoRaWAN connector** (konektor LoRaWAN): prepojte J-Link ku konektoru LoRaWAN. Sprievodca pokračuje sám, hneď ako
   modul odpovie.
4. **Flash** (nahratie): firmvér sa zapíše do modulu a overí sa.
5. **APP connector**: prepojte J-Link späť; MCU aplikácie sa znova spustí a s ním aj modul.
6. **Done** (hotovo): **LoRaWAN module flashed with lora-modem for CHESTER 1.4.1.**; konzola sa znova pripojí.

Počas sprievodcu je konzola karty pozastavená (**Paused for the LoRaWAN module**). Zrušiť sa dá, kým sprievodca čaká
na kábel. Po kroku 2 zostane MCU aplikácie zastavený, kým CHESTER neresetujete alebo nevypnete a znova nezapnete.

**Modul z výroby** má pôvodný firmvér Murata a ochranu proti čítaniu (RDP úroveň 1). V kroku 4 sa potom sprievodca
najprv opýta **Unlock the LoRaWAN module?** (odomknúť modul LoRaWAN?): **Unlock and erase** (odomknúť a vymazať)
odstráni ochranu a modul sa celý vymaže. Pôvodný firmvér Murata v module sa odstráni; nedá sa vopred zálohovať a
HARDWARIO Monitor ho nevie zapísať späť. Po odomknutí nahrávanie pokračuje.

**Údržba:** v [režime](getting-started.md#modes) **Expert** ponúka voľba nad tlačidlami
údržby aj **LoRaWAN module** s akciami **Erase chip…** (vymazať čip: flash pamäť aj dátovú EEPROM) a **Unlock
(erase)…** (odomknúť a vymazať). Každá z nich spustí rovnakého sprievodcu s prepojením kábla (krok 4 je potom **Erase**
alebo **Unlock**) a najprv sa opýta. **Reset** modulu sa neponúka: posledný krok sprievodcu znova spustí MCU aplikácie,
a tým sa reštartuje aj modul.

Na príkazovom riadku: `hwio chester lrw` ([Príkazový riadok](../en/cli.md#lrw)).

<a id="stm32-chips"></a>
## Nahrávanie firmvéru do čipov STM32

HARDWARIO Monitor cez J-Link nahráva aj čipy STM32L0, napríklad riadiaci čip rozširujúcej dosky CHESTER.

**Pripojenie:** prepojte piny SWD J-Linku s čipom: **SWDIO**, **SWCLK**, **GND** a **VTref** (napájacie napätie čipu,
podľa ktorého J-Link spozná, že je doska napájaná). Doska potrebuje vlastné napájanie. Na karte **New device** vyberte
programátor, v časti **Device** zvoľte **STM32 chip** (čip STM32), v časti **Chip** vyberte typ a stlačte **Connect**.

**Chip** ponúka podporované čipy STM32L0 podľa kategórie a veľkosti flash pamäte, napríklad **STM32L0 cat. 3, 64
KiB**; nápoveda uvádza čipy, ktoré položka pokrýva. Čipy jednej kategórie s rovnakou veľkosťou flash pamäte majú
rovnaké ID a aplikácia ich nerozlíši, preto ich pokrýva jedna položka. Rad STM32L010 (value line) podporovaný nie je.
Zoznam vypíše `hwio stm32 chips`.

**Stránka Flash:** krok 1 má jedinú kartu **Firmware for &lt;chip&gt;** (firmvér pre čip), ktorá prijíma **.hex or .bin
file** (súbor `.hex` alebo `.bin`). Súbor `.bin` neobsahuje adresy: zadajte jeho **Start address** (počiatočná adresa;
predvolene `0x08000000`, začiatok flash pamäte). Možnosti:

- **Erase the whole chip first** (najprv vymazať celý čip): pred zápisom vymaže flash pamäť aj dátovú EEPROM.
  Predvolene vypnuté; bežné nahrávanie vymaže len sektory, do ktorých zapisuje.
- **Keep data EEPROM** (zachovať dátovú EEPROM, predvolene zapnuté): záznamy dátovej EEPROM zo súboru sa nezapíšu.
  Vypnite ju, keď má firmvér dátovú EEPROM zmeniť.
- **Halt the CPU after flashing**.

Pred zápisom HARDWARIO Monitor prečíta ID čipu a veľkosť flash pamäte. Keď je pripojený iný čip, než aký ste zvolili,
nahrávanie skončí chybou [target.wrong_chip](troubleshooting.md#target.wrong_chip); ak je nájdený čip v zozname,
tlačidlo **Use &lt;found chip&gt;** (použiť nájdený čip) na neho kartu prepne.

**Ochrana proti čítaniu (RDP):** na úrovni 1 J-Link nemôže flash pamäť čítať ani zapisovať a nahrávanie skončí chybou
[target.protected](troubleshooting.md#target.protected) s tlačidlom **Unlock…** (odomknúť). Odomknutie ochranu
odstráni a čip sa potom celý vymaže vrátane dátovej EEPROM. To sa nedá vrátiť späť a firmvér chráneného čipu sa nedá
vopred zálohovať. Po odomknutí spustite nahrávanie znova. Úroveň 2 je trvalá: čip sa už nedá preprogramovať
([target.permanently_protected](troubleshooting.md#target.permanently_protected)).

Časť **Maintenance — &lt;chip&gt;** ponúka **Reset**, **Reset and halt**, **Erase chip…** (vymazať čip: flash pamäť aj
dátovú EEPROM) a **Unlock (erase)…** (odomknúť a vymazať); posledné dve akcie sa najprv opýtajú. Stránka **Console**
karty uvádza, že čip v HARDWARIO Monitor nemá konzolu.

### CHESTER-S1 (Clime IAQ)

Riadiaci čip druhej dosky CHESTER Clime IAQ (CHESTER-S1) je **STM32L0 cat. 3, 64 KiB**. HARDWARIO preň zatiaľ nemá
pripravený firmvér a senzory bez jeho firmvéru nefungujú: **pred nahrávaním alebo vymazaním ho zálohujte**.
`hwio stm32 info --chip stm32l0-cat3-64k` ukáže úroveň ochrany proti čítaniu; pri RDP 0 uložte flash pamäť a dátovú
EEPROM pomocou J-Link Commandera (`JLinkExe`, vo Windows `JLink.exe`):

```text
JLinkExe -device STM32L053R8 -if SWD -speed 4000 -autoconnect 1
J-Link> halt
J-Link> savebin s1-flash.bin 0x08000000 0x10000
J-Link> savebin s1-eeprom.bin 0x08080000 0x800
J-Link> exit
```

Obnovenie: `hwio stm32 flash --chip stm32l0-cat3-64k s1-flash.bin`, potom
`hwio stm32 flash --chip stm32l0-cat3-64k --address 0x08080000 --allow-eeprom s1-eeprom.bin`.

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
hwio chester lrw flash                                    # lora-modem for CHESTER (the LoRaWAN module, guided)
hwio stm32 flash --chip stm32l0-cat3-64k firmware.hex     # an STM32 chip (hwio stm32 chips)
```

Pozrite si [Príkazový riadok](../en/cli.md#flashing).
