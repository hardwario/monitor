# Pierwsze kroki

[Dokumentacja](index.md) › Pierwsze kroki

HARDWARIO Monitor wyświetla konsolę i log urządzeń HARDWARIO CHESTER i wgrywa ich firmware przez sondę SEGGER
J-Link. Konsola działa także przez Bluetooth LE ([Bluetooth LE](bluetooth.md)). Serwer MCP dla asystentów AI jest
dostępny w wierszu poleceń (`hwio`, zobacz [Serwer MCP](../en/mcp.md)).

Przed rozpoczęciem zainstaluj aplikację i SEGGER J-Link Software ([Instalacja](install.md)).

## Urządzenie

CHESTER ma dwa MCU, nazywane **celami** (targets):

| Cel | MCU | Co można zrobić |
|---|---|---|
| `app` | MCU aplikacji (nRF52840) | konsola i log (RTT), wgrywanie firmware, kasowanie, reset, recover |
| `lte` | MCU LTE (nRF9160 lub nRF9151) | wgrywanie firmware LTE i firmware modemu, kasowanie, reset, recover; nie ma konsoli |

Podłącz J-Link do komputera przez USB i do złącza programowania CHESTER, a CHESTER zasil z baterii lub przez jego
wejście zasilania USB-C.

## Połączenie

1. Uruchom **HARDWARIO Monitor**. Pierwsza karta, **New device** (nowe urządzenie), wyświetla **Choose a device to
   connect** (wybierz urządzenie do połączenia) z sondami J-Link podłączonymi do tego komputera. Lista odświeża się
   co dwie sekundy.
2. Wybierz sondę. Sonda otwarta już w innej karcie jest oznaczona **In use (tab N)**; jej wybranie przełącza na tę
   kartę. Sonda zajęta przez inny program jest oznaczona **In use by another program** i nie można jej wybrać.
3. W sekcji **Device** pozostaw **CHESTER** i wybierz cel: **app** lub **lte**.
4. Naciśnij **Connect** (połącz).

Pasek urządzenia u góry karty pokazuje sondę, cel i stan: **Connecting…**, **Connected · 3.30 V** (z napięciem
celu), **Reconnecting…** lub **Disconnected — reason** (z przyczyną). **Connect** i **Disconnect**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>) znajdują się po prawej stronie, a <kbd>F4</kbd> łączy ponownie.

