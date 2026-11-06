# 0040 — CHESTER LoRaWAN module (`lrw` target, prepared firmware)

- **Status:** Draft
- **Milestone:** 2.0.0-rc.4 (user decision 2026-11-06)
- **Author:** Matej Strnad (with Claude), 2026-11-06
- **Related ADRs:** ADR-0037, ADR-0038, ADR-0057, ADR-0077, ADR-0165, ADR-0174, ADR-0274 … ADR-0277; accepted
  here: ADR-0278
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
| D2 | If the downloader cannot follow GitHub's redirect | allow HTTPS cross-host redirects for designated firmware / re-host on docs.hardwario.com | proposed: allow HTTPS → HTTPS redirects to `*.githubusercontent.com` only | — |
| D3 | Card name | "LoRaWAN module" / "LoRaWAN modem" / "LRW module" | proposed: "LoRaWAN module" | — |
| D4 | Check module after flashing | offer "Check module" over the app console / no check | proposed: offer it (needs the app's `lrw` command name, P0.4) | — |
| D5 | Flash wizard (0010) | add the third choice in rc.4 / later | proposed: later (0010 is already in rc.4 scope) | — |
| D6 | CHESTER v2 LoRaWAN module | own part + firmware when known / same part | open: the user to tell which chip `ctr_lrw_v2` uses | — |

## Implementation steps

### Phase 0 — HIL (with a CHESTER board that has the ABZ module)

- [ ] P0.1 Where is the module's SWD (own connector / test pads / shared with the app connector)? Photo for the
      connection guide illustration (via Git LFS) — user.
- [ ] P0.2 DEV_ID `0x447`, flash size 192 KiB, RDP level of a factory module and of a module with lora-modem.
- [ ] P0.3 Flash lora-modem 1.4.1 with J-Link Commander once by hand (after a backup where readable); confirm the
      module works with the CHESTER application afterwards.
- [ ] P0.4 The application-shell command that proves the module answers (`lrw …`), for D4.
- [ ] P0.5 Does lora-modem keep anything in data EEPROM (`0x08080000`) that CHESTER needs after a mass erase?

### Phase A — backend and CLI

- [ ] A1 Download the asset once, measure SHA-256, add `loraModemFirmware` to the profile; unit test for the entry.
- [ ] A2 Target `lrw` in the CHESTER profile + guide + ranges; profile invariants test (0039 A1) covers it.
- [ ] A3 Downloader redirect rule (D2) with tests (redirect to an allowed host, to a disallowed host, HTTP).
- [ ] A4 `hwio chester lrw …`; CLI end-to-end tests on the fake J-Link (prepared firmware from a fake catalog).

### Phase B — GUI

- [ ] B1 Third card, kind classification and mismatch hint; cable-move prompts for cross-target `lrw`.
- [ ] B2 Maintenance for `lrw`; Unlock confirmation text; "Check module" (D4).
- [ ] B3 Qt Quick tests, screenshots (fake J-Link kit), translations, docs section "LoRaWAN module" (5 languages).

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
