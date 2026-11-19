# Wgrywanie firmware

[Dokumentacja](index.md) › Wgrywanie firmware

Strona **Flash** (wgrywanie, <kbd>Ctrl</kbd>+<kbd>2</kbd>) zapisuje firmware do urządzenia CHESTER przez J-Link
oraz kasuje, odzyskuje (recover) i resetuje jego MCU. Działa w trzech krokach, a przycisk **Flash** na dole
rozpoczyna wgrywanie (<kbd>Ctrl</kbd>+<kbd>Enter</kbd>). Gdy przycisk jest nieaktywny, tekst obok niego podaje
przyczynę, na przykład **Choose a firmware file or enter a catalog ID**.

Podczas wgrywania konsola karty jest wstrzymana i po zakończeniu wznawia się samoczynnie.

## 1. Co chcesz wgrać?

| Wybór | MCU | Firmware |
|---|---|---|
| **Application firmware** | MCU aplikacji | plik `.hex` lub identyfikator firmware z katalogu |
| **LTE firmware** | MCU LTE i jego modem | firmware LTE od HARDWARIO lub własny plik `.hex` / `.zip` |
| **LoRaWAN module** | moduł Murata Type ABZ (CHESTER z LoRaWAN) | lora-modem dla CHESTER lub własny plik `.hex` / `.bin` |

Karta pokazuje firmware, które jest obecnie w urządzeniu, o ile urządzenie je zgłasza.

