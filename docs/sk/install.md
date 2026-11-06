# Inštalácia HARDWARIO Monitor

[Dokumentácia](index.md) › Inštalácia

HARDWARIO Monitor je jeden balík pre každý operačný systém. Každý balík obsahuje desktopovú aplikáciu (`hwio-monitor`)
a nástroj pre príkazový riadok (`hwio`). Pre sondu J-Link ďalej potrebujete **SEGGER J-Link Software** a len pre
firmvér LTE modemu aj **Nordic nRF Util**. Ani jedno nie je súčasťou balíkov; HARDWARIO Monitor ich používa z vašej
inštalácie.

## Stiahnutie

Najnovšie vydanie stiahnite zo [stránky vydaní][releases].

| Systém | Súbor |
|---|---|
| Windows 10 22H2 alebo novší, 64-bitový | `hwio-monitor-<version>-windows-x64.msi` |
| macOS 13.3 alebo novší, Apple silicon aj Intel | `hwio-monitor-<version>-macos-universal.dmg` |
| Ubuntu 24.04 alebo novšie, Debian 13 alebo novší | `hardwario-monitor_<version>_amd64.deb` |
| Ubuntu 22.04 alebo novšie a iné distribúcie Linuxu s glibc 2.35 alebo novšou, x86-64 | `hwio-monitor-<version>-x86_64.AppImage` |

Každé vydanie obsahuje aj súbor `SHA256SUMS`. Stiahnutý súbor skontrolujete takto:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## Windows

1. Spustite MSI. Inštaluje sa pre všetkých používateľov do `C:\Program Files\HARDWARIO\Monitor` a vyžaduje
   oprávnenia správcu.
2. V zozname súčastí:
   - **Add hwio to PATH** (pridať hwio do PATH; zapnuté): príkaz `hwio` funguje v nových termináloch.
   - **Desktop shortcut** (odkaz na ploche; vypnuté): odkaz na pracovnej ploche.
3. Spustite **HARDWARIO Monitor** z ponuky Štart.

Inštalátor je podpísaný spoločnosťou HARDWARIO a.s. Kým Microsoft SmartScreen nové vydanie ešte nepozná, Windows môže
pred spustením zobraziť varovanie: zvoľte **More info → Run anyway** (Ďalšie informácie → Spustiť aj tak).

Inštalácia novšej verzie nahradí staršiu; nastavenia zostanú zachované. Staršiu verziu nie je možné nainštalovať cez
novšiu ("A newer version of HARDWARIO Monitor is already installed."): najprv ju odinštalujte. Odinštalovať ju môžete
v **Settings → Apps** (Nastavenia → Aplikácie).

Tichá inštalácia, napríklad s odkazom na ploche:

```bat
msiexec /i hwio-monitor-<version>-windows-x64.msi /qn ADDDESKTOPSHORTCUT=1
```

## macOS

1. Otvorte DMG a presuňte **HARDWARIO Monitor** do **Applications** (Aplikácie).
2. Spustite ho z priečinka Aplikácie. Aplikácia je podpísaná spoločnosťou HARDWARIO a notarizovaná spoločnosťou Apple,
   preto sa otvorí bez varovania.

Príkaz `hwio` je vnútri balíka aplikácie a nie je v `PATH`. **Settings → About → Command line** zobrazuje jeho cestu a
príkaz, ktorý vytvorí odkaz; môžete ho skopírovať:

```sh
sudo ln -s '/Applications/HARDWARIO Monitor.app/Contents/MacOS/hwio' /usr/local/bin/hwio
```

**Bluetooth:** keď prvýkrát otvoríte zobrazenie Bluetooth v aplikácii alebo spustíte prvý príkaz `hwio ble`, macOS sa
spýta na prístup k Bluetooth. Pre `hwio` udelí macOS povolenie aplikácii, z ktorej ho spúšťate, zvyčajne aplikácii
Terminál. Ak ste ho odmietli, povoľte ho v **System Settings → Privacy &
Security → Bluetooth** (Systémové nastavenia → Súkromie a bezpečnosť → Bluetooth).

Pri aktualizácii nahraďte aplikáciu v priečinku Aplikácie novou. Pri odinštalovaní ju presuňte do Koša.

## Linux

### .deb (Ubuntu 24.04+, Debian 13+)

