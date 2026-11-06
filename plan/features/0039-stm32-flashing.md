# 0039 — STM32 flashing over J-Link (framework, generic STM32 device)

- **Status:** In progress (approved by the user 2026-11-06 with all recommended options; Phase 0 done on the
  CHESTER-S1 controller 2026-11-06, except P0.2 on the ABZ module)
- **Milestone:** 2.0.0-rc.4 (user decision 2026-11-06)
- **Author:** Matej Strnad (with Claude), 2026-11-06
- **Related ADRs:** ADR-0015, ADR-0031, ADR-0032, ADR-0035–ADR-0040, ADR-0050, ADR-0057, ADR-0099, ADR-0160,
  ADR-0165, ADR-0174; accepted here: ADR-0274 … ADR-0277, ADR-0279, ADR-0280, ADR-0282 (Phase 0)
- **Depends on:** [0003](0003-m2-flashing.md) (flash pipeline), [0006](0006-m3-flash-settings.md) (Flash page),
  [0013](0013-m6-mcp-core-cli.md) (target operations)
- **Followed by:** [0040](0040-chester-lrw-module.md) (CHESTER LoRaWAN module: `lrw` target, prepared firmware)

## Context

Today v2 flashes only Nordic MCUs: the CHESTER application MCU (nRF52840) and the LTE MCU (nRF9160 / nRF9151).
HARDWARIO devices also carry **STM32** chips — the Murata Type ABZ LoRaWAN module on CHESTER (STM32L072) and other
STM32L0 chips on CHESTER boards — that need flashing in production, service and development. Today this needs
STM32CubeProgrammer, the `bcf` tool over UART or J-Link Commander.

The user's request (2026-11-06): *"These are STM32 chips; the user picks which one it is and the app flashes it
accordingly. For the ones on CHESTER, have the firmware prepared."* This plan is the **framework** (STM32 support in
the J-Link adapter, the profile model, a generic STM32 device, CLI, GUI, MCP). Per-chip plans add CHESTER targets with
prepared firmware (0040 and later).

The backend is built only on the JLinkARM library (ADR-0015): J-Link's own flash loaders program STM32 flash, so no
new dependency is needed. Neither py-hardwario nor the old app flash STM32; there is no behavioural reference.

### Facts verified for this plan

| Fact | Source |
|---|---|
| A CHESTER STM32 chip connected over SWD to a J-Link (2026-11-06): CPUID `0x410CC601` (Cortex-M0+ r0p1), DBGMCU_IDCODE @ `0x40015800` = `0x10386417` → **DEV_ID `0x417`** (STM32L0 category 3: L051/L052/L053/L062/L063), REV_ID `0x1038`; FLASH_OPTR @ `0x4002201C` = `0x807000AA` → **RDP level 0**; flash size @ `0x1FF8007C` (16 bit) = `0x0040` → 64 KiB; vector table SP `0x20002000` → 8 KiB RAM; application present (reset vector `0x08005E39`, Phase 0 dump; the first read noted `0x08003AE9`). Running: about 1 in 40 reads of IDCODE and of the flash size register return 0 (P0.3); halted: always correct. | J-Link Commander 9.84, `connect` + `mem32` (read only), developer machine; Phase 0 2026-11-06 |
| The Murata Type ABZ (CMWX1ZZABZ) contains an STM32L072CZ (192 KiB flash, 20 KiB RAM, 6 KiB data EEPROM; DEV_ID `0x447`). Its original Murata firmware sets read-out protection on first boot; hardwario/lora-modem's flashing guide therefore uses `--unprotect`. | hardwario/lora-modem wiki "Type ABZ Modules", "LoRa Module Firmware Update" (read 2026-11-05); DEV_ID `0x447` confirmed (OpenOCD stm32lx.c; ST cmsis_device_l0), on hardware in P0.2 |
| STM32L0 read-out protection: RDP byte in FLASH_OPTR (`0xAA` = level 0, `0xCC` = level 2 = permanent, else level 1). Level 1 → 0 ("regression") is done by programming the option bytes with RDP = `0xAA` and reloading them; the chip then **mass-erases flash and data EEPROM**. Level 2 cannot be undone. | Confirmed in Phase 0: ST stm32l0xx_hal_driver (`OB_RDP_LEVEL_0/1/2` = `0xAA`/`0xBB`/`0xCC`, RDP word = value \| ~value << 16), OpenOCD stm32lx.c, and on the S1 (level 1 → 0 erased flash + EEPROM; ADR-0282) |

