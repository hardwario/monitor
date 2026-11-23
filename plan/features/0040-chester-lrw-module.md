# 0040 — CHESTER LoRaWAN module (`lrw` target, prepared firmware)

- **Status:** In progress (approved 2026-11-06 with all recommended options; D6 stays open; D7/D8 decided
  2026-11-09 after Phase 0, ADR-0352)
- **Milestone:** 2.0.0-rc.5 (user decision 2026-11-19; was 2.0.0-rc.4, user decision 2026-11-06)
- **Author:** Matej Strnad (with Claude), 2026-11-06
- **Related ADRs:** ADR-0037, ADR-0038, ADR-0057, ADR-0077, ADR-0165, ADR-0174, ADR-0274 … ADR-0277; accepted
  here: ADR-0278, ADR-0281, ADR-0352
- **Depends on:** [0039](0039-stm32-flashing.md) (STM32 framework)

## Context

CHESTER boards with LoRaWAN use the Murata Type ABZ module (CMWX1ZZABZ, inside an STM32L072CZ + SX1276). HARDWARIO
uses the open firmware [hardwario/lora-modem](https://github.com/hardwario/lora-modem) (BSD-3-Clause), which has a
build variant for CHESTER. Updating it today needs a USB-UART adapter and the `bcf` tool, or STM32CubeProgrammer.
The user wants the module flashable over SWD with J-Link (decision 2026-11-05) and its firmware **prepared** in the
app (2026-11-06).

### Facts verified for this plan

| Fact | Source |
|---|---|
| Latest lora-modem release: **v1.4.1** (2024-05-18); CHESTER asset `lora-modem-1.4.1.chester.hex` (310 263 B, Intel HEX) and `.chester.bin` (110 280 B); `.debug.*` variants exist. URL `https://github.com/hardwario/lora-modem/releases/download/v1.4.1/lora-modem-1.4.1.chester.hex`. The GitHub API publishes no digest for these assets. Licence BSD-3-Clause. | `gh release list` / `gh api repos/hardwario/lora-modem/releases/tags/v1.4.1`, 2026-11-06 |
| The original Murata firmware protects the flash on first boot (the lora-modem guide flashes with `--unprotect`). | lora-modem wiki "LoRa Module Firmware Update" |
| CHESTER's docs name the module CMWX1ZZABZ-078; the lora-modem wiki lists -078 as "OpenMCU, no firmware" — the two sources disagree about the factory state. | docs.hardwario.com "LoRaWAN radio"; lora-modem wiki "Type ABZ Modules" |
| On CHESTER the application MCU talks to the module over UART (chester-sdk `ctr_lrw`, shell `lrw …`); LoRaWAN keys are set with `lrw config …` on the application MCU. Whether the module keeps state in its own data EEPROM that a mass erase loses is **not verified**. | ADR-0186 (shell command list) |

## Goals / Non-goals

**Goals**

- G1 CHESTER profile target **`lrw`** ("LoRaWAN module"): STM32 vendor, part STM32L072CZ (shared part data from
  0039), connection guide, no RTT console.
- G2 The prepared firmware **lora-modem 1.4.1 (CHESTER)** pinned in the profile, downloaded and SHA-256 verified on
  first use, cached (ADR-0278, as ADR-0165).
- G3 GUI: third card **LoRaWAN module** on the CHESTER Flash page; Maintenance actions for `lrw` (erase, unlock,
  reset). The flash wizard (0010) offers it as a third choice only if 0010 is not frozen for rc.4 (decision D5).
- G4 CLI `hwio chester lrw flash [FILE] | erase | unlock | reset | info` (ADR-0277).
- G5 After flashing, an optional check that the CHESTER application sees the module (decision D4).

**Non-goals**

- No LoRaWAN configuration (keys, region) — that is the application firmware's shell.
- No Murata original firmware; no UART bootloader path.
- The CHESTER-S1 IAQ board controller (STM32L0 cat. 3, Clime IAQ second PCB): generic STM32 device only until its
  firmware is available (ADR-0279).

## User experience

- CHESTER Flash page, step 1: **Application firmware · LTE firmware · LoRaWAN module**. The LoRaWAN card shows
  "lora-modem 1.4.1 for CHESTER (recommended)" and **Use your own file…** (`.hex`, `.bin`).
- The card always shows the cabling note from the connection guide: "Connect the J-Link to the programming connector
  of the LoRaWAN module." (wording and illustration slot `chester-lrw-cable` until HARDWARIO provides connector
  names, like open question 22).
