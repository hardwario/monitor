# 0033 — rc.2 feedback: guided LTE update, one target per tab, steady BLE list, expert actions, product name

- **Status:** Approved (2026-11-04, by Matej Strnad: D1–D9 as recommended)
- **Milestone:** RC follow-up (v2.0.0-rc.3)
- **Author:** Matej Strnad (with Claude), 2026-11-04
- **Related ADRs:** ADR-0233, ADR-0234, ADR-0235, ADR-0236, ADR-0237 (Accepted 2026-11-04); amended: ADR-0038, ADR-0043,
  ADR-0049, ADR-0055, ADR-0057, ADR-0083, ADR-0129, ADR-0168, ADR-0174. Done before this plan: ADR-0232 (symbol
  archives named `debug-symbols`, a233a53).
- **Depends on:** [0007](0007-m4-wizard-framework-diagnostics.md) (wizard framework, implemented),
  [0006](0006-m3-flash-settings.md) (Flash page), [0012](0012-m5-ble-gui-pairing.md) (BLE pickers). Amends
  [0010](0010-m4-flash-wizard.md) (Approved, not implemented): its LTE path hands over to the wizard of this plan.

## Context

Feedback from the team on v2.0.0-rc.2 (2026-11-03/04):

1. **LTE flashing fails at random.** The application firmware talks to the modem after start; while the LTE MCU is
   being flashed the modem does not answer and the application pulls its RESET pin, which breaks the J-Link flash.
   The documented procedure (docs.hardwario.com → CHESTER → Firmware flashing → LTE modem over J-Link) erases the
   application first (`hardwario chester app erase`), moves the J-Link to the LTE connector, flashes, moves it back
   and flashes the application again. rc.2 leaves all of this to the user.
2. **App / LTE is chosen twice.** On the Choose device page (and the device bar, "Target: lte") and again on the
   Flash page; the orange warning then sends the user back to the start. In practice people use `app`; `lte` only to
   update its firmware.
3. **The BLE list jumps.** Sorted by the last RSSI; with tens of CHESTERs the row under the pointer changes just
   before the click (HIO Manager uses easing and averaging).
4. **Erase everything is a loaded gun** (Karel Blavka): it removes UICR and PIB, and the fast erase on nRF52 takes the
   UICR along. Erase application already exists and is enough for developers.
5. **Name:** README / release notes say "HARDWARIO Monitor 2", the app says "HARDWARIO Monitor".

Facts checked in the code (2026-11-04): Erase application writes 0xFF over the whole code flash through the J-Link
flash loader (`JLinkProgrammer::eraseCode`, every sector that is not empty, not only the first page) and leaves the
UICR alone, so the old nrfjprog first-page bug does not apply. The session already identifies the chip family from
FICR / CTRL-AP in the flash workflows (`target.wrong_family`) and in Diagnose (`identify`).

Not in this plan: the app icon (brand clearance, macOS 26 grey squircle) waits for a design from Matěj; a separate
plan when it arrives.

## Goals / Non-goals

Goals
- An LTE firmware update that works without knowing the reset-pin trap, with the J-Link moves guided and checked.
- A GUI tab works with one MCU (the application MCU); no target choice.
- A BLE list that does not jump under the pointer.
- Erase everything / Recover out of sight unless the user asks for expert actions.
- One product name.

Non-goals
- CLI / MCP targets stay (`hwio chester lte …`, MCP `connect(…, target?)`).
- The generic flash wizard of 0010 (application firmware) — still its own plan.
- LTE console (out of scope since 0002).
- The app icon.

## User experience

### A. Choose device page and device bar (ADR-0233)

- The **Target** segmented choice (app / lte) disappears from the Choose device page; the device bar has no
  "Target: …" box. *D1:* the **Device** picker (CHESTER) is hidden while only one J-Link profile exists; the device
  name is shown as text instead.
- A tab saved with target `lte` (ui/tabs) is restored on `app`.
- J-Link on the LTE connector: Connect finds the LTE MCU family. The tab shows a Warning banner instead of the console:
  > **The J-Link is connected to the LTE MCU.** The console runs on the application MCU. Move the J-Link cable to the
  > APP connector, or update the LTE firmware.
  > [Update LTE firmware…] [Retry]

  The tab does not auto-reconnect in this state (the user acts).

### B. Flash page (ADR-0234, ADR-0236)

- Kind cards stay: **Application firmware** · **LTE firmware**. The orange connector warning is removed.
- With **LTE firmware** chosen, the page shows a short text and one button instead of Firmware / Options:
  > Updating the LTE MCU needs the J-Link moved between the APP and LTE connectors. The guide stops the application,
  > updates the LTE MCU and puts the application back.
  > [Update LTE firmware…]

  The bottom bar's Flash button is disabled for this kind (its tooltip points to the button above).