### STM32 chips on HARDWARIO products (known so far)

| Product | Chip | Role | Prepared firmware |
|---|---|---|---|
| CHESTER LoRaWAN (Murata Type ABZ) | STM32L072CZ (cat. 5, DEV_ID `0x447`) | LoRaWAN modem, UART AT commands | lora-modem 1.4.1 (plan 0040, ADR-0278) |
| CHESTER Clime IAQ, second PCB (**CHESTER-S1**) | STM32L0 cat. 3, 64 KiB (DEV_ID `0x417`, measured above) | IAQ sensor board controller on I2C (chester-sdk driver `ctr_s1`): CO₂ (target / ABC calibration), temperature, humidity, illuminance, pressure / altitude, PIR motion, button, RGB LED, buzzer; registers for serial number, HW revision / variant, FW version | **none yet** — the firmware source is unknown (user, 2026-11-06); flashed as a generic STM32L0 part until HARDWARIO provides it |
| TOWER Core Module (not a CHESTER) | STM32L083CZ (cat. 5) | application MCU | none (out of scope; listed because it is the same family) |

All of them are **STM32L0**: same DBGMCU_IDCODE address, same flash interface, same RDP mechanism and option-byte
layout. The framework therefore describes the protection and identification **once per family** and the parts as
small entries (DEV_ID, flash size, J-Link device name, ranges) — ADR-0279.

## Goals / Non-goals

**Goals**

- G1 Flash, verify, erase, reset an STM32 chip through J-Link from a **curated, compiled-in list of STM32 parts**
  (ADR-0275), covering the **whole STM32L0 family** generically (ADR-0279): one part entry per category and flash
  size — cat. 1 (DEV_ID `0x457`, L011 / L021), cat. 2 (`0x425`, L031 / L041), cat. 3 (`0x417`, L051 / L052 / L053 /
  L062 / L063), cat. 5 (`0x447`, L071 / L072 / L073 / L081 / L082 / L083), each in its flash sizes (confirmed in
  Phase 0, see "Phase 0 results"; the STM32L010 value line is only named in the hint, ADR-0282). Each part carries its J-Link device name, ID register and expected DEV_ID, flash / EEPROM / RAM ranges
  and its protection data.
- G2 The user **picks the chip**; after connecting the app reads DBGMCU_IDCODE and **stops with a clear error when it
  is another chip** (`target.wrong_chip`, ADR-0275).
- G3 Read-out protection is detected and explained; the app **offers "Unlock (erases the chip)"** with a confirmation,
  never automatically (ADR-0276). RDP level 2 is reported as permanent.
- G4 A generic **STM32** device in Add device (profile `stm32`; one target per curated part) for chips that are not on
  a known HARDWARIO product, flashing the user's own `.hex` / `.bin` file (ADR-0274).
- G5 CLI `hwio stm32 …` and MCP tools with the same backend (ADR-0277).
- G6 The profile model supports per-chip prepared firmware so that per-chip plans (0040) only add data. Adding the
  CHESTER-S1 firmware later = one `DesignatedFirmware` entry (and, if wanted, a CHESTER target `s1`) in a small
  follow-up plan; no framework change.
- G7 The family data is the extension point: another STM32 family (e.g. STM32WL, STM32G0) later = one family entry
  (IDCODE address, flash interface, option-byte layout) + its parts, by a new plan (not in rc.4).

**Non-goals**

- No STM32 UART / USB DFU bootloader, no ST-LINK, no STM32CubeProgrammer (ADR-0015: J-Link only).
- No free-form J-Link device names and no "all STM32 devices from the J-Link list" (user decision: curated list).
- No option-byte editor beyond the RDP level 1 → 0 regression; no write protection (WRP) / PCROP management except
  reporting it as a cause of a failed flash.
- No RTT console for STM32 targets in this plan (lora-modem has none; the tab shows Flash / Maintenance only).
- No STM32 PIB (the PIB stays a Nordic UICR concept).

## User experience

### Add device (generic STM32)