- A cross-target operation (tab on `app`) follows ADR-0057: pause RTT, connect to `lrw`, flash, reconnect to `app`.
  Because the module very likely has its own connector (to confirm in P0.1), the page asks the user to move the cable
  before and after, like the LTE update wizard (ADR-0234), instead of failing with `target.wrong_chip`.
- A factory module with Murata firmware is RDP level 1: the card shows the protected state and **Unlock…**
  (ADR-0276) before Flash is enabled; the confirmation adds "The module's original Murata firmware is removed."
- Result: "LoRaWAN module flashed with lora-modem 1.4.1." With D4 = yes, the page then offers **Check module**:
  reconnects to `app`, sends `lrw` status command over RTT and shows whether the module answered.

## CLI

```
hwio chester lrw info  [--json]            # DEV_ID, flash size, RDP level
hwio chester lrw flash [FILE] [--address ADDR] [--halt]   # no FILE: prepared lora-modem 1.4.1
hwio chester lrw erase [--yes]
hwio chester lrw unlock [--yes]
hwio chester lrw reset [--halt]
```

Output and errors as `hwio stm32` (0039). `chester lrw flash` without FILE prints the prepared firmware name and
version before downloading.

## Design

- `libs/device/src/profiles/chester.cpp`: target `lrw` — `vendor = Stm32`, `parts = {stm32l072cz}` (from
  `stm32_parts.cpp`), `rttTerminal = ""`, `connectionGuide = lrwGuide`, `designatedFirmware = loraModemFirmware`,
  `defaultErase = All` (STM32 mass erase), memory-write ranges for M6 target operations (RAM `0x20000000`–
  `0x20005000`, peripherals, `writeDenied` = the FLASH interface `0x40022000`–`0x40022400`, FPB, SCS; to confirm
  against RM0367 in P0).
- `DesignatedFirmware{ .name = "lora-modem for CHESTER", .version = "1.4.1", .url = <GitHub asset>, .sha256 =
  <measured in A1>, .kind = AppHex }`. Cache file `lrw-<sha256>.hex`.
- The ADR-0037 downloader must follow the GitHub redirect to `objects.githubusercontent.com` (HTTPS → HTTPS); if the
  downloader allows no cross-host redirect today, ADR-0278 option "HARDWARIO-hosted copy" is the fallback (D2).
- `ConsoleSession` / tabs: `lrw` is never a console target (ADR-0233 keeps `app`).
- GUI: `FlashViewModel::Kind::LrwFirmware`, kind-mismatch hint between the three cards (a lora-modem file on
  Application firmware → "This is LoRaWAN module firmware — switch?"); classification by the image's address range
  (`0x08000000` STM32 flash vs `0x0` nRF), ADR-0040 extended.
- MCP: the existing `flash` / `erase` tools accept `target: "lrw"`; `unlock` as 0039 D9.

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D1 | Prepared firmware for the ABZ module | lora-modem 1.4.1 chester / HARDWARIO-hosted copy / other | **lora-modem 1.4.1 `chester.hex` from the GitHub release** (2026-11-06) | ADR-0278 |
| D2 | If the downloader cannot follow GitHub's redirect | allow HTTPS cross-host redirects for designated firmware / re-host on docs.hardwario.com | **Allow HTTPS → HTTPS redirects to `*.githubusercontent.com` only** (approved 2026-11-06) | ADR-0281 |
| D3 | Card name | "LoRaWAN module" / "LoRaWAN modem" / "LRW module" | **"LoRaWAN module"** (approved 2026-11-06) | ADR-0281 |
| D4 | Check module after flashing | offer "Check module" over the app console / no check | **Offer "Check module"** (approved 2026-11-06; command from P0.4) | ADR-0281 |
| D5 | Flash wizard (0010) | add the third choice in rc.4 / later | **Later (after rc.4)** (approved 2026-11-06) | ADR-0281 |
| D6 | CHESTER v2 LoRaWAN module | own part + firmware when known / same part | open: the user to tell which chip `ctr_lrw_v2` uses | — |
| D7 | Phase 0: the module answers only while the application MCU is held | guided cable moves (as ADR-0234) / flash only, docs explain | **Guided cable moves** (2026-11-09) | ADR-0352 |
| D8 | Phase 0: "Check module" needs a config change (`lrw config test true`) | hide it in rc.4 / use test mode briefly | **Hidden in rc.4** (2026-11-09; supersedes the D4 "offer" for rc.4) | ADR-0352 |
| D9 | Guided Reset of the LoRaWAN module in the GUI | keep [Reset] in the `lrw` maintenance / drop | **Drop** (2026-11-10, user): the final reset of the application MCU in the guided flow restarts the module anyway; the CLI `hwio chester lrw reset` stays (direct) | ADR-0352 (amended) |