- **Maintenance — application MCU** (folded by default, ADR-0049):
  - Expert actions off (default): [Reset] [Reset and halt] [Erase application].
  - Expert actions on: also [Erase everything…] [Recover…] (ADR-0038 confirmation) and a segmented choice
    **Application MCU | LTE MCU** above the buttons; LTE MCU maintenance runs as today's cross-target operation
    (ADR-0057) and needs the J-Link on the LTE connector (`target.wrong_family` otherwise).

### C. LTE firmware update wizard (ADR-0234)

A modal wizard of the 0007 framework, `WizardId::LteUpdate` (*D2*: new id). Steps:

1. **Firmware** — the designated HARDWARIO LTE firmware (default, ADR-0165) or own file (.hex / bundle .zip / modem
   .zip), the same source part as the Flash page today (FirmwareSourceViewModel, LTE kind). Prerequisites inline (nRF
   Util for bundle / modem, ADR-0016). [Next]
2. **Connect to the APP connector** — picture of the APP connector (existing slot `chester-app-cable`). The step polls
   the probe every 1 s (bounded: until the user cancels) with an identify of the `app` target; it advances by itself
   when the application MCU answers. Text while waiting: "Connect the J-Link cable to the APP connector."; when the
   LTE MCU answers: "This is the LTE connector. Move the cable to the APP connector."
3. **Stop the application** — *D3:* reads the application code flash and keeps it as a backup, then **Erase
   application** (code only; UICR and PIB stay). Progress: "Saving a copy of the application…", "Erasing the
   application…". If the MCU is read-back protected (`target.protected`) or the read fails, the step says so and
   offers [Continue without a copy] (step 7 then asks for firmware) or [Cancel].