The Add device picker (ADR-0050) gets a third profile **STM32 chip**. Its target picker becomes **Chip** and lists the
curated parts, grouped by family and named by category and flash size with the known HARDWARIO use as a hint
("STM32L0 cat. 5, 192 KiB — e.g. Murata Type ABZ (CHESTER LoRaWAN)", "STM32L0 cat. 3, 64 KiB — e.g. CHESTER-S1
(Clime IAQ)", …). Chips of one category and size share a DEV_ID and cannot be told apart by the app; one entry covers
them. Connect opens
a tab without a console: the tab shows **Flash** and **Maintenance**; the Console page shows "This chip has no console
in HARDWARIO Monitor." (decision D8).

### Flash page for an STM32 chip

- Step 1 has one card **Firmware for <chip>** (no kind choice). It accepts `.hex` and `.bin`; a `.bin` needs a start
  address, prefilled with the part's flash start (`0x08000000`) (decision D5).
- When the part has prepared firmware (only on CHESTER targets, plan 0040), the card preselects it as on the LTE card
  (ADR-0165): "<name> <version> (recommended)" and **Use your own file…**.
- Options: "Erase the whole chip first" (default off; a normal flash erases only the touched sectors through the J-Link
  flash loader) and "Keep data EEPROM" (default on; off when erasing all).
- Progress and result as ADR-0049 (Connecting → Identifying → Programming → Verifying → Resetting).

### Wrong chip

`target.wrong_chip`: "The connected chip is <found> (ID 0x447), not the selected <chosen>." Why: "The probe is
connected to another chip, or a different chip was chosen." What to do: "Check that the cable is on the right
connector, or choose <found> as the chip." When `found` is a curated part, a **Use <found>** button switches the tab's
chip (no silent switch). An unknown DEV_ID shows the raw ID in the detail only.

### Protected chip

Flash on an RDP level 1 chip fails before writing: `target.protected` (existing code) with the STM32 text "The chip is
read-out protected (RDP level 1). Unlocking erases the whole chip, including its data EEPROM." and the button
**Unlock…**. The confirmation (ADR-0038 style) names what is lost; it is never shown pre-checked. After a successful
unlock the flash can be started again (the page does not continue automatically). RDP level 2:
`target.permanently_protected` (new) — "This chip is permanently locked (RDP level 2) and cannot be reprogrammed."

Maintenance (STM32): **Erase chip**, **Unlock (erase)…** (only when protection is detected or on demand), **Reset**.

## CLI

ADR-0277. Chip ids are the curated part ids (lower case, e.g. `stm32l072cz`, `stm32l0-cat3-64k`).

```
hwio stm32 chips [--json]                                  # list the curated parts
hwio stm32 info   --chip ID [--json]                       # DEV_ID, REV_ID, flash size, RDP level (read only)
hwio stm32 flash  --chip ID FILE [--address ADDR] [--erase-all] [--halt]
hwio stm32 erase  --chip ID [--yes]                        # mass erase (asks, ADR-0038)
hwio stm32 unlock --chip ID [--yes]                        # RDP 1 -> 0, erases (asks)
hwio stm32 reset  --chip ID [--halt]
```

Common probe options (`--serial`, `--speed`) as the `chester` commands. `--address` is required for `.bin` unless the
default flash start is wanted (decision D5). Exit codes and `--json` errors as plan 0006. `hwio stm32 info` on an RDP
level 1 chip succeeds and reports `"rdp": 1`.

## Design

### Profile model (libs/device, ADR-0274)

- `ChipPart` gains an identification union-like typed field instead of the Nordic-only `part`:
  ```cpp
  struct Stm32Id {
      std::uint32_t idcodeAddress;   // DBGMCU_IDCODE: 0x40015800 (L0, G0) or 0xE0042000 (L4, WL)
      std::uint16_t devId;           // bits 11:0, e.g. 0x447
      std::uint32_t flashSizeAddress;// 16-bit size in KiB (L0: 0x1FF8007C); 0 = not checked
      std::uint16_t flashSizeKib;    // expected; 0 = any
  };
  struct ChipPart {
      std::uint32_t part = 0;                         // Nordic FICR INFO.PART (unchanged)
      std::optional<Stm32Id> stm32;                   // set for STM32 parts
      std::string_view displayName;
      std::string_view jlinkDevice;
      std::optional<firmware::DesignatedFirmware> designatedFirmware; // per part (G6), overrides the target's
  };
  ```
- `TargetProfile` gets `ChipVendor vendor` (`Nordic`, `Stm32`). For `Stm32`, `chipFamily` (FICR / CTRL-AP),
  `uicr`, `pibOffset` and `recoverImage` are unused and must be empty / zero (a `static_assert`-style unit test checks
  every registered profile). New STM32 fields: `std::optional<firmware::AddressRange> dataEeprom`,
  `Stm32Protection protection` (flash-interface base, key values, option-byte address, RDP byte position —
  data only, ADR-0032).
- `checkFlashWrite()` for STM32 targets: allowed inside `flash` and, with `allowEeprom`, inside `dataEeprom`; option
  bytes and system memory are never writable through an image (`firmware.out_of_range`).
- New profile `stm32` (`libs/device/src/profiles/stm32.cpp`): display name "STM32 chip", one target per curated part,
  no console, no BLE, no catalog. The parts table lives in `stm32_parts.cpp` and is shared with CHESTER targets
  (plan 0040) so a part is described once.

### J-Link adapter (libs/hw/jlink)

- `IProgrammer::identify()` dispatches on the configured family kind: Nordic (unchanged) or STM32: read
  `idcodeAddress` (32-bit, no halt needed — measured), then the flash size register **after a halt** (Phase 0 P0.3).
  `ChipInfo` gains `devId`, `revId`, `flashSizeKib`, `rdpLevel`.
- Protection check: read the RDP byte of FLASH_OPTR (L0: `0x4002201C` bits 7:0) → level 0 / 1 / 2.
- `unlock(std::stop_token)` (new, STM32 only): the L0 sequence from the part's `Stm32Protection` data (clear the
  FLASH_SR error flags, unlock PECR, unlock option bytes, program the RDP word = `0xAA` with byte 1 kept from
  FLASH_OPTR and its complement, launch the option-byte reload), then wait for the probe to re-connect (the chip
  resets) and re-identify (RDP `0xAA`, no OPTVERR); a non-canonical stored word is rewritten once at level 0
  (ADR-0282); timeout 60 s like `recover`. Own sequence confirmed in P0.4 (D7); the J-Link library's unsecure
  dialog is always answered NO by the existing hook.