## Implementation steps

### Phase 0 — HIL (with a CHESTER board that has the ABZ module)

- [ ] P0.1 Where is the module's SWD (own connector / test pads / shared with the app connector)? Photo for the
      connection guide illustration (via Git LFS) — user.
- [x] P0.2 DEV_ID `0x447`, flash size 192 KiB, RDP level of a factory module and of a module with lora-modem.
- [x] P0.3 Flash lora-modem 1.4.1 with J-Link Commander once by hand (after a backup where readable); confirm the
      module works with the CHESTER application afterwards.
- [x] P0.4 The application-shell command that proves the module answers (`lrw …`), for D4.
- [ ] P0.5 Does lora-modem keep anything in data EEPROM (`0x08080000`) that CHESTER needs after a mass erase?

#### Phase 0 results (2026-11-06)

- **Reachability:** the user's board has the module on its own "LoRaWAN" SWD connector. The module did **not**
  answer (no DAP, also not with connect-under-reset) while the CHESTER application firmware ran; after a reset +
  halt of the nRF52840 over the APP connector it answered. The application MCU drives the module's reset / power.
  → The cross-target flow needs the application MCU held (reset + halt) before the module is reached; this
  changes the UX section ("move the cable") and needs a user decision before Phase B (asked at 0040 start).
- **P0.2:** factory module = DEV_ID `0x447`, REV_ID `0x2008`, 192 KiB, **RDP level 1** (settles the source
  disagreement for this board: protected Murata firmware, not "no firmware"). After lora-modem: RDP 0.
- **P0.3:** unlocked and flashed with `hwio stm32 unlock` / `hwio stm32 flash --chip stm32l0-cat5-192k` (0039
  CLI) instead of J-Link Commander; read-back matched. lora-modem-1.4.1.chester.hex: 310 263 B, SHA-256
  `e9215b222b68092c33d9a31330bdec6ebea1afaf1cd1844f74cd8745c46ff2e6`, flash records only (0x08000000-0x0801AEC8).
- **P0.4 (open):** the CHESTER application's shell has `lrw config|start|join|test`; `lrw test cmd <AT>` talks to
  the modem but needs `lrw config test true` (a configuration change); the app on this board does not start the
  LoRaWAN interface by itself (boot log: only `ctr_lrw: init`). Whether the module works with the application was
  not confirmed yet.

### Phase A — backend and CLI

- [x] A1 Download the asset once, measure SHA-256, add `loraModemFirmware` to the profile; unit test for the entry.
      Implementation notes: the entry is on the `lrw` target (not on the shared part, so the generic STM32
      chip of the same part offers no firmware); `DesignatedFirmware::cacheTag` ("lte", "lrw") names the
      cache file (`lrw-<sha256>.hex`, the LTE bundle stays `lte-<sha256>.zip`); a `FirmwareKind::AppHex`
      designated download yields `DesignatedDownload::image`.
- [x] A2 Target `lrw` in the CHESTER profile + guide + ranges; profile invariants test (0039 A1) covers it.
      Implementation notes: `stm32Target()` and the STM32L0 memory-write ranges moved to `stm32_parts.h`
      (the guide and designated firmware are parameters); placeholder illustration `chester-lrw-cable`
      (LFS). Places that assumed app + lte: `LteUpdateProcedure` (target `lte` by name), Flash page
      `maintenanceTargets` (no `lrw` until B2: the module needs the guided flow), wizard illustration list.
- [x] A3 Downloader redirect rule (D2) with tests (redirect to an allowed host, to a disallowed host, HTTP).
      Implementation notes: `designatedRedirectAllowed()` (https on the same host or `*.githubusercontent.com`);
      catalog downloads keep "any https". Test-only `HWIO_DESIGNATED_REDIRECT_HOST` (HWIO_TEST_OVERRIDES) lets
      the local http fixture stand in for the asset host.