```sh
sudo apt install ./hardwario-monitor_<version>_amd64.deb
```

Balík sa inštaluje do `/opt/hardwario-monitor`, umiestni `hwio-monitor` a `hwio` do `/usr/bin`, pridá aplikáciu do
ponuky pracovného prostredia a nainštaluje pravidlo udev pre sondy J-Link
(`/usr/lib/udev/rules.d/60-hardwario-jlink.rules`). Pre Bluetooth závisí od balíka `bluez`. Odstránite ho príkazom
`sudo apt remove hardwario-monitor`.

### AppImage (Ubuntu 22.04+ a iné distribúcie)

```sh
chmod +x hwio-monitor-<version>-x86_64.AppImage
./hwio-monitor-<version>-x86_64.AppImage
```

AppImage obsahuje oba programy. Nástroj pre príkazový riadok spustí, keď je `hwio` prvým argumentom alebo keď je
spustený cez odkaz s názvom `hwio`:

```sh
./hwio-monitor-<version>-x86_64.AppImage hwio jlink list
ln -s "$PWD/hwio-monitor-<version>-x86_64.AppImage" ~/.local/bin/hwio
```

Bez FUSE (niektoré kontajnery a minimálne systémy) pridajte `--appimage-extract-and-run` alebo nastavte
`APPIMAGE_EXTRACT_AND_RUN=1`. AppImage sa sám neaktualizuje a neinštaluje pravidlo udev: pozrite si nasledujúcu časť.

<a id="usb-access-to-the-j-link"></a>
### Prístup k J-Linku cez USB

Balík .deb toto pravidlo nainštaluje. Pri AppImage ho nainštalujte raz sami:

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/60-hardwario-jlink.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Potom J-Link odpojte a znova pripojte. Pravidlo sprístupní sondu používateľovi prihlásenému pri počítači, nie každému
účtu. Používateľ pripojený len cez SSH takéto prihlásenie nemá a potrebuje vlastné pravidlo.