Jeśli nie znaleziono żadnej sondy, strona wyświetla **No J-Link probe found** i informację, czy znaleziono J-Link
Software. Sprawdź kabel USB, a w systemie Linux regułę udev ([Instalacja](install.md#dostęp-usb-do-j-link)).
`hwio doctor` sprawdza to wszystko.

**Poprowadź mnie** na tej stronie lub **Nie wiesz, jak połączyć? Poprowadź mnie** pod listą programatorów
otwiera kreator połączenia: pomaga wybrać programator, podłączyć kabel i zasilanie, sprawdza połączenie, a potem łączy
kartę.

### Opcje połączenia

**⋯** (**Advanced**, zaawansowane) na pasku urządzenia zawiera opcje tej sondy. HARDWARIO Monitor zapamiętuje je
dla każdego numeru seryjnego sondy. Obowiązują od następnego połączenia.

| Opcja | Domyślnie | Znaczenie |
|---|---|---|
| **Automatic speed** | włączona | J-Link sam wybiera prędkość. Wyłączona: **J-Link speed (kHz)**, od 5 do 50000 |
| **RTT latency (ms)** | 0 (wartość urządzenia, 50 ms dla CHESTER) | Jak często odczytywana jest konsola |
| **Auto-reconnect** | włączona | Samoczynne ponowne połączenie po zerwaniu łącza |
| **Reset on connect** | wyłączona | Reset urządzenia przy łączeniu |
| **Save console to log file** | włączona | Zapis [pliku konsoli](console.md#pliki-konsoli) |

Sondy bez własnych opcji używają ustawień domyślnych z **Settings → J-Link**.

## Karty

Każde urządzenie to jedna karta; można otworzyć do 8 kart. <kbd>Ctrl</kbd>+<kbd>T</kbd> lub **+** otwiera nową
kartę, <kbd>Ctrl</kbd>+<kbd>W</kbd> ją zamyka (podobnie kliknięcie karty środkowym przyciskiem myszy),
<kbd>Ctrl</kbd>+<kbd>Tab</kbd> i <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> przełączają między kartami.
Jeśli w karcie trwa jakaś operacja, przed zamknięciem pojawi się pytanie.

Pasek boczny po lewej stronie przełącza strony karty: **Console** (konsola, <kbd>Ctrl</kbd>+<kbd>1</kbd>) i
**Flash** (wgrywanie, <kbd>Ctrl</kbd>+<kbd>2</kbd>). **Settings** (ustawienia, <kbd>Ctrl</kbd>+<kbd>,</kbd>)
otwiera się jako osobna karta.

Aplikacja zawsze uruchamia się z jedną kartą **New device** (**Choose a device to connect**); karty z poprzedniej
sesji nie są ponownie otwierane.

Jednocześnie działa tylko jedno okno aplikacji. Ponowne uruchomienie aplikacji przenosi otwarte okno na wierzch.

## Strony

- [Konsola](console.md): powłoka i log urządzenia, wyszukiwanie, filtrowanie, historia, pliki wsadowe, pliki konsoli.
- [Wgrywanie firmware](flashing.md): firmware aplikacji i LTE, kasowanie, recover, reset.

## Ustawienia

| Sekcja | Zawartość |
|---|---|
| **General** | **Mode** (Basic, Advanced, Expert), **Language** (System, English, Čeština, Deutsch, Polski, Slovenčina), **Theme** (System, Light, Dark) |
| **Console** | rozmiar czcionki, znaczniki czasu, zawijanie wierszy, podpowiedzi poleceń, historia poleceń, pliki konsoli i ich folder |
| **J-Link** | biblioteka J-Link (**Automatic** lub **Custom path**) i domyślne opcje połączenia |
| **nRF Util** | program nRF Util (**Automatic** lub **Custom path**), potrzebny tylko do firmware modemu LTE |
| **AI assistants (MCP)** | [serwer MCP](../en/mcp.md) dla asystentów AI |
| **Expert** | [serwer wsparcia](remote-support.md#settings) |
| **About** | wersja (z przyciskiem **Copy** dla wsparcia technicznego), [sprawdzanie aktualizacji](privacy.md#sprawdzanie-aktualizacji), polecenie `hwio` tej instalacji, licencje, odnośniki |

Zmiany obowiązują natychmiast. W polskim interfejsie tryby nazywają się **Podstawowy**, **Zaawansowany** i **Ekspert**.

<a id="modes"></a>
### Tryby

**Mode** (tryb) decyduje, ile pokazują ustawienia. Przy pierwszym uruchomieniu (i jeden raz po aktualizacji z wersji
2.0.0-rc.3 lub starszej) pierwszy krok kreatora konfiguracji, **Witaj**, pyta **Jak chcesz używać HARDWARIO Monitor?**:
kliknij tryb lub wybierz go klawiszami <kbd>↑</kbd> / <kbd>↓</kbd>. Wstępnie wybrany jest **Advanced**; jeśli w starszej
wersji było włączone **Show expert maintenance actions**, jest to **Expert**. Wybrany tryb zostaje zapisany niezależnie
od tego, jak opuścisz ten krok: **Dalej**, **Pomiń konfigurację**, **Anuluj**, **×** lub <kbd>Esc</kbd>. Zmienisz go w
dowolnej chwili w **Settings › General › Mode**.

| Wyświetlane | Basic | Advanced | Expert |
|---|---|---|---|
| **General**: Mode, Language, Theme | ✓ | ✓ | ✓ |
| **Console**: rozmiar czcionki, znaczniki czasu, zawijanie wierszy, pliki konsoli, folder dzienników | ✓ | ✓ | ✓ |
| **Console**: podpowiedzi poleceń, odczyt listy poleceń, **Remember commands**, **Clear history** | | ✓ | ✓ |
| **J-Link**: Library, Default speed, Auto-reconnect | | ✓ | ✓ |
| **J-Link**: RTT latency, Reset on connect | | | ✓ |
| **nRF Util**, **AI assistants (MCP)** | | ✓ | ✓ |
| **Expert**: Support server | | | ✓ |
| **About**, w trybach Advanced i Expert z **Include pre-releases** | ✓ | ✓ | ✓ |
| **Flash › Maintenance**: **Erase everything…**, **Recover…**, MCU LTE | | | ✓ |

Tryb tylko ukrywa ustawienia: ukryte ustawienie zachowuje swoją wartość i nadal jest używane (na przykład własna
ścieżka do biblioteki J-Link w trybie Basic). Gdy błąd lub przewodnik otworzy ustawienie ukryte przez tryb (**Open
Settings**), jest ono wyświetlane podczas tej wizyty z uwagą „Shown because it was needed.”; tryb się nie zmienia.

## Pomoc

**Help** (pomoc) na dole paska bocznego:

- **Keyboard shortcuts (F1)**: wszystkie skróty klawiaturowe aplikacji (także <kbd>Ctrl</kbd>+<kbd>/</kbd>).
- **Kreator konfiguracji…**: sprawdza J-Link Software, dostęp do USB (Linux), nRF Util i programatory.
- **Połącz urządzenie…**: kreator połączenia (programator, kabel, zasilanie, sprawdzenie na żywo, połączenie).
- **Rozwiązywanie problemów z połączeniem…**: wybierz problem; kreator uruchomi sprawdzenia i poprowadzi Cię krok
  po kroku.
- **Wgraj firmware (z kreatorem)…**: wgrywa firmware aplikacji krok po kroku ([Wgrywanie](flashing.md)).

Te trzy działają na bieżącej karcie urządzenia (z karty ustawień: na ostatnio wyświetlonej karcie urządzenia). Działają
z urządzeniem podłączonym do tego komputera: na karcie [sesji wsparcia](remote-support.md#join-a-session) się nie
otwierają i informują o tym.
- **Create diagnostic report…**: plik zip dla wsparcia technicznego HARDWARIO
  ([Prywatność](privacy.md#raport-diagnostyczny)).
- **Documentation ↗**: te strony w serwisie GitHub, dla zainstalowanej wersji aplikacji i w języku aplikacji
  (po angielsku, jeśli strona nie jest przetłumaczona).

<a id="guides-and-views"></a>
<a id="kreatory-i-inne-widoki"></a>
### Kreatory i inne widoki

Kreatory (kreator konfiguracji, kreator połączenia, kreator rozwiązywania problemów, wgrywanie z kreatorem i **Update
LTE firmware…**), raport diagnostyczny, skróty klawiaturowe, aktualizacja firmware J-Linka, **Join support session…** i
**Share with support…** otwierają się w obszarze strony karty, obok paska bocznego; karty i pasek boczny pozostają.
**×** lub <kbd>Esc</kbd> je zamyka i wraca poprzednia strona. Przełączenie na inną kartę również je zamyka, z wyjątkiem
czasu, gdy pracują (zbieranie raportu, zapis firmware J-Linka).

Podczas działania kreatora możesz otwierać inne strony karty, na przykład konsolę: **Wizard** (kreator) na górze paska
bocznego (oraz mała plakietka na karcie urządzenia) przywraca kreator tam, gdzie był. Każda karta urządzenia może mieć
własny kreator.
Gdy otwarty jest kreator połączenia, kreator rozwiązywania problemów lub wgrywanie z kreatorem, **Wybierz inne
urządzenie** tej karty czeka, aż go zamkniesz: urządzenie karty wybiera kreator. Połączenie i rozłączenie pozostają
dostępne, a kreator za nimi podąża.

Kreator konfiguracji, dopóki jest otwarty, zakrywa obszar strony wszystkich kart. Kreator działający w karcie pracuje
dalej pod nim (jego plakietka pozostaje) i pojawia się ponownie po zamknięciu kreatora konfiguracji. Inne kreatory
uruchamiają się dopiero po zamknięciu kreatora konfiguracji (**Najpierw zakończ lub zamknij kreator konfiguracji.**).

Gdy coś się nie powiedzie, aplikacja pokazuje, co się stało, prawdopodobną przyczynę (**Probably:**) i co należy
zrobić (**What to do:**). **Details** pokazuje kod błędu; [Rozwiązywanie problemów](troubleshooting.md) zawiera
wszystkie kody.
**Learn more ↗** otwiera w przeglądarce opis kodu błędu na tej stronie.

## Wiersz poleceń

Wszystko, co aplikacja robi z sondą J-Link, jest dostępne także jako polecenie:

```sh
hwio jlink list                       # the connected probes
hwio chester app console              # the console of the application MCU
hwio chester app flash firmware.hex
hwio doctor                           # check the installation
```

Zobacz [Wiersz poleceń](../en/cli.md).

`hwio-monitor --version` wypisuje wersję aplikacji desktopowej, a `hwio-monitor --help` jej opcje; żadne z nich nie
otwiera okna. W systemie Windows tekst pojawia się w oknie wiersza poleceń lub PowerShell, z którego ją uruchomiono.