- [x] A4 `hwio chester lrw …`; CLI end-to-end tests on the fake J-Link (prepared firmware from a fake catalog).
      Implementation notes: `app/cli/src/lrw_commands.cpp` reuses `stm32_commands` with the target
      `{chester, lrw}`; flash / erase / unlock are guided (LrwUpdateProcedure) in an interactive terminal,
      `--direct` otherwise (usage error naming `--direct`); `flash --halt` needs `--direct` (the guided flow
      restarts the application MCU, which restarts the module); `flash --no-cache`; info / reset /
      `--direct` failures to reach the module (target.connect_failed) suggest `hwio chester app reset
      --halt`. The guided erase / unlock ask once (or `--yes`); a protected module before a guided flash
      asks "Unlock the LoRaWAN module (it is erased)?". MCP: `flash`, `erase` and the `stm32_*` tools
      accept `target: "lrw"` (test).

- [x] A5 `LrwUpdateProcedure` (ADR-0352): hold app → cable to LoRaWAN → identify / confirm unlock → flash | erase |
      unlock → cable back → reset app; used by the GUI card and `hwio chester lrw flash|erase|unlock` (`--direct`
      skips it); `LteUpdateProcedure` names its target `lte`.
      Implementation notes: steps ConnectApp, HoldApp, ConnectLrw, NeedsUnlockConfirmation (RDP 1 before
      flash / erase; the action Unlock needs no second confirmation), Unlock, Flash, Erase, ConnectAppAgain,
      ResetApp; a failed operation step is retryable, cancel only in waiting steps; connector labels "APP"
      and "LoRaWAN". The fake J-Link's connectors model gains `"needsHalted": "app"` (the module answers only
      while the app CPU is held; the hold survives a new library session, as on hardware).

### Phase B — GUI

