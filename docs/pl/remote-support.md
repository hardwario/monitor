# Zdalne wsparcie

[Dokumentacja](index.md) › Zdalne wsparcie

Wsparcie HARDWARIO może ze swojego biura oglądać konsolę Twojego urządzenia, wysyłać polecenia, a jeśli za każdym
razem na to zezwolisz, także wgrać firmware z katalogu, skasować, odblokować (recover) lub zresetować urządzenie.
Konsolę udostępniasz jednym kliknięciem, a 6-cyfrowy kod sesji podajesz wsparciu przez telefon. Zdalne wsparcie
działa z serwerem [HARDWARIO Terminal](https://terminal.hardwario.com/) i jest zgodne z Terminalem webowym: wsparcie
może dołączyć z Terminala webowego (tylko konsola) albo z HARDWARIO Monitor.

Inne języki: [Čeština](../cs/remote-support.md) · [Deutsch](../de/remote-support.md) ·
[Slovenčina](../sk/remote-support.md) · [English](../en/remote-support.md)

<a id="share-your-console"></a>
## Udostępnianie konsoli

1. Podłącz urządzenie (J-Link lub Bluetooth) i otwórz stronę **Konsola**.
2. Kliknij **Udostępnij wsparciu…**. Okno wyjaśnia, co wsparcie może robić. Kliknij **Rozpocznij udostępnianie**.
3. Baner pokazuje **Udostępniono wsparciu — kod 482 913**. Podaj kod wsparciu HARDWARIO. **Kopiuj link** kopiuje
   link z kodem.
4. Gdy ktoś dołączy, HARDWARIO Monitor zapyta: *Ktoś dołączył do sesji 482 913. Zezwolić mu na wysyłanie poleceń do
   urządzenia?* Kliknij **Zezwól** tylko wtedy, gdy wsparcie HARDWARIO właśnie powiedziało Ci, że dołącza.
   **Zakończ sesję** (domyślnie) kończy sesję dla wszystkich. Bez odpowiedzi sesja kończy się po 2 minutach.
5. Polecenia od wsparcia pojawiają się w konsoli, a każde z nich poprzedza wiersz *— Zdalnie: polecenie <polecenie> —*.
6. Jeśli wsparcie poprosi o wgranie firmware, skasowanie, odblokowanie (recover) lub zresetowanie urządzenia, zobaczysz,
   o co dokładnie prosi, na przykład *Wsparcie prosi o wgranie firmware CHESTER Clime v3.2.1 (ID …) do rdzenia
   aplikacji.* Domyślna jest odpowiedź **Odmów**; prośba bez odpowiedzi zostaje odrzucona po 60 sekundach. Firmware
   pochodzi wyłącznie z katalogu HARDWARIO, a Twój HARDWARIO Monitor sam je pobiera i weryfikuje.
7. Gdy skończysz, kliknij **Zakończ udostępnianie**.

Udostępnianie kończy się też po odłączeniu urządzenia, zamknięciu karty lub zamknięciu aplikacji, po 30 minutach
bezczynności (baner ostrzega 2 minuty wcześniej, **Kontynuuj udostępnianie** je przedłuża) i najpóźniej po 8 godzinach.

<a id="what-support-sees"></a>
## Co widzi wsparcie

**Każdy, kto zna kod sesji, widzi wyjście konsoli** od chwili połączenia, jeszcze zanim mu zezwolisz. Podawaj kod
tylko wsparciu HARDWARIO. Polecenia i operacje zawsze wymagają Twojego kliknięcia: wyjście konsoli jest widoczne, ale
bez Twojej zgody nikt nie może sterować urządzeniem.

Sesja przechodzi przez serwer HARDWARIO (`terminal.hardwario.com`) szyfrowanym połączeniem. Polecenia z tajnymi
argumentami i tajne wartości wypisywane przez urządzenie (np. claim token) są przesyłane w ukryciu, tak jak w pliku
konsoli.

Wszystko, co zdalne, jest zapisywane w pliku konsoli: początek i koniec udostępniania, każde dołączenie i Twoja
odpowiedź, każde zdalne polecenie oraz każda prośba o operację wraz z jej wynikiem. Zobacz
[Pliki konsoli](console.md#pliki-konsoli).

<a id="join-a-session"></a>
## Dołączanie do sesji (wsparcie)

1. Na stronie wyboru urządzenia w karcie kliknij **Dołącz do sesji wsparcia…**.
2. Wpisz kod sesji, który podaje Ci klient (spacje są dozwolone), albo wklej link. Link do innego serwera niż
   skonfigurowany wymaga dodatkowego potwierdzenia.
3. Otwiera się nowa karta zatytułowana **Sesja 482 913**, a potem *CHESTER-M 2159018247 — zdalnie* (lub *Sesja 482 913 —
   web Terminal*, gdy klient korzysta z web Terminal). Dopóki klient Ci nie zezwoli, baner pokazuje *Czekanie, aż klient
   zezwoli…*. Wyjście na żywo może się już pojawiać.
4. Po uzyskaniu zgody otrzymujesz historię konsoli klienta i możesz pracować w konsoli jak zwykle: historia,
   wyszukiwanie, filtrowanie i zapisywanie widoku.
5. Gdy klient używa HARDWARIO Monitor, strona **Wgraj** oferuje **Poproś o wgranie** (identyfikator firmware z
   katalogu), **Poproś o skasowanie**, **Poproś o recover** i **Poproś o reset** dla rdzenia aplikacji lub LTE. Każda
   prośba czeka na potwierdzenie klienta; wynik zgłasza Monitor klienta. Gdy klient używa Terminala webowego, dostępna
   jest tylko konsola.

HARDWARIO Monitor nie zawsze rozpozna, że klient zakończył udostępnianie: serwer tego nie ogłasza. Po 30 minutach bez
żadnej aktywności baner o tym informuje i oferuje **Opuść**. Karta pozostaje otwarta, więc nadal możesz zapisać
konsolę.

<a id="command-line"></a>
## Wiersz poleceń

```
hwio remote host                          # share the J-Link console; prints the code and the link
hwio remote host --ble -a F1:22:33:44:55:66
hwio remote join 482913                   # interactive console
hwio remote join 482913 --command "info show"
hwio remote join 482913 --flash 0123456789abcdef0123456789abcdef
hwio remote join 482913 --reset --target lte
```

`hwio remote host` pyta w terminalu, zanim zezwoli na dołączenie lub operację. Bez terminala na stdin dołączenie
nigdy nie jest dozwolone. Zobacz [Wiersz poleceń](../en/cli.md#remote-support-hwio-remote).

<a id="settings"></a>
## Ustawienia

**Ustawienia** › **Ekspert** › **Serwer wsparcia** (w [trybie](getting-started.md#modes) Ekspert) zmienia serwer (na
przykład na serwer testowy). Akceptowane są tylko adresy `https`. Pozostaw puste, aby używać
`https://terminal.hardwario.com`.