Inštalátor od spoločnosti SEGGER môže pridať `/etc/udev/rules.d/99-jlink.rules` s `MODE="0666"`, čo umožní sondu
používať každému účtu na počítači. `hwio doctor` na to upozorní
([usb.world_accessible](troubleshooting.md#usb.world_accessible)). Ak to nechcete, toto pravidlo alebo jeho `MODE`
odstráňte; aktualizácia J-Link Software ho môže nainštalovať znova.

### Bluetooth

Bluetooth používa BlueZ. Ak nebeží: `sudo systemctl start bluetooth`. Párovanie potrebuje program, ktorý sa spýta na
prístupový kľúč (passkey) – párovacieho agenta, zvyčajne nastavenia Bluetooth pracovného prostredia. Pozrite si
[Bluetooth LE](bluetooth.md#párovanie).

<a id="segger-j-link-software"></a>
## SEGGER J-Link Software

Nainštalujte **J-Link Software and Documentation Pack 8.00 alebo novší** z webu spoločnosti SEGGER. HARDWARIO Monitor
ho nájde v predvolených umiestneniach:

| Systém | Prehľadáva sa |
|---|---|
| Windows | cesta inštalácie v registri (`SOFTWARE\SEGGER\J-Link`), `C:\Program Files\SEGGER\JLink*` |
| macOS | `/Applications/SEGGER/JLink`, `/Applications/SEGGER/JLink_V*` |
| Linux | `/opt/SEGGER/JLink`, `/opt/SEGGER/JLink_V*` |

Ak je nainštalovaných viac verzií, použije sa najnovšia. Ak chcete použiť inú kópiu, zvoľte **Custom path** (vlastná
cesta) v **Settings → J-Link → Library**, alebo v príkazovom riadku zadajte `--jlink-library PATH` či nastavte
`HWIO_JLINK_LIBRARY`. Poradie je: `--jlink-library`, potom `HWIO_JLINK_LIBRARY`, potom nastavenie, potom predvolené
umiestnenia. Cesta, ktorá neexistuje, je chyba; HARDWARIO Monitor sa potom nevráti k predvoleným umiestneniam.

<a id="nordic-nrf-util-lte-modem-firmware-only"></a>
## Nordic nRF Util (len firmvér LTE modemu)

nRF Util aktualizuje firmvér modemu v LTE MCU. Všetko ostatné funguje aj bez neho. V aplikácii otvorí tlačidlo
**Download nRF Util ↗** (Stiahnuť nRF Util) správne stiahnutie pre váš počítač.

**Windows**

1. Stiahnite [nrfutil.exe][nrfutil-win] do priečinka **Stiahnuté súbory** (Downloads).
2. Otvorte PowerShell v priečinku Stiahnuté súbory (`cd $HOME\Downloads`) a spustite:

   ```powershell
   .\nrfutil.exe install device
   ```

Prípadne spustite `winget install --id NordicSemiconductor.nrfutil` a potom v novom termináli `nrfutil install device`.
Ak Windows hlási, že chýba `VCRUNTIME140.dll` (alebo `nrfutil.exe` skončí bez výpisu), nainštalujte
[Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe), alebo použite winget, ktorý ho nainštaluje sám.

**macOS**

1. Stiahnite [nrfutil][nrfutil-mac] (jeden súbor pre Intel aj Apple silicon) do priečinka **Stiahnuté súbory**
   (Downloads).
2. Otvorte Terminál a spustite:

   ```sh
   cd ~/Downloads
   chmod +x nrfutil
   ./nrfutil install device
   ```

   Ak macOS hlási, že nrfutil nemožno otvoriť, otvorte **Systémové nastavenia › Súkromie a bezpečnosť**, pri nrfutil
   kliknite na **Napriek tomu otvoriť** a spustite `./nrfutil install device` znova. Prípadne najprv v tom
   istom Termináli spustite `xattr -d com.apple.quarantine nrfutil`.

**Linux**

1. Stiahnite nrfutil pre [x86-64][nrfutil-linux] alebo [ARM64][nrfutil-linux-arm] do priečinka **Stiahnuté súbory**
   (Downloads).
2. Otvorte terminál a spustite:

   ```sh
   cd ~/Downloads
   chmod +x nrfutil
   ./nrfutil install device
   ```

Prvé spustenie potrebuje internet: nrfutil nainštaluje sám seba a svoj príkaz `device` do priečinka `.nrfutil` vo
vašom domovskom priečinku (`~/.nrfutil/bin`, vo Windows `%USERPROFILE%\.nrfutil\bin`). HARDWARIO Monitor ho tam nájde
sám; `PATH` meniť nemusíte. Potom kliknite na **Check again** (Skontrolovať znova) v **Settings → nRF Util**, alebo
spustite `hwio doctor`. Stiahnutý súbor potom môžete zmazať. Iné systémy: [stránka na stiahnutie nRF Util][nrfutil-page].

**Pre pokročilých.** HARDWARIO Monitor hľadá nRF Util v **Settings → nRF Util → Path** (alebo `--nrfutil PATH`, alebo
`HWIO_NRFUTIL`), potom v `PATH`, potom v `~/.nrfutil/bin` (`%USERPROFILE%\.nrfutil\bin` vo Windows) a `/usr/local/bin`.
Ak je príliš starý: `~/.nrfutil/bin/nrfutil self-upgrade` / `~/.nrfutil/bin/nrfutil upgrade device` (Windows: `& "$HOME\.nrfutil\bin\nrfutil.exe" self-upgrade` / `& "$HOME\.nrfutil\bin\nrfutil.exe" upgrade device`).

[nrfutil-win]: https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-pc-windows-msvc/nrfutil.exe
[nrfutil-mac]: https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/universal-apple-darwin/nrfutil
[nrfutil-linux]: https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil
[nrfutil-linux-arm]: https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/aarch64-unknown-linux-gnu/nrfutil
[nrfutil-page]: https://www.nordicsemi.com/Products/Development-tools/nrf-util

## Kontrola inštalácie

```sh
hwio doctor
```

`hwio doctor` skontroluje J-Link Software, nRF Util, prístup k J-Linku cez USB (Linux), iné programy, ktoré držia
sondu, pripojené sondy a ich firmvér, priečinky nastavení a logov a Bluetooth. Pri každom probléme uvedie, čo robiť.
V aplikácii sú tie isté kontroly súčasťou diagnostickej správy (**Help → Create diagnostic report…**) a
**Settings → J-Link** a **Settings → nRF Util** ukazujú, čo sa našlo.

Ďalej: [Začíname](getting-started.md).

[releases]: https://github.com/hardwario/monitor/releases/latest