- [x] B1 Third card, kind classification and mismatch hint; cable-move prompts for cross-target `lrw`.
      Implementation notes: `FirmwareSourceViewModel::Kind::LrwFirmware` (target `lrw`, recent kind
      "lrw"); the designated-firmware part follows the target of the current kind (LTE or LoRaWAN; a
      kind with another target forgets the other download), an AppHex download becomes an Intel HEX
      source. The card (shown where the profile has `lrw`): "lora-modem for CHESTER 1.4.1
      (recommended)" (the profile's name + version, as the LTE card) or "Use your own file…" (.hex,
      .bin with the start address); the Options card shows "Erase the whole chip first" / "Keep data
      EEPROM" (no halt); the source card always shows the `lrw` connection guide's cabling note plus
      "Flash first asks for the APP connector…". Kind hints by address range: an Intel HEX starting
      in 0x08000000-0x0FFFFFFF on Application / LTE firmware → "This is LoRaWAN module firmware —
      switch?"; an nRF HEX on the LoRaWAN card → "This is not LoRaWAN module firmware — switch to
      Application firmware?"; a .bin on Application / LTE firmware suggests the LoRaWAN card. Flash
      opens the new wizard `lrw-update` (`WizardId::LrwUpdate`, `LrwUpdateWizard`,
      `LrwUpdateStep.qml`) with the page's request (`FlashViewModel::takeLrwRequest()`); it starts
      `LrwUpdateProcedure` at once: APP connector · Stop the application · LoRaWAN connector ·
      Flash (unlock confirmation of an RDP 1 module: the procedure's text "…the module's original
      Murata firmware is removed.", [Unlock and erase]) · APP connector · Done ("LoRaWAN module
      flashed with <name>."). Kind cards: three columns from 1000 px.
- [x] B2 Maintenance for `lrw`; Unlock confirmation text; "Check module" (D4) — hidden in rc.4 (D8, ADR-0352).
      Implementation notes: with the expert actions the MCU choice lists "LoRaWAN module"; its
      [Erase chip…] and [Unlock (erase)…] open the `lrw-update` wizard (the ConfirmDialog
      first for erase / unlock; the unlock's consequence adds "The module's original Murata firmware
      is removed."); no [Reset] / [Reset and halt] / [Erase application] for it. No "Check module".
      2026-11-10 (D9, user): the guided [Reset] of the module (action Reset, step ResetModule of
      `LrwUpdateProcedure`, its wizard texts and test) was removed again: the final reset of the
      application MCU restarts the module; `FlashViewModel::resetAvailable` hides both reset buttons
      for `lrw`. The CLI `hwio chester lrw reset` never used it (it connects directly).
- [ ] B3 Qt Quick tests, screenshots (fake J-Link kit), translations, docs section "LoRaWAN module" (5 languages).
  - [x] Tests: view-model GTests `LrwFlashTest` (card, designated download + guided flash, kind hints,
        RDP 1 unlock in the wizard, maintenance actions, wizard without a request), `LrwUpdate.*`
        (the module Reset test was removed with D9), Qt Quick `tst_lrw.qml` (fake J-Link board with
        the APP and LoRaWAN connectors, `"needsHalted": "app"`). Note: the fake's CTRL-AP answers
        behind any connector, so the wizard's APP-connector check can pass before the cable is back
        (the tests accept it).
  - [ ] Screenshots, translations, docs (5 languages).

- [x] B4 Prepared firmware without a "Downloading…" flash (user report 2026-11-19, approved by Matej Strnad the same
      day). Implemented 2026-11-19, verified 2026-11-23: debug 1944/1945 (the one failure was
      `McpControllerTest.TokenFileErrorAndRegenerate`, a port collision with a running Monitor), qmllint,
      `check_translations`, tidy 0 findings, ASan 1944/1944, TSan 1871/1871 with the three new download tests left
      out of the TSan run like the other QNetworkAccessManager tests (approved by the user 2026-11-23). The entries
      belong to the device profile: they are cleared (running downloads cancelled) when the tab's profile changes;
      another device with the same profile keeps them. Finding: the file is cached on disk and fetched from the network once, but (1)
      `FirmwareSourceViewModel::setKind` discards the prepared firmware of the other card (`resetDesignated()`), so
      every switch between the LTE and LoRaWAN cards loads it again and cancels a running download of the other
      card, and (2) a cache hit is shown as "Downloading…" until its asynchronous result arrives.
  - The view model keeps the designated state **per target** (`lte`, `lrw`): state, running operation, progress,
    loaded source and error move from single members into one bounded map keyed by the target id (at most the
    targets of the profile that have `designatedFirmware`; today 2). Switching the card neither resets nor cancels
    the other card's entry; the properties (`designatedState`, `designatedStatus`, `designatedProgress`,
    `designatedError`, …) read the entry of the current kind. `cancelDesignated()` / `retryDesignated()` act on the
    current kind's entry. The entries belong to the device profile (the designated firmware is a property of the profile's target, not of
    the connected device): a change of the profile clears all entries and cancels their downloads; another device
    of the same profile keeps them (corrected 2026-11-19: the earlier wording "or of the tab's device" described a
    reset the code never had).
  - `firmware::DesignatedOperation` gains `[[nodiscard]] bool fromCache() const noexcept` (true when
    `FirmwareCatalog::downloadDesignated` found a verified cache file and only the asynchronous hand-over is
    pending). For such an operation the view model enters a new state `DesignatedLoading` instead of
    `DesignatedDownloading`: `designatedStatus()` is empty, no progress bar; the Flash button's reason stays
    "… is not downloaded yet" for that moment. The cache format, the SHA-256 re-check of every cache hit and the
    CLI are unchanged.
  - Tests: view-model GTests — switch LTE → LoRaWAN → LTE with both cached: no second catalog request for LTE and
    its state stays Ready; a running LTE download survives a switch to LoRaWAN and back; a cache hit never reports
    `DesignatedDownloading`; `DesignatedOperation::fromCache()` in the catalog tests. No user-visible string
    changes (no translation work).

### Phase C — HIL

- [ ] C1 Factory module (RDP 1): Unlock → flash prepared firmware → module works with the CHESTER application.
- [ ] C2 Re-flash over an existing lora-modem (RDP 0) from GUI and CLI; own `.hex` and `.bin`.
- [ ] C3 Cross-target from an `app` tab with the cable move; RTT re-attaches afterwards.

## Tests and verification

As 0039 (unit, CLI e2e, Qt Quick, sanitizers, clang-tidy, formatting, translations), plus the HIL phase C on Linux and
one manual flash on Windows and macOS.

## Risks and open points

- The factory state of CMWX1ZZABZ-078 on CHESTER is unclear (the sources disagree); P0.2 settles it.
- lora-modem 1.4.1 is from 2024; a newer release needs a profile change and an app release (ADR-0165 model).
- Pinning a GitHub release asset relies on GitHub keeping it; the SHA-256 guards the content, not the availability.
- The BSD-3-Clause notice: the firmware is downloaded, not distributed with the app, so no notice is needed in the
  packages; the docs name the project and its licence.