- `program` / `verify` / `eraseAll` reuse the existing J-Link flash-loader path (`BeginDownload` / `WriteMem` /
  `EndDownload`, `JLINK_EraseChip`); the data EEPROM is written through the same loader if J-Link supports it for the
  part (P0.5), otherwise images with EEPROM records are rejected (`firmware.out_of_range`, "data EEPROM is not
  supported for this chip").
- `.bin` input: `FirmwareImage::fromBinary(bytes, startAddress)` in libs/firmware (bounded by the target's flash size).

### Session, CLI, GUI, MCP

- `DeviceSession` (ADR-0031) works unchanged per target; the identify step uses the target's vendor. A tab of profile
  `stm32` never attaches RTT (`rttTerminal` empty, ADR-0233 `consoleTargetId` returns the first target).
- CLI: `app/cli/commands/stm32.cpp`, thin over the same backend (CLAUDE.md).
- GUI: `FlashViewModel` gets `Kind::Stm32Firmware` (the single card), a `chip` property, an `unlock()` invokable;
  `AddDeviceViewModel` lists the `stm32` profile with "Chip" as the target label. QML stays thin (ADR-0003).
- MCP (decision D9): `stm32_info`, `stm32_flash`, `stm32_erase` with the same confirmation rules as `erase`;
  `stm32_unlock` only with an explicit `confirm: true` argument and the GUI approval prompt (ADR-0098 addressing).

### Errors (new or extended codes)

| Code | When | What to do |
|---|---|---|
| `target.wrong_chip` (new) | DEV_ID / flash size differ from the chosen part | Check the connector or choose the found chip |
| `target.protected` (extended text) | RDP level 1 | Unlock (erases the chip) |
| `target.permanently_protected` (new) | RDP level 2 | None; the chip cannot be reprogrammed |
| `target.unlock_failed` (new) | the RDP regression did not reach level 0 in 60 s | Power-cycle the board, retry; check the connection |
| `firmware.bin_address_required` (new) | `.bin` without an address where no default applies | Give `--address` |

### Bounds

Curated parts: at most 32 (compile-time array). `.bin` input: at most the part's flash size (≤ 2 MiB) and the
existing 32 MiB file cap. Unlock timeout 60 s, ID read retries 3.

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D1 | How does the user pick the chip in the GUI? | CHESTER card + generic STM32 device / one STM32 flow / CHESTER only | **CHESTER card + generic STM32 device** (2026-11-06) | ADR-0274 |
| D2 | Which chips can be chosen? | curated list / + free J-Link name / whole J-Link list | **Curated list** (2026-11-06) | ADR-0275 |
| D3 | Detected chip vs chosen chip | verify, block on mismatch / detect and preselect / warn only | **Verify, block on mismatch** (2026-11-06) | ADR-0275 |
| D4 | Read-out protection | offer unlock with confirmation / report only | **Offer unlock, never automatic** (2026-11-06) | ADR-0276 |
| D5 | `.bin` files | accept with address (default flash start) / `.hex` only | **Accept `.bin`, default address `0x08000000`** (approved 2026-11-06) | ADR-0280 |
| D6 | CLI naming | `chester lrw` + `stm32` / `chester lora` + `stm32` | **`hwio chester lrw …` + `hwio stm32 … --chip`** (2026-11-06) | ADR-0277 |
| D7 | Unlock implementation | own option-byte sequence / J-Link built-in unsecure | **Own option-byte sequence; Phase 0 P0.4 confirms (a change needs a plan update)** (approved 2026-11-06) | ADR-0280 |
| D8 | Tab without a console | Flash + Maintenance only, console page explains / hide the console page | **Console page with explanation** (approved 2026-11-06) | ADR-0280 |
| D9 | MCP tools for STM32 | yes (unlock needs confirm + GUI approval) / no MCP | **Yes; unlock needs `confirm: true` + GUI approval** (approved 2026-11-06) | ADR-0280 |
| D10 | Which parts are in the first curated list | the two known chips only / **the whole STM32L0 family, generic, prepared firmware later** | **Generic for the STM32L0 family; prepared firmware is added per part when HARDWARIO has it** (2026-11-06: "do it generically for these chips, we'll get the firmware later; they are similar STM32 everywhere") | ADR-0279 |
| D11 | Phase 0: the RDP word read back non-canonical after the unlock | rewrite it at level 0 / verify FLASH_OPTR only | **Rewrite once at level 0** (2026-11-06) | ADR-0282 |
| D12 | Phase 0: STM32L010 value line (same DEV_ID, smaller EEPROM) | named in the hint only / own entries | **Hint only: "not for STM32L010"** (2026-11-06) | ADR-0282 |

## Implementation steps

### Phase 0 — HIL spike (J-Link Commander scripts and a throw-away test of the library; nothing committed except notes)

- [x] P0.1 Confirm DEV_ID / REV_ID / flash size of the CHESTER-S1 chip with the CPU halted, and which J-Link device
      name programs a whole category / size (one representative name per entry, e.g. `STM32L053R8` for cat. 3 64 KiB);
      check the STM32L0 DEV_ID / flash size table against the ST reference manuals.
- [ ] P0.2 Same on a Murata ABZ (STM32L072CZ) with the original Murata firmware: RDP level, DEV_ID `0x447`.
- [x] P0.3 Is a halt needed to read the flash size register reliably? Does the halt / read disturb the running
      firmware (CHESTER app talking to the module over UART)?
- [x] P0.4 RDP 1 → 0: own sequence vs J-Link built-in; how the J-Link library behaves on a secured STM32 in no-GUI
      mode (dialog, automatic mass erase?) — **we must never let J-Link unlock silently**.
- [x] P0.5 Data EEPROM through the J-Link flash loader (STM32L0: `0x08080000`); erase-chip scope (flash only or
      flash + EEPROM).
- [x] P0.6 Back up the measured chip's flash before any write (`mem` dump to the developer machine only; no binary
      in the repo).

#### Phase 0 results (2026-11-06, CHESTER-S1 controller, J-Link 9.84; notes and backup outside the repo)

- **Sources:** ST `cmsis_device_l0` headers and `stm32l0xx_hal_driver` (GitHub, ST; st.com blocked the RM PDFs),
  OpenOCD `stm32lx.c`, the J-Link 9.84 device list (`ExpDevListXML`), hardware.
- **Family data (STM32L0):** DBGMCU_IDCODE `0x40015800`; F_SIZE `0x1FF8007C` (16 bit, KiB); FLASH interface
  `0x40022000`: PECR +0x04 (PELOCK bit 0, PRGLOCK 1, OPTLOCK 2, OBL_LAUNCH 18), PEKEYR +0x0C (`0x89ABCDEF`,
  `0x02030405`), PRGKEYR +0x10, OPTKEYR +0x14 (`0xFBEAD9C8`, `0x24252627`), SR +0x18 (BSY 0, WRPERR 8, OPTVERR 11;
  errors write-1-to-clear), OPTR +0x1C (RDP bits 7:0). Option bytes `0x1FF80000`: each 32-bit word is a 16-bit value
  with its complement in bits 31:16; RDP word 0 (`0xFF5500AA` = level 0). Erased flash and EEPROM read `0x00`.
- **Parts (DEV_ID / flash / data EEPROM / RAM → J-Link name):** cat. 1 `0x457` 8 KiB and 16 KiB / 512 B / 2 KiB →
  `STM32L011K3`, `STM32L011K4`; cat. 2 `0x425` 16 and 32 KiB / 1 KiB / 8 KiB → `STM32L031K4`, `STM32L031K6`; cat. 3
  `0x417` 32 and 64 KiB / 2 KiB / 8 KiB → `STM32L051K6`, `STM32L053R8`; cat. 5 `0x447` 64 KiB (J-Link: EEPROM 3 KiB at
  `0x08080C00`, not confirmed by ST → EEPROM records rejected for this entry), 128 and 192 KiB / 6 KiB / 20 KiB →
  `STM32L071K8`, `STM32L072CB`, `STM32L072CZ`. Within a group J-Link uses the same banks and loader. Only the plain
  names are used: the "(allow opt. bytes)" variants add `0x1FF80000` as a flash bank.
- **P0.1/P0.3:** S1 halted: IDCODE `0x10386417`, F_SIZE `0x0040` (3/3), RDP 0, flash 64 KiB, EEPROM 2 KiB. Running:
  ~1 in 40 reads of IDCODE / F_SIZE return 0 → identify retries a 0 read (3×, bounds) and reads F_SIZE after the halt
  of flash / erase / unlock; `info` does not halt and retries only.
- **P0.4:** with our unsecure-dialog hook (answer NO, ADR existing review R1) J-Link asks on `Connect` ("Active read
  and/or write protected STM32 device detected … Unsecuring will trigger a mass erase") and does nothing. On an RDP 1
  chip `EndDownload` fails after ~10 s (-5), `JLINK_EraseChip` fails at once (-5), nothing is erased; an EraseChip
  in the same connection after the failed download hung > 15 min → the RDP level is checked in identify **before any
  loader call** (as designed). IDCODE and FLASH_OPTR stay readable at level 1, flash / EEPROM do not. The own
  sequence (D7) works (2 runs): the chip mass-erases and is at level 0 after `OBL_LAUNCH`; the stored word was
  `0x015500AA` → canonical rewrite at level 0 (ADR-0282). Level 1 for the test was set with RDP `0xBB`.
- **P0.5:** the J-Link loader programs the data EEPROM with the plain device name (512 B sectors);
  `JLINK_EraseChip` erases flash **and** data EEPROM. The STM32 "erase" is therefore EraseChip only (the nRF
  `eraseCode` writes `0xFF`, which is not the STM32 erased state).
- **P0.6:** flash 64 KiB, EEPROM 2 KiB and option bytes dumped twice (identical), restored after the tests and
  verified against the dump.

### Phase A — backend

- [x] A1 Profile model: `ChipVendor`, `Stm32Id`, per-part designated firmware, STM32 fields, `checkFlashWrite` rules,
      profile invariants test.
- [x] A2 `stm32_parts.cpp` + profile `stm32`; registration; tests.
- [x] A3 `JLinkProgrammer`: STM32 identify, RDP read, `unlock`; fake `IJLinkApi` tests for every path (wrong chip,
      level 0/1/2, unlock timeout, unlock success with re-identify).
- [x] A4 `FirmwareImage::fromBinary`; tests (bounds, address alignment).
- [x] A5 `DeviceSession`: vendor-aware identify, `unlock` operation (target-carrying, ADR-0057), new error codes in
      `hwio::Error` with EN texts.
      Implementation notes: the part table lives in `profiles/stm32_parts.h` (inline constexpr, so `stm32.cpp` and
      CHESTER share the objects); the family data type is `jlink::Stm32Protection` (identification addresses + flash
      interface, ADR-0032 data) on `TargetProfile::stm32`; new `OperationKind::Unlock` / `ProgramPhase::Unlocking`;
      `IProgrammer::programRdpLevel0()` does one option-word write + reload, the device layer reconnects and
      re-identifies (`runUnlock`); `firmware::readFirmwareFile()` reads `.hex` / `.bin`.

### Phase B — CLI and MCP

- [x] B1 `hwio stm32 chips|info|flash|erase|unlock|reset`; end-to-end tests on the fake J-Link.
      Implementation notes: `flash` also takes `--allow-eeprom` (the CLI form of the card's data EEPROM
      consent) and `--yes` for `--erase-all` (ADR-0038); the fake J-Link library has an STM32L0 model
      (`"family": "stm32l0"`) that reproduces the Phase 0 behaviour.
- [ ] B2 MCP tools (if D9 = yes); tests; `plan/0004-mcp.md` tool table.

### Phase C — GUI

- [ ] C1 Add device: STM32 profile, "Chip" picker.
- [ ] C2 Flash page: STM32 card, `.bin` address field, protected / wrong-chip states, Unlock confirmation.
- [ ] C3 Maintenance for STM32; console page text for chips without a console.
- [ ] C4 Qt Quick tests; screenshots with the fake J-Link kit (`../screenshots-demo`).
- [ ] C5 Translations: `update_translations`, Czech (+ DE / SK / PL per plan 0030) entries; docs page
      "Flashing STM32 chips" (5 languages, plan 0029 structure).

### Phase D — HIL

- [ ] D1 Flash, verify, erase, reset the cat. 3 chip from GUI and CLI; restore its original firmware from P0.6.
- [ ] D2 Wrong chip (choose ABZ, connect cat. 3) → `target.wrong_chip` with **Use …** button.
- [ ] D3 Unlock on an RDP level 1 chip (ABZ with Murata firmware, plan 0040) — only on a board the user provides for
      it.

## Tests and verification

- Unit: profile invariants (every STM32 target has no UICR / PIB / CTRL-AP data; every part id unique), identify /
  RDP / unlock on the fake `IJLinkApi`, `checkFlashWrite` STM32 rules, `fromBinary`.
- CLI end-to-end on the fake J-Link; MCP tool tests; Qt Quick tests for the card, the confirmation and the error
  states.
- ASan + UBSan, TSan, clang-tidy, clang-format, qmllint, translations check (CI).
- HIL Phase D on Linux; flash once from Windows and macOS (manual check).

## Risks and open points

- STM32 facts above marked "from memory" must be confirmed in Phase 0 against ST reference manuals before the plan is
  approved; wrong option-byte data could brick a chip (RDP level 2 is permanent). The unlock sequence writes only the
  RDP byte to `0xAA` and never to `0xCC`; a unit test asserts that no code path can write `0xCC`.
- The J-Link library may handle a secured STM32 on its own (dialog or automatic erase); P0.4 must prove it never
  erases without our confirmation.
- The measured cat. 3 chip is the CHESTER-S1 controller (Clime IAQ second PCB). Its firmware source is unknown; until
  HARDWARIO provides it the chip is flashed only with the user's own file through the generic STM32 device. chester-sdk
  logs "Legacy protocol version detected" for old S1 firmware, so field updates will be wanted once a firmware exists.
- Overwriting the S1 firmware without a replacement breaks the Clime IAQ sensors; the HIL steps restore the P0.6
  backup, and the docs warn to back up first (`hwio stm32 info` + a memory dump on an RDP 0 chip).
- rc.4 already contains wizards 0035 / 0008–0010 and 0038 A1; this plan plus 0040 is a large addition for one RC.