Karta zawsze pracuje z MCU aplikacji. **LTE firmware** nie jest wgrywane z tej strony: uruchamia prowadzoną
[aktualizację firmware LTE](#aktualizacja-firmware-lte), która informuje, kiedy przełożyć kabel J-Link. **LoRaWAN
module** działa tak samo: **Flash** otwiera kreator, który informuje, kiedy przełożyć kabel ([Moduł
LoRaWAN](#lorawan-module)).

## 2. Firmware

### Firmware aplikacji

- Upuść plik `.hex` w dowolnym miejscu strony lub naciśnij **Browse…** (przeglądaj, <kbd>Ctrl</kbd>+<kbd>O</kbd>).
- Albo wpisz **identyfikator firmware z katalogu** (32 znaki, `0`–`9` i `a`–`f`) i naciśnij **Download** (pobierz).
  Strona pokaże nazwę firmware, wersję i datę przesłania (lub **Unknown firmware — it can still be flashed.**).
  **Find firmware IDs ↗** otwiera listę aplikacji katalogowych CHESTER w serwisie docs.hardwario.com.

Pobrane firmware jest przechowywane w pamięci podręcznej i nie jest pobierane ponownie. **Recent** wyświetla
ostatnich 10 plików i identyfikatorów.

### Firmware LTE

Wybierasz je w pierwszym kroku [aktualizacji firmware LTE](#aktualizacja-firmware-lte):

- **HARDWARIO LTE firmware 1.7.0 (recommended)**: aplikacja LTE i firmware modemu przetestowane przez HARDWARIO
  z CHESTER. Jest pobierane z docs.hardwario.com, a jego suma kontrolna jest weryfikowana.
- **Use your own file…**: plik `.hex` aplikacji, pakiet CHESTER LTE `.zip` (firmware modemu i aplikacja LTE) lub
  plik `.zip` z firmware modemu Nordic. HARDWARIO Monitor sprawdza zawartość pliku, a nie jego nazwę, i odrzuca plik
  zip, który nie jest żadnym z nich.

Firmware modemu wymaga **nRF Util** ([Instalacja](install.md#nordic-nrf-util-tylko-firmware-modemu-lte)).
Aktualizacja modemu trwa 1–3 minuty, a konsola jest w tym czasie rozłączona.

Jeśli wybierzesz plik innego rodzaju, strona zaproponuje przełączenie (**This is LTE firmware — switch to LTE
firmware?**).

## 3. Opcje

- **Halt the CPU after flashing**: CPU nie uruchomi nowego firmware, dopóki go nie zresetujesz lub nie pozwolisz
  mu działać.
- **Allow writing UICR**: wyświetlane tylko wtedy, gdy obraz zawiera rekordy UICR; zobacz niżej.

### UICR i blok informacji o produkcie

UICR to mały obszar pamięci flash z konfiguracją urządzenia. W CHESTER zawiera on także **Product Information
Block** (blok informacji o produkcie, PIB): numer seryjny, wariant i rewizję sprzętu, claim token i passkey BLE.

- Wgrywanie nigdy nie zapisuje UICR, chyba że na to zezwolisz (**Allow writing UICR** lub `--allow-uicr` w wierszu
  poleceń), i nigdy nie zmienia PIB.
- **Erase application** zachowuje UICR i PIB.
- **Erase everything** i **Recover** kasują także PIB. HARDWARIO Monitor nie potrafi go zapisać z powrotem, dlatego
  najpierw pytają o potwierdzenie.

`hwio chester app pib read` wyświetla PIB.

## Postęp i wynik

Karta postępu pokazuje krok (**Connecting**, **Erasing**, **Programming**, **Verifying**, **Resetting** oraz, dla
modemu, **Updating the modem**), bajty i czas. **Cancel** zatrzymuje operację; anulowane lub przerwane wgrywanie
pozostawia urządzenie bez działającego firmware, dopóki nie wgrasz go ponownie.

Na koniec strona wyświetla na przykład **Firmware flashed and verified in 12 s.** z przyciskami **Open console** i
**Flash again**.

<a id="lte-firmware-update"></a>
## Aktualizacja firmware LTE

W CHESTER-M MCU LTE ma własne złącze programowania, a firmware aplikacji resetuje MCU LTE, gdy modem nie odpowiada, co
przerywa wgrywanie. **Update LTE firmware…** (zaktualizuj firmware LTE; na karcie **LTE firmware** lub w banerze, gdy
J-Link jest na złączu LTE) prowadzi więc przez całą procedurę:

1. **Firmware**: firmware LTE od HARDWARIO lub własny plik.
2. **APP connector** (złącze APP): podłącz J-Link do złącza APP. Kreator przechodzi dalej sam, gdy MCU aplikacji
   odpowie; na niewłaściwym złączu wyświetla **This is the LTE connector. Move the cable to the APP connector.** (to
   jest złącze LTE, przełóż kabel do złącza APP).
   Zanim cokolwiek zostanie zmienione, krok **Ready to update the LTE MCU** (gotowe do aktualizacji MCU LTE) mówi, co
   się stanie: aplikacja zostanie wymazana, przełożysz kabel J-Link do złącza LTE, a na końcu z powrotem do APP, i
   aplikacja zostanie zapisana z powrotem. **Continue** (kontynuuj) to rozpoczyna; **Cancel** (anuluj) kończy kreator
   bez żadnych zmian.
3. **Stop the application** (zatrzymanie aplikacji): zapisywana jest kopia firmware aplikacji, a następnie aplikacja
   jest kasowana (**Erase application**: UICR i PIB pozostają). Jeśli nie da się wykonać kopii, na przykład gdy MCU ma
   ochronę przed odczytem, możesz wybrać **Continue without a copy** (kontynuuj bez kopii) i na końcu samodzielnie
   wgrać aplikację.
4. **LTE connector** (złącze LTE): przełóż J-Link do złącza LTE.
5. **Update** (aktualizacja): MCU LTE i jego modem są aktualizowane (w dwóch krokach dla pakietu).
6. **APP connector**: przełóż J-Link z powrotem.
7. **Application** (aplikacja): kopia jest zapisywana z powrotem (domyślnie) albo wybierz firmware z katalogu lub z
   pliku.
8. **Done** (gotowe): aplikacja znowu działa, a konsola łączy się ponownie.

Podczas działania kreatora konsola karty jest wstrzymana (**Paused for the LTE firmware update**). Anulowanie działa
zawsze, z wyjątkiem zapisu do MCU. Po kroku 3 kreator najpierw pyta: MCU aplikacji jest skasowane i urządzenie nie
działa, dopóki nie wgrasz aplikacji. Kopia pozostaje w folderze `backups` ([Prywatność i przechowywane
dane](privacy.md)).

## Konserwacja

Sekcja **Maintenance — application MCU** na dole strony jest domyślnie zwinięta:

| Akcja | Działanie | Pyta najpierw |
|---|---|---|
| **Reset** | Resetuje MCU | nie |
| **Reset and halt** | Resetuje MCU i utrzymuje zatrzymany CPU | nie |
| **Erase application** | Kasuje firmware; zachowuje UICR i PIB (tylko MCU aplikacji) | nie |
| **Erase everything…** | Kasuje pamięć flash, UICR i PIB (tylko w trybie Expert) | tak |
| **Recover…** | Zobacz niżej (tylko w trybie Expert) | tak |

**Erase everything…**, **Recover…** i konserwacja **LTE MCU** (wybór **Application MCU | LTE MCU** nad przyciskami)
pojawiają się tylko w [trybie](getting-started.md#modes) **Expert** (**Settings › General › Mode**), ponieważ mogą
skasować numer seryjny i konfigurację urządzenia. W MCU LTE wymagają J-Link na złączu LTE.
W CHESTER z LoRaWAN wybór zawiera też **LoRaWAN module** ([Moduł LoRaWAN](#lorawan-module)).

<a id="recover"></a>
### Odzyskiwanie (Recover)

Gdy MCU ma ochronę przed odczytem (approtect), J-Link nie może go odczytać ani zapisać, a wgrywanie kończy się błędem
[target.protected](troubleshooting.md#target.protected). **Recover…** usuwa ochronę, kasując MCU całkowicie, łącznie z
UICR i PIB. W MCU LTE następnie wgrywa mały obraz, który utrzymuje możliwość debugowania MCU. Po recover wgraj
firmware ponownie. Recover nigdy nie uruchamia się samoczynnie. Gdy przewodnik rozwiązywania problemów dla chronionego
MCU kieruje na stronę **Flash**, **Recover…** jest tam wyświetlane jeden raz, w każdym trybie.

<a id="lorawan-module"></a>
## Moduł LoRaWAN

CHESTER z LoRaWAN ma moduł Murata Type ABZ (STM32L072CZ z radiem LoRa) z własnym złączem programowania **LoRaWAN**.
Moduł ma własny firmware, który HARDWARIO Monitor wgrywa przez J-Link. Moduł nie ma konsoli w HARDWARIO Monitor: jego
ustawienia LoRaWAN (klucze, region, …) ustawia się poleceniami `lrw config …` w konsoli MCU aplikacji.

**Firmware:** w kroku 1 wybierz **LoRaWAN module** (moduł LoRaWAN):

- **lora-modem for CHESTER 1.4.1 (recommended)** (zalecane): otwarty firmware modemu LoRaWAN od HARDWARIO, projekt
  [hardwario/lora-modem](https://github.com/hardwario/lora-modem), licencja BSD-3-Clause. Nie jest częścią HARDWARIO
  Monitor: aplikacja przy pierwszym użyciu pobiera plik wydania z GitHub, sprawdza jego sumę kontrolną SHA-256 i
  przechowuje go w pamięci podręcznej.
- **Use your own file…** (użyj własnego pliku): plik `.hex` lub `.bin`; plik `.bin` wymaga podania **Start address**
  (adres początkowy, domyślnie `0x08000000`).

Opcje **Erase the whole chip first** i **Keep data EEPROM** działają jak w przypadku [układów STM32](#stm32-chips).
Gdy na karcie **Application firmware** wybierzesz plik lora-modem, strona zaproponuje **This is LoRaWAN module
firmware — switch?** (to jest firmware modułu LoRaWAN — przełączyć?).

**Dlaczego trzeba przełożyć kabel:** MCU aplikacji steruje zasilaniem i resetem modułu, więc moduł odpowiada na swoim
złączu tylko wtedy, gdy MCU aplikacji jest zatrzymany. Dlatego **Flash** otwiera kreator, podobnie jak aktualizacja
firmware LTE:

1. **APP connector** (złącze APP): podłącz J-Link do złącza APP.
2. **Stop the application** (zatrzymanie aplikacji): MCU aplikacji zostaje zresetowany i pozostaje zatrzymany.
3. **LoRaWAN connector** (złącze LoRaWAN): przełóż J-Link do złącza LoRaWAN. Kreator przechodzi dalej sam, gdy tylko
   moduł odpowie.
4. **Flash** (wgrywanie): firmware zostaje zapisany do modułu i zweryfikowany.
5. **APP connector**: przełóż J-Link z powrotem; MCU aplikacji uruchamia się ponownie, a wraz z nim moduł.
6. **Done** (gotowe): **LoRaWAN module flashed with lora-modem for CHESTER 1.4.1.**; konsola łączy się ponownie.

Podczas działania kreatora konsola karty jest wstrzymana (**Paused for the LoRaWAN module**). Anulować można, dopóki
kreator czeka na kabel. Po kroku 2 MCU aplikacji pozostaje zatrzymany do zresetowania CHESTER albo jego wyłączenia i
ponownego włączenia.

**Moduł fabryczny** ma oryginalny firmware Murata i ochronę przed odczytem (RDP poziom 1). W kroku 4 kreator najpierw
pyta wtedy **Unlock the LoRaWAN module?** (odblokować moduł LoRaWAN?): **Unlock and erase** (odblokuj i wymaż) usuwa
ochronę, a moduł wymazuje się całkowicie. Oryginalny firmware Murata w module zostaje usunięty; nie da się go
wcześniej zarchiwizować, a HARDWARIO Monitor nie potrafi zapisać go z powrotem. Po odblokowaniu wgrywanie jest
kontynuowane.

**Konserwacja:** w [trybie](getting-started.md#modes) **Expert** wybór nad przyciskami
konserwacji zawiera też **LoRaWAN module** z akcjami **Erase chip…** (wymaż układ: pamięć flash i EEPROM danych) i
**Unlock (erase)…** (odblokuj i wymaż). Każda z nich uruchamia ten sam kreator z przekładaniem kabla (krok 4 to wtedy
**Erase** lub **Unlock**) i najpierw pyta. Modułu nie resetuje się osobno (**Reset**): ostatni krok kreatora ponownie
uruchamia MCU aplikacji, co restartuje także moduł.

W wierszu poleceń: `hwio chester lrw` ([Wiersz poleceń](../en/cli.md#lrw)).

<a id="stm32-chips"></a>
## Wgrywanie firmware do układów STM32

HARDWARIO Monitor wgrywa przez J-Link także firmware do układów STM32L0, na przykład do kontrolera płytki
rozszerzeń CHESTER.

**Podłączenie:** połącz piny SWD J-Link z układem: **SWDIO**, **SWCLK**, **GND** i **VTref** (napięcie zasilania
układu, po którym J-Link rozpoznaje, że płytka jest zasilana). Płytka potrzebuje własnego zasilania. W karcie **New
device** wybierz programator, następnie w sekcji **Device** wybierz **STM32 chip** (układ STM32), w sekcji **Chip**
wybierz układ i naciśnij **Connect**.

**Chip** zawiera obsługiwane układy STM32L0 według kategorii i rozmiaru pamięci flash, na przykład **STM32L0 cat. 3,
64 KiB**; podpowiedź wymienia układy, które obejmuje pozycja. Układy jednej kategorii z tym samym rozmiarem pamięci
flash mają ten sam identyfikator i aplikacja nie potrafi ich odróżnić, dlatego obejmuje je jedna pozycja. Seria
STM32L010 (value line) nie jest obsługiwana. Listę wypisuje `hwio stm32 chips`.

**Strona Flash:** krok 1 ma jedną kartę, **Firmware for &lt;chip&gt;** (firmware dla układu), która przyjmuje **.hex or
.bin file** (plik `.hex` lub `.bin`). Plik `.bin` nie zawiera adresów: podaj jego **Start address** (adres
początkowy; domyślnie `0x08000000`, początek pamięci flash). Opcje:

- **Erase the whole chip first** (najpierw wymaż cały układ): przed zapisem wymazuje pamięć flash i EEPROM danych.
  Domyślnie wyłączone; zwykłe wgrywanie wymazuje tylko zapisywane sektory.
- **Keep data EEPROM** (zachowaj EEPROM danych, domyślnie włączone): rekordy EEPROM danych z pliku nie są zapisywane.
  Wyłącz tę opcję, gdy firmware ma zmienić EEPROM danych.
- **Halt the CPU after flashing**.

Przed zapisem HARDWARIO Monitor odczytuje identyfikator układu i rozmiar pamięci flash. Gdy podłączony jest inny układ
niż wybrany, wgrywanie kończy się błędem [target.wrong_chip](troubleshooting.md#target.wrong_chip); jeśli znaleziony
układ jest na liście, przycisk **Use &lt;found chip&gt;** (użyj znalezionego układu) przełącza na niego kartę.

**Ochrona przed odczytem (RDP):** na poziomie 1 J-Link nie może odczytać ani zapisać pamięci flash, a wgrywanie kończy
się błędem [target.protected](troubleshooting.md#target.protected) z przyciskiem **Unlock…** (odblokuj). Odblokowanie
usuwa ochronę, a układ następnie wymazuje się całkowicie, łącznie z EEPROM danych. Tego nie można cofnąć, a firmware
chronionego układu nie da się wcześniej zarchiwizować. Po odblokowaniu uruchom wgrywanie ponownie. Poziom 2 jest
trwały: układu nie można już przeprogramować
([target.permanently_protected](troubleshooting.md#target.permanently_protected)).

Sekcja **Maintenance — &lt;chip&gt;** zawiera **Reset**, **Reset and halt**, **Erase chip…** (wymaż układ: pamięć flash i
EEPROM danych) i **Unlock (erase)…** (odblokuj i wymaż); dwie ostatnie akcje najpierw pytają. Strona **Console**
karty informuje, że układ nie ma konsoli w HARDWARIO Monitor.

### CHESTER-S1 (Clime IAQ)

Kontrolerem drugiej płytki CHESTER Clime IAQ (CHESTER-S1) jest **STM32L0 cat. 3, 64 KiB**. HARDWARIO nie ma jeszcze
dla niego przygotowanego firmware, a bez jego firmware czujniki nie działają: **zrób kopię zapasową przed wgraniem
lub wymazaniem**. `hwio stm32 info --chip stm32l0-cat3-64k` pokazuje poziom ochrony przed odczytem; przy RDP 0 zapisz
pamięć flash i EEPROM danych za pomocą J-Link Commander (`JLinkExe`, w Windows `JLink.exe`):

```text
JLinkExe -device STM32L053R8 -if SWD -speed 4000 -autoconnect 1
J-Link> halt
J-Link> savebin s1-flash.bin 0x08000000 0x10000
J-Link> savebin s1-eeprom.bin 0x08080000 0x800
J-Link> exit
```

Przywracanie: `hwio stm32 flash --chip stm32l0-cat3-64k s1-flash.bin`, a następnie
`hwio stm32 flash --chip stm32l0-cat3-64k --address 0x08080000 --allow-eeprom s1-eeprom.bin`.

## Wiersz poleceń

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

Zobacz [Wiersz poleceń](../en/cli.md#flashing).
