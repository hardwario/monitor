# Bluetooth LE

[Dokumentacja](index.md) › Bluetooth LE

CHESTER udostępnia swoją powłokę poleceń przez Bluetooth LE (Nordic UART Service). HARDWARIO Monitor łączy się z
nią w aplikacji desktopowej i z wiersza poleceń za pomocą `hwio ble`; nie jest potrzebny J-Link ani kabel.

Powłoka przez Bluetooth działa jak konsola przez J-Link: wysyłasz polecenia i czytasz ich odpowiedzi, a komunikaty
logu wysyłane przez firmware pojawiają się jako wiersze logu.

## W aplikacji desktopowej

1. Otwórz nową kartę (<kbd>Ctrl</kbd>+<kbd>T</kbd>) i przełącz **Connect via** (połącz przez) na **Bluetooth**.
   Aplikacja zapamiętuje ten wybór dla następnej karty.
2. Lista pokazuje pobliskie urządzenia CHESTER, od najsilniejszego sygnału, i odświeża się samoczynnie. Sygnał jest
   uśredniany przez kilka sekund, a urządzenie przesuwa się wyżej tylko wtedy, gdy jest wyraźnie silniejsze (o 6 dB),
   dzięki czemu lista pozostaje stabilna; gdy wskaźnik myszy jest nad listą (lub wiersz ma fokus klawiatury),
   kolejność w ogóle się nie zmienia, a nowe urządzenia są dodawane na końcu. Można ją filtrować po nazwie albo
   zaznaczyć **Show all devices** (pokaż wszystkie urządzenia), aby zobaczyć wszystkie urządzenia Bluetooth (najpierw
   urządzenia CHESTER). Urządzenie używane w innej karcie jest oznaczone **In use (tab N)**; jego wybranie przełącza
   na tę kartę. Urządzenie używane przez inny program jest oznaczone **In use by another program** i nie można go
   wybrać.
3. Wybierz urządzenie i naciśnij **Connect** (połącz) (albo kliknij je dwukrotnie lub naciśnij <kbd>Enter</kbd>).

Karta działa jak karta J-Link: konsola z podpowiedziami, historią, wyszukiwaniem, plikami wsadowymi i plikami konsoli
([Konsola](console.md)). Panel logu pojawia się tylko wtedy, gdy urządzenie wysyła komunikaty logu. Różnice:

- Wgrywanie firmware wymaga J-Link: **Flash** jest w karcie Bluetooth nieaktywny. Aby wgrać firmware do urządzenia,
  otwórz kartę J-Link.
- **Auto-reconnect** znajduje się w menu **⋯** na pasku urządzenia i jest domyślnie wyłączone; aplikacja zapamiętuje
  je dla każdego urządzenia.
