# Vzdálená podpora

[Dokumentace](index.md) › Vzdálená podpora

Podpora HARDWARIO se může ze své kanceláře podívat na konzoli vašeho zařízení, posílat příkazy a, pokud to pokaždé
povolíte, nahrát firmware z katalogu, zařízení smazat, obnovit (recover) nebo resetovat. Konzoli sdílíte jedním
kliknutím a podpoře do telefonu přečtete šestimístný kód. Vzdálená podpora pracuje se serverem
[HARDWARIO Terminal](https://terminal.hardwario.com/) a je kompatibilní s webovým Terminalem: podpora se může připojit
z webového Terminalu (jen konzole) nebo z aplikace HARDWARIO Monitor.

Další jazyky: [English](../en/remote-support.md) · [Deutsch](../de/remote-support.md) ·
[Slovenčina](../sk/remote-support.md) · [Polski](../pl/remote-support.md)

<a id="share-your-console"></a>
## Sdílení konzole

1. Připojte zařízení (J-Link nebo Bluetooth) a otevřete stránku **Konzole**.
2. Klikněte na **Sdílet s podporou…**. Dialog vysvětlí, co podpora může dělat. Klikněte na **Zahájit sdílení**.
3. Banner ukáže **Sdíleno s podporou — kód 482 913**. Přečtěte kód podpoře HARDWARIO. **Kopírovat odkaz** zkopíruje
   odkaz s kódem.
4. Když se někdo připojí, HARDWARIO Monitor se zeptá: *K relaci 482 913 se někdo připojil. Povolit mu posílat příkazy
   do zařízení?* Klikněte na **Povolit** jen tehdy, když vám podpora HARDWARIO právě řekla, že se připojuje.
   **Ukončit relaci** (výchozí volba) ukončí relaci pro všechny. Bez odpovědi relace skončí po 2 minutách.
5. Příkazy od podpory se v konzoli zobrazí a každému z nich předchází řádek *— Vzdáleně: příkaz <příkaz> —*.
6. Pokud podpora požádá o nahrání firmwaru, smazání, obnovení (recover) nebo reset zařízení, uvidíte, o co přesně
   žádá, například *Podpora žádá o nahrání firmwaru CHESTER Clime v3.2.1 (ID …) do aplikačního jádra.* Výchozí volba je
   **Zamítnout**; žádost bez odpovědi se po 60 sekundách zamítne. Firmware pochází jen z katalogu HARDWARIO a stáhne
   a ověří ho váš HARDWARIO Monitor.
7. Až budete hotovi, klikněte na **Ukončit sdílení**.

Sdílení skončí také tehdy, když zařízení odpojíte, zavřete kartu nebo aplikaci ukončíte, po 30 minutách bez aktivity
(banner vás 2 minuty předem upozorní, **Ponechat sdílení** ho prodlouží) a nejpozději po 8 hodinách.

<a id="what-support-sees"></a>
## Co podpora vidí

**Kdokoli, kdo zná kód, vidí výstup konzole** od okamžiku, kdy se připojí, ještě předtím, než mu to povolíte. Kód
čtěte jen podpoře HARDWARIO. Příkazy a operace vždy potřebují vaše kliknutí: výstup konzole je vidět, ale bez vašeho
svolení nikdo zařízení ovládat nemůže.

Relace prochází serverem HARDWARIO (`terminal.hardwario.com`) přes šifrované spojení. Příkazy s tajnými argumenty
a tajné hodnoty, které zařízení vypíše (například claim token), se posílají skryté, stejně jako v souboru konzole.

Vše vzdálené se zapisuje do souboru konzole: začátek a konec sdílení, každé připojení a vaše odpověď, každý vzdálený
příkaz a každá žádost o operaci s jejím výsledkem. Viz [Soubory konzole](console.md#soubory-konzole).

<a id="join-a-session"></a>
## Připojení k relaci (podpora)

1. Na stránce **Vyberte zařízení** karty klikněte na **Připojit se k relaci podpory…**.
2. Napište kód, který vám zákazník přečte (mezery jsou povolené), nebo vložte odkaz. Odkaz na jiný server, než je
   nastavený, vyžaduje další potvrzení.
3. Otevře se nová karta s názvem **Relace 482 913** a potom *CHESTER-M 2159018247 — vzdáleně* (nebo *Relace 482 913 —
   web Terminal*, když zákazník používá web Terminal). Dokud vám to zákazník nepovolí, banner ukazuje *Čeká se, až vám
   to zákazník povolí…*. Živý výstup se může zobrazovat už teď.
4. Po povolení dostanete historii konzole zákazníka a můžete v konzoli pracovat jako obvykle: historie, hledání, filtr
   a uložení výpisu.
5. Se zákazníkem, který používá HARDWARIO Monitor, nabízí stránka **Nahrát FW** volby **Požádat o nahrání** (ID
   firmwaru z katalogu), **Požádat o smazání**, **Požádat o obnovení** a **Požádat o reset** pro aplikační nebo LTE
   jádro. Každá žádost čeká na potvrzení zákazníka; výsledek hlásí Monitor zákazníka. Se zákazníkem, který používá
   webový Terminal, je k dispozici jen konzole.

Když zákazník ukončí sdílení, HARDWARIO Monitor to nemusí vždy poznat: server to neoznamuje. Po 30 minutách bez
jakékoli aktivity to banner oznámí a nabídne **Opustit**. Karta zůstane otevřená, takže konzoli můžete ještě uložit.

<a id="command-line"></a>
## Příkazová řádka

```
hwio remote host                          # share the J-Link console; prints the code and the link
hwio remote host --ble -a F1:22:33:44:55:66
hwio remote join 482913                   # interactive console
hwio remote join 482913 --command "info show"
hwio remote join 482913 --flash 0123456789abcdef0123456789abcdef
hwio remote join 482913 --reset --target lte
```

`hwio remote host` se před povolením připojení nebo operace zeptá v terminálu. Bez terminálu na stdin se připojení
nikdy nepovolí. Viz [Příkazová řádka](../en/cli.md#remote-support-hwio-remote).

<a id="settings"></a>
## Nastavení

**Nastavení** › **Pokročilé** › **Server podpory** změní server (například na testovací). Přijímají se jen adresy
`https`. Pro `https://terminal.hardwario.com` nechte pole prázdné.
