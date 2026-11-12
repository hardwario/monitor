# Vzdialená podpora

[Dokumentácia](index.md) › Vzdialená podpora

Podpora HARDWARIO sa môže zo svojej kancelárie pozrieť na konzolu vášho zariadenia, posielať príkazy a, ak to zakaždým
povolíte, nahrať firmvér z katalógu, vymazať, obnoviť alebo resetovať zariadenie. Konzolu zdieľate jedným kliknutím a
podpore prečítate po telefóne 6-miestny kód. Vzdialená podpora funguje so serverom
[HARDWARIO Terminal](https://terminal.hardwario.com/) a je kompatibilná s webovým Terminalom: podpora sa môže pripojiť
z webového Terminalu (len konzola) alebo z HARDWARIO Monitor.

Ďalšie jazyky: [English](../en/remote-support.md) · [Čeština](../cs/remote-support.md) ·
[Deutsch](../de/remote-support.md) · [Polski](../pl/remote-support.md)

<a id="share-your-console"></a>
## Zdieľanie konzoly

1. Pripojte zariadenie (J-Link alebo Bluetooth) a otvorte stránku **Konzola**.
2. Kliknite na **Zdieľať s podporou…**. Dialóg vysvetlí, čo môže podpora robiť. Kliknite na **Začať zdieľanie**.
3. Lišta zobrazí **Zdieľané s podporou — kód 482 913**. Prečítajte kód podpore HARDWARIO. **Kopírovať odkaz**
   skopíruje odkaz s kódom.
4. Keď sa niekto pripojí, HARDWARIO Monitor sa opýta: *K relácii 482 913 sa niekto pripojil. Povoliť mu posielať
   príkazy do zariadenia?* Kliknite na **Povoliť** len vtedy, keď vám podpora HARDWARIO práve povedala, že sa
   pripája. **Ukončiť reláciu** (predvolená voľba) ukončí reláciu pre všetkých. Bez odpovede sa relácia po 2 minútach
   ukončí.
5. Príkazy od podpory sa zobrazujú v konzole a každému z nich predchádza riadok *— Vzdialené: príkaz <príkaz> —*.
6. Ak vás podpora požiada o nahranie firmvéru, vymazanie, obnovenie alebo reset zariadenia, uvidíte, o čo presne žiada,
   napríklad *Podpora žiada o nahranie firmvéru CHESTER Clime v3.2.1 (ID …) do aplikačného jadra.* **Zamietnuť** je
   predvolená voľba; žiadosť bez odpovede sa po 60 sekundách zamietne. Firmvér pochádza len z katalógu HARDWARIO a váš
   HARDWARIO Monitor ho stiahne a overí.
7. Keď skončíte, kliknite na **Ukončiť zdieľanie**.

Zdieľanie sa skončí aj vtedy, keď zariadenie odpojíte, zatvoríte kartu alebo aplikáciu ukončíte, po 30 minútach bez
aktivity (lišta vás 2 minúty vopred upozorní, **Ponechať zdieľanie** ho predĺži) a najneskôr po 8 hodinách.

<a id="what-support-sees"></a>
## Čo podpora vidí

**Ktokoľvek, kto pozná kód, vidí výstup konzoly** od chvíle, keď sa pripojí, ešte predtým, ako ho povolíte. Kód
prečítajte len podpore HARDWARIO. Príkazy a operácie vždy potrebujú vaše kliknutie: výstup konzoly je viditeľný, ale
bez vášho povolenia nikto zariadenie neovláda.

Relácia prechádza cez server HARDWARIO (`terminal.hardwario.com`) šifrovaným spojením. Príkazy s tajnými argumentmi a
tajné hodnoty, ktoré vypíše zariadenie (napríklad claim token), sa posielajú skryté, rovnako ako v súbore konzoly.

Všetko vzdialené sa zapisuje do súboru konzoly: začiatok a koniec zdieľania, každé pripojenie a vaša odpoveď, každý
vzdialený príkaz a každá žiadosť o operáciu s jej výsledkom. Pozrite si [Súbory konzoly](console.md).

<a id="join-a-session"></a>
## Pripojenie k relácii (podpora)

1. Na stránke **Vyberte zariadenie** v karte kliknite na **Pripojiť sa k relácii podpory…**.
2. Napíšte kód, ktorý vám zákazník prečíta (medzery sú povolené), alebo vložte odkaz. Odkaz na iný server, ako je
   nastavený, potrebuje ďalšie potvrdenie.
3. Otvorí sa nová karta s názvom **Relácia 482 913** a potom *CHESTER-M 2159018247 — na diaľku* (alebo *Relácia 482 913
   — web Terminal*, keď zákazník používa web Terminal). Kým vás zákazník nepovolí, lišta zobrazuje *Čaká sa, kým vás
   zákazník povolí…*. Živý výstup sa už môže zobrazovať.
4. Po povolení dostanete históriu konzoly zákazníka a môžete s konzolou pracovať ako zvyčajne: história, vyhľadávanie,
   filter a uloženie zobrazenia.
5. So zákazníkom, ktorý používa HARDWARIO Monitor, ponúka stránka **Nahrávanie firmvéru** voľby **Požiadať o nahranie**
   (ID firmvéru z katalógu), **Požiadať o vymazanie**, **Požiadať o obnovenie** a **Požiadať o reset** pre aplikačné
   alebo LTE jadro. Každá žiadosť čaká na potvrdenie zákazníka; výsledok hlási Monitor zákazníka. So zákazníkom, ktorý
   používa webový Terminal, je k dispozícii len konzola.

Keď zákazník ukončí zdieľanie, HARDWARIO Monitor to nie vždy rozpozná: server to neoznamuje. Po 30 minútach bez
akejkoľvek aktivity to lišta oznámi a ponúkne **Opustiť**. Karta zostane otvorená, takže konzolu môžete stále uložiť.

<a id="command-line"></a>
## Príkazový riadok

```
hwio remote host                          # share the J-Link console; prints the code and the link
hwio remote host --ble -a F1:22:33:44:55:66
hwio remote join 482913                   # interactive console
hwio remote join 482913 --command "info show"
hwio remote join 482913 --flash 0123456789abcdef0123456789abcdef
hwio remote join 482913 --reset --target lte
```

`hwio remote host` sa pred povolením pripojenia alebo operácie opýta v termináli. Bez terminálu na stdin sa pripojenie
nikdy nepovolí. Pozrite si [Príkazový riadok](../en/cli.md#remote-support-hwio-remote).

<a id="settings"></a>
## Nastavenia

**Nastavenia** › **Expertné** › **Server podpory** (v [režime](getting-started.md#modes) Expertný) zmení server
(napríklad na testovací server). Prijímajú sa len adresy `https`. Pre `https://terminal.hardwario.com` nechajte pole
prázdne.
