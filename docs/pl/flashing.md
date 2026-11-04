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

Karta pokazuje firmware, które jest obecnie w urządzeniu, o ile urządzenie je zgłasza.

Karta zawsze pracuje z MCU aplikacji. **LTE firmware** nie jest wgrywane z tej strony: uruchamia prowadzoną
[aktualizację firmware LTE](#aktualizacja-firmware-lte), która informuje, kiedy przełożyć kabel J-Link.

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

## Aktualizacja firmware LTE

W CHESTER-M MCU LTE ma własne złącze programowania, a firmware aplikacji resetuje MCU LTE, gdy modem nie odpowiada, co
przerywa wgrywanie. **Update LTE firmware…** (zaktualizuj firmware LTE; na karcie **LTE firmware** lub w banerze, gdy
J-Link jest na złączu LTE) prowadzi więc przez całą procedurę:

1. **Firmware**: firmware LTE od HARDWARIO lub własny plik.
2. **APP connector** (złącze APP): podłącz J-Link do złącza APP. Kreator przechodzi dalej sam, gdy MCU aplikacji
   odpowie; na niewłaściwym złączu wyświetla **This is the LTE connector. Move the cable to the APP connector.** (to
   jest złącze LTE, przełóż kabel do złącza APP).
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
| **Erase everything…** | Kasuje pamięć flash, UICR i PIB (tylko akcje eksperckie) | tak |
| **Recover…** | Zobacz niżej (tylko akcje eksperckie) | tak |

**Erase everything…**, **Recover…** i konserwacja **LTE MCU** (wybór **Application MCU | LTE MCU** nad przyciskami)
pojawiają się tylko przy włączonym **Settings › Advanced › Show expert maintenance actions** (pokaż eksperckie akcje
konserwacji), ponieważ mogą skasować numer seryjny i konfigurację urządzenia. W MCU LTE wymagają J-Link na złączu LTE.

### Odzyskiwanie (Recover)

Gdy MCU ma ochronę przed odczytem (approtect), J-Link nie może go odczytać ani zapisać, a wgrywanie kończy się błędem
[target.protected](troubleshooting.md#target.protected). **Recover…** usuwa ochronę, kasując MCU całkowicie, łącznie z
UICR i PIB. W MCU LTE następnie wgrywa mały obraz, który utrzymuje możliwość debugowania MCU. Po recover wgraj
firmware ponownie. Recover nigdy nie uruchamia się samoczynnie. Gdy przewodnik rozwiązywania problemów dla chronionego
MCU kieruje na stronę **Flash**, **Recover…** jest tam wyświetlane jeden raz, także bez ustawienia eksperckiego.

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
```

Zobacz [Wiersz poleceń](../en/cli.md#flashing).