- Pierwsze połączenie z urządzeniem, które nie jest jeszcze sparowane, wyświetla **Waiting for pairing — enter the
  passkey in the system dialog** (czekanie na parowanie — wpisz passkey w oknie systemowym), a w razie potrzeby
  **Pair** (sparuj) (Linux, Windows). Zobacz [Parowanie](#parowanie).
- Jeśli nie można znaleźć urządzenia, **Choose another device** (wybierz inne urządzenie) wraca do listy.

Karty Bluetooth po ponownym uruchomieniu wracają rozłączone; naciśnij **Connect**. Aplikacja uruchamia Bluetooth
dopiero przy pierwszym otwarciu widoku Bluetooth, więc w macOS prosi o dostęp do Bluetooth dopiero wtedy.

## Wyszukiwanie urządzenia (wiersz poleceń)

```sh
hwio ble scan
```

Skanowanie trwa 5 sekund (`--timeout S`, od 1 do 60) i wyświetla pobliskie urządzenia CHESTER: te, których
rozgłaszana nazwa zawiera `CHESTER`. `--all` wyświetla wszystkie urządzenia Bluetooth.

Każde urządzenie ma **ID**:

- Linux i Windows: jego adres Bluetooth, na przykład `F1:22:33:44:55:66`.
- macOS: UUID, który macOS przypisuje urządzeniu. Nie jest to adres Bluetooth i jest ważny tylko na danym Macu.

## Połączenie (wiersz poleceń)

Wybierz urządzenie po jego ID (`-a`) lub po części nazwy (`--name`, bez rozróżniania wielkości liter; pasować musi
dokładnie jedno urządzenie znalezione 5-sekundowym skanowaniem):

```sh
hwio ble console -a F1:22:33:44:55:66      # interactive console
hwio ble console --name "CHESTER Clime"
hwio ble command -a F1:22:33:44:55:66 "info show"
```

Nawiązanie połączenia może potrwać do 30 sekund; nieudane próby są w tym czasie ponawiane. `ble command` wypisuje
odpowiedź i kończy działanie przy znaku zachęty powłoki lub po `-t` sekundach (domyślnie 3).

W konsoli interaktywnej <kbd>F4</kbd> łączy ponownie, a <kbd>Ctrl</kbd>+<kbd>C</kbd> w pustym wierszu lub
<kbd>Ctrl</kbd>+<kbd>D</kbd> kończy działanie. Z `--auto-reconnect` konsola łączy się ponownie po zerwaniu łącza,
dopóki urządzenie jest w zasięgu. Konsola zapisuje plik konsoli i prowadzi historię poleceń, tak jak konsola przez
J-Link ([Konsola](console.md#pliki-konsoli)).

Z urządzeniem może być połączony tylko jeden program. Jeśli połączony jest z nim inny `hwio`, pojawi się błąd
[ble.in_use_by_other_process](troubleshooting.md#ble.in_use_by_other_process).

## Parowanie

CHESTER chroni swoją powłokę Bluetooth kluczem dostępu (passkey), więc komputer musi się z nim jednorazowo sparować
(utworzyć powiązanie, bond). Passkey to 123456, chyba że zmieniono go podczas konfiguracji urządzenia. HARDWARIO
Monitor nigdy nie przechowuje passkey i nigdy nie wpisuje go za Ciebie: pyta o niego system operacyjny.

| System | Sposób |
|---|---|
| Linux | Przy pierwszym połączeniu, przyciskiem **Pair** w aplikacji lub za pomocą `hwio ble pair -a ID`. O passkey pytają ustawienia Bluetooth środowiska graficznego. |
| Windows | Przy pierwszym połączeniu, przyciskiem **Pair** w aplikacji lub za pomocą `hwio ble pair -a ID`. Windows pyta o passkey. |
| macOS | Tylko przy pierwszym połączeniu; macOS pyta o passkey. `hwio ble pair` i **Pair** nie są dostępne w macOS. |

Gdy urządzenie czeka na passkey, połączenie czeka do 60 sekund.

**Linux bez agenta parowania.** Niektóre środowiska graficzne (GNOME) pytają o passkey tylko wtedy, gdy ich
ustawienia Bluetooth są otwarte. Jeśli nic nie zapyta, pojawi się błąd
[ble.no_pairing_agent](troubleshooting.md#ble.no_pairing_agent). Sparuj urządzenie w systemowych ustawieniach
Bluetooth lub za pomocą `bluetoothctl`:

```text
$ bluetoothctl
[bluetooth]# agent KeyboardOnly
[bluetooth]# default-agent
[bluetooth]# pair F1:22:33:44:55:66
```

Jeśli parowanie nie powiedzie się po zmianie passkey w urządzeniu, usuń urządzenie w systemowych ustawieniach
Bluetooth i sparuj je ponownie ([ble.pairing_failed](troubleshooting.md#ble.pairing_failed)).

## Uprawnienia i adapter

- **Linux:** Bluetooth korzysta z BlueZ. Jeśli usługa nie działa, uruchom ją poleceniem
  `sudo systemctl start bluetooth`.
- **macOS:** przy pierwszym skanowaniu macOS poprosi o dostęp do Bluetooth: w aplikacji dla HARDWARIO Monitor,
  w wierszu poleceń dla aplikacji, z której uruchamiasz `hwio` (zwykle Terminal). Jeśli odmówiono, zezwól na dostęp
  w **System Settings → Privacy & Security → Bluetooth** (**Ustawienia systemowe → Prywatność i ochrona →
  Bluetooth**); w aplikacji komunikat o błędzie oferuje **Open Privacy settings**.
- **Wszystkie systemy:** włącz adapter Bluetooth. `hwio doctor` pokazuje, czy jest gotowy.

## JSON

Z `--json` polecenie `ble scan` wypisuje urządzenia, a `ble console` wypisuje jeden wiersz JSON na każdy wiersz
konsoli, tak jak konsola przez J-Link ([Wiersz poleceń](../en/cli.md#json-output)).