4. **Connect to the LTE connector** — slot `chester-lte-cable`, polling as in 2 with target `lte`.
5. **Update the LTE MCU** — PhaseStepper with `plannedPhases` (1/2 and 2/2 for a bundle), as on the Flash page today.
6. **Connect to the APP connector** — as step 2.
7. **Put the application back** — default: the copy from step 3 ("Write back the application that was in the
   device"); alternatives: firmware from the catalog / file (the Application source part). Then flash + reset (run).
8. **Done** — summary (LTE firmware version, application restored), [Close]; the tab's console reconnects.

Cancel: allowed in steps 1–4 and 6–7 (not during a flash, as today). After step 3 the wizard warns on Cancel:
"The application MCU is erased. The device will not run until you flash an application." with [Cancel anyway] /
[Back]; the backup file is kept and mentioned with its path.

While the wizard runs, the tab's session is **held** (*D4*): no auto-reconnect, the console shows "Paused for the LTE
firmware update"; afterwards it reconnects on `app`.

### D. BLE pickers (ADR-0235)

- Order: profile devices first (only matters with "Show all devices"), then smoothed RSSI, strongest first; a row
  overtakes another only when stronger by ≥ 6 dB (*D5*).
- While the pointer is over the list or a row has keyboard focus, the order is frozen; new devices are appended at
  the end; the order updates when the pointer leaves.
- The RSSI column shows the smoothed value (whole dBm).

### E. Settings → Advanced (ADR-0236)

New section **Advanced** (after Console, before About):
- Switch **Show expert maintenance actions** — "Erase everything, Recover and the LTE MCU maintenance on the Flash
  page. They can erase the serial number and the configuration of the device." Default off. Key
  `advanced/expertActions`.
- The troubleshooting hand-off for `target.protected` opens the Flash page with Maintenance unfolded and [Recover…]
  shown for that MCU this once (the setting is not changed).

### F. Product name (ADR-0237)

README ("HARDWARIO Monitor 2 is a complete rewrite…" → "HARDWARIO Monitor 2.0 is …" style), docs in five languages,
`packaging/RELEASE.md` rule for release notes. Version strings like "HARDWARIO Monitor 2.1.0 is available" stay.
*D6:* edit the published rc.2 release text on GitHub (asks before publishing).

## CLI

- *D7:* `hwio chester lte flash … --guided` runs the same procedure interactively (TTY only; refused with
  `--json`/non-TTY: `usage.invalid_argument`): prompts "Connect the J-Link to the APP connector…" and continues by itself when the
  family matches (same polling), backup → erase application → LTE → APP → write back → reset. Without `--guided`
  nothing changes (py-hardwario compatible). Backup path printed at the end.
- `hwio ble scan` sorts by the smoothed RSSI (one scan: the average over the scan); the human and JSON output show
  the smoothed value; JSON gains `rssi_last` (*D5*).
- No new CLI for the expert setting (GUI only).

## Design

### Modules

| Area | Change |
|---|---|
| `libs/device` | `DeviceSession::identifyTarget(targetId)` → `Result<ChipFamilyMatch>` (light op, reuses the Diagnose `identify` code; no RTT pause on the console target, ADR-0157 pattern). Session connect checks the family of the console target (one FICR/CTRL-AP read) and fails with a new `ErrorCode::TargetOtherConnector` carrying the found target id. `DeviceSession::hold()` → RAII `SessionHold` (moved-only): pauses RTT and auto-reconnect until destroyed (*D4*). |
| `libs/device` | `LteUpdateProcedure` — the step sequence of C as a headless state machine (GUI wizard and CLI `--guided` both drive it; no logic in QML or CLI). Inputs: session, profile, LTE source, restore choice; outputs: step, prompt id, progress, result. Polling interval 1 s, one identify in flight. |
| `libs/device` | `ApplicationBackup` — reads the code flash of `app` (profile range; CHESTER 1 MiB; bound = profile flash size, refused above 4 MiB) via `readMemory`, trims trailing 0xFF pages, writes Intel HEX to `<AppData>/backups/<serial>-<UTC>.hex` (0600 / user ACL), keeps the newest 10 files (*D3*). |
| `libs/hw/ble` | `BleScanner`: per device `rssiSmoothed` (EMA, τ = 3 s, time-based: `s += (r − s)·(1 − e^(−Δt/τ))`, first sample = value), sort by it; `BleDeviceInfo` gains `rssiSmoothed`, `rssi` stays the last value. |
| `app/gui` viewmodels | `ProbePickerViewModel` / `ConnectionViewModel`: target picker removed, `targetId` fixed to the console target; tab restore maps other targets. `BlePickerViewModel`: hysteresis order (stable insertion with margin), profile first, `freezeOrder` property set by QML hover/focus. `FlashViewModel`: `connectorWarning` removed; `lteUpdateRequested()`; `expertActions`; maintenance MCU choice only when expert. `SettingsViewModel`: `expertActions`. New `LteUpdateWizard` (0007 framework) as a thin adapter on `LteUpdateProcedure`. |
| `app/gui` QML | ChooseDevicePage (no target), DeviceBar (no targetBox), KindCards (no warning), FlashPage (LTE kind body), MaintenanceSection (expert), new `settings/AdvancedSection.qml`, BLE lists (hover/focus → freezeOrder), banner for `target.other_connector`. |
| `app/cli` | `--guided` for `chester lte flash` on `LteUpdateProcedure`. |
| `libs/core` | `ErrorCode::TargetOtherConnector` (`target.other_connector`), `WizardId::LteUpdate`; error texts EN (CS via .ts). |

### Errors

| Code | Message | Cause | Suggestion / action |
|---|---|---|---|
| `target.other_connector` (new) | "The J-Link is connected to the %1 MCU." | "The console runs on the %2 MCU; the J-Link cable is on the other connector." | "Move the J-Link cable to the %2 connector, or update the LTE firmware." · wizard `LteUpdate` |
| `target.protected` (existing) | unchanged | | hand-off shows Recover once (E) |
| backup failure | reuses `target.memory_access_failed` / `io.write_failed` | | wizard offers [Continue without a copy] |

### Threading and limits

All J-Link calls stay on the session worker (ADR-0057). `LteUpdateProcedure` lives on the GUI / CLI main thread and
issues session operations; polling = one identify in flight, 1 s apart. Backup ≤ profile flash size (≤ 4 MiB),
10 files retained. BLE: smoothing state per device inside the existing `maxDevices` bound.

## Decisions requiring user confirmation

Confirmed on 2026-11-04 (chat, rc.2 feedback): the four direction decisions → ADR-0233 (no target choice; family
recognition), ADR-0234 (guided LTE update with erase, not halt), ADR-0235 (smoothed RSSI + hysteresis), ADR-0236
(expert actions only via Settings → Advanced), ADR-0237 (name).

| # | Question | Options | Recommendation | Decision (by user) | ADR |
|---|---|---|---|---|---|
| D1 | Device picker with one profile | hide (show name) · keep | hide | accepted (recommendation), 2026-11-04 | 0233 |
| D2 | LTE update UI | modal wizard (0007 framework, `WizardId::LteUpdate`) · steps inline on the Flash page | wizard | accepted (recommendation), 2026-11-04 | 0234 |
| D3 | Restoring the application | backup before erase, written back by default (file kept, 10 newest) · ask for catalog/file at the end · both | backup + alternatives | accepted (recommendation), 2026-11-04 | 0234 |
| D4 | Tab session during the wizard | `SessionHold` (RTT + auto-reconnect paused) · close the session | hold | accepted (recommendation), 2026-11-04 | 0234 |
| D5 | BLE parameters | τ = 3 s, margin 6 dB, freeze on hover/focus, JSON `rssi_last` · other values | as listed | accepted (recommendation), 2026-11-04 | 0235 |
| D6 | Fix the published rc.2 release text on GitHub | yes (asks before publishing) · leave, fix from rc.3 on | from rc.3 on | accepted (recommendation), 2026-11-04 | 0237 |
| D7 | CLI | `chester lte flash --guided` · no CLI form (GUI only) | `--guided` | accepted (recommendation), 2026-11-04 | 0234 |
| D8 | Connect on the LTE connector | banner + [Update LTE firmware…] + [Retry], no auto-reconnect · plain error | banner | accepted (recommendation), 2026-11-04 | 0233 |
| D9 | Expert setting scope | Erase everything + Recover + LTE MCU maintenance · only the two erases | all three | accepted (recommendation), 2026-11-04 | 0236 |

## Implementation steps

Phase A — name and docs rule (ADR-0237)
- [ ] A1 README, docs (en/cs/de/sk/pl `index.md`), `packaging/RELEASE.md` rule; grep test for "Monitor 2 " outside
  version strings in docs.

Phase B — expert actions (ADR-0236)
- [ ] B1 `SettingsViewModel::expertActions` + `AdvancedSection.qml` + settings test.
- [ ] B2 `FlashViewModel` / `MaintenanceSection.qml`: visibility, MCU choice when expert, one-shot Recover for the
  `target.protected` hand-off (0009 topic table amended).

Phase C — BLE order (ADR-0235)
- [ ] C1 `BleScanner` smoothing + sort; fake clock tests (EMA, eviction, unknown RSSI last as today).
- [ ] C2 `BlePickerViewModel` hysteresis, profile first, `freezeOrder`; QML hover/focus; CLI JSON `rssi_last`.

Phase D — one target per GUI tab (ADR-0233)
- [ ] D1 `libs/device`: family check at connect, `ErrorCode::TargetOtherConnector`, `identifyTarget()`.
- [ ] D2 GUI: target picker removed (page, device bar), tab restore mapping, one-profile device picker (D1),
  `connectorWarning` removed, other-connector banner.

Phase E — LTE update (ADR-0234)
- [ ] E1 `SessionHold`, `ApplicationBackup` (+ tests with the fake J-Link library: read, trim, hex, retention,
  protected).
- [ ] E2 `LteUpdateProcedure` state machine (all paths: happy, wrong connector, cancel before/after erase, no backup,
  bundle 1/2–2/2, modem, failure in each flash).
- [ ] E3 `LteUpdateWizard` (0007) + Flash page LTE kind body; plan 0010 amendment note.
- [ ] E4 CLI `chester lte flash --guided` + end-to-end test with the fake library (scripted connector moves).

Phase F — docs and translations
- [ ] F1 `docs/<lang>/flashing.md` (LTE update guide, Maintenance + expert setting), `troubleshooting.md`
  (`target.other_connector`), `bluetooth.md` (order), `cli.md` (`--guided`), `privacy.md` (backups folder).
- [ ] F2 `update_translations`, Czech; de/sk/pl via the 0030 process.

Phase G — verification
- [ ] G1 debug / asan / tsan ctest, tidy 0, format, qmllint, translations, LFS.
- [ ] G2 Mock E2E (../screenshots-demo kit: fake J-Link with a scripted connector switch, fake BLE with 30 devices and
  noisy RSSI).
- [ ] G3 HIL on CHESTER-M (asks in chat before every erase / flash): full LTE update with the designated firmware,
  cancel after erase + manual recovery, connect on the LTE connector, supercap board if available.
- [ ] G4 Manual on Windows / macOS: Settings → Advanced, wizard images, BLE list with many devices.

## Tests and verification

Unit (GMock'd / fake vendor libraries): family check at connect; `identifyTarget`; `SessionHold` (no reconnect while
held, reconnect after); `ApplicationBackup` (size bound, trim, HEX round trip, retention 10, protected MCU);
`LteUpdateProcedure` all paths above; `BleScanner` EMA with a fake clock; picker hysteresis and freeze; settings
persistence; FlashViewModel expert visibility and the one-shot Recover. Qt Quick tests: Choose device page without
target, device bar, Advanced section, LTE kind body, BLE list freeze on hover. CLI E2E: `--guided` with scripted
connector moves; refusal with `--json`. Sanitizers and tidy as always. HIL and manual checks in G3/G4.

## Risks and open points

- **Backup of a protected application MCU:** if CHESTER application firmware enables APPROTECT, the read fails and
  the wizard falls back to "choose firmware" (catalog / file). To check on HIL (G3).
- **Family check at connect** adds one FICR read per connect; must not break auto-reconnect timing or BLE.
- **Boards with other connector layouts** (CHESTER variants not checked in ADR-0057 Phase 0): the procedure relies on
  the family seen through each connector.
- **Supercapacitor boards:** no power cycle is needed (the application is flashed and reset over J-Link); confirm on
  HIL.
- The app icon (separate plan, design by Matěj).
