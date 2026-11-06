# 0033 — rc.2 feedback: guided LTE update, one target per tab, steady BLE list, expert actions, product name

- **Status:** In progress (approved 2026-11-04 by Matej Strnad: D1–D9 as recommended; implementation started 2026-11-04)
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
- [x] A1 README, docs (en/cs/de/sk/pl `index.md`), `packaging/RELEASE.md` rule; grep test for "Monitor 2 " outside
  version strings in docs.

Phase B — expert actions (ADR-0236)
- [x] B1 `SettingsViewModel::expertActions` + `AdvancedSection.qml` + settings test.
- [x] B2 `FlashViewModel` / `MaintenanceSection.qml`: visibility, MCU choice when expert, one-shot Recover for the
  `target.protected` hand-off (0009 topic table amended).

Phase C — BLE order (ADR-0235)
- [x] C1 `BleScanner` smoothing + sort; fake clock tests (EMA, eviction, unknown RSSI last as today).
- [x] C2 `BlePickerViewModel` hysteresis, profile first, `freezeOrder`; QML hover/focus; CLI JSON `rssi_last`.

Phase D — one target per GUI tab (ADR-0233)
- [x] D1 `libs/device`: family check at connect, `ErrorCode::TargetOtherConnector`, `identifyTarget()`.
- [x] D2 GUI: target picker removed (page, device bar), tab restore mapping, one-profile device picker (D1),
  `connectorWarning` removed, other-connector banner.

Phase E — LTE update (ADR-0234)
- [x] E1 `SessionHold`, `ApplicationBackup` (+ tests with the fake J-Link library: read, trim, hex, retention,
  protected).
- [x] E2 `LteUpdateProcedure` state machine (all paths: happy, wrong connector, cancel before/after erase, no backup,
  bundle 1/2–2/2, modem, failure in each flash).
- [x] E3 `LteUpdateWizard` (0007) + Flash page LTE kind body; plan 0010 amendment note.
- [x] E4 CLI `chester lte flash --guided` + end-to-end test with the fake library (scripted connector moves).

Phase F — docs and translations
- [x] F1 `docs/<lang>/flashing.md` (LTE update guide, Maintenance + expert setting), `troubleshooting.md`
  (`target.other_connector`), `bluetooth.md` (order), `cli.md` (`--guided`), `privacy.md` (backups folder).
- [x] F2 `update_translations`, Czech; de/sk/pl via the 0030 process.

Phase G — verification
- [x] G1 debug / asan / tsan ctest, tidy 0, format, qmllint, translations, LFS.
- [x] G2 Mock E2E (../screenshots-demo kit: fake J-Link with a scripted connector switch, fake BLE with 30 devices and
  noisy RSSI).
- [ ] G3 (open: waits for the user and a CHESTER-M) HIL on CHESTER-M (asks in chat before every erase / flash): full LTE update with the designated firmware,
  cancel after erase + manual recovery, connect on the LTE connector, supercap board if available.
- [ ] G4 (open: for the user) Manual on Windows / macOS: Settings → Advanced, wizard images, BLE list with many devices.

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

## Implementation notes (2026-11-04, branch `strnad/impl-0033-rc2-feedback-5a0a5d`)

What was built, per phase, and the small choices the plan did not settle (to check against the ADRs):

- **A (ADR-0237).** README "HARDWARIO Monitor 2.0 is a complete rewrite of the earlier HARDWARIO Monitor (version 1)";
  the docs `index.md` already said "describes HARDWARIO Monitor 2.0" (a version, kept). Rule in `packaging/RELEASE.md`
  (release gates) and in plan 0029's tone paragraph. ctest `product_name_check` (`tests/cmake/check_product_name.cmake`)
  scans README, `docs/**` and `packaging/release-notes/*.md` except the published rc.1/rc.2 notes (D6: fixed from rc.3 on).
- **B (ADR-0236).** Key `advanced/expertActions` (Bool, false) registered with the firmware services (shared by the
  Flash and Settings pages). Settings section "advanced" at order 80 (after Console, J-Link and nRF Util, before About:
  "Advanced" last, as in most apps). FlashViewModel: the maintenance MCU is its own property `maintenanceTarget`
  (default the console target), the choice `maintenanceTargets` exists only with the expert setting; turning it off
  goes back to the application MCU. The fold state of Maintenance moved into the view model (`maintenanceExpanded`) so
  that the hand-off can unfold it. Hand-off id `open-flash-page:recover:<target>` (AppViewModel parses it and calls
  `FlashViewModel::revealRecover`); the revealed [Recover…] disappears after a recover of that MCU ran or the device
  of the tab changed. The troubleshooting wizard of 0009 is not implemented yet, so the hand-off is tested by emitting
  it; plan 0009's topic table is amended.
- **C (ADR-0235).** `BleDeviceInfo::rssiSmoothed` (double, 0 = no sample) and `rssiSampled`; the EMA is a free function
  `smoothRssi()` (pure, tested with time points). The picker keeps the shown order and lets a row pass the one above
  it only when stronger by 6 dB (insertion sort over the previous order; profile devices always first, unknown RSSI
  last); `freezeOrder` keeps the order and appends new rows. QML: a HoverHandler on the list and a focused-row counter.
  `BlePickerViewModel::Hooks::devices` is a test seam (like `now`). Fake BLE: `rssiJitter` (deterministic LCG noise),
  scenario `tests/ble_scenarios/noisy.json` (30 devices). CLI: `rssi` is the rounded average, `rssi_last` the last
  value — **open point:** the other JSON keys of `ble scan` are camelCase (`nusAdvertised`, `inUseBy`); `rssi_last`
  follows D5 literally. *Resolved 2026-11-04 by user decision (plan [0034](0034-pre-release-fixes.md) H1): the key is
  `rssiLast`, deviating literally from D5.*
- **D (ADR-0233).** `ErrorCode::TargetOtherConnector` (`target.other_connector`, category Target, wizard LteUpdate);
  `WizardId::LteUpdate` ("lte-update"). The family check runs only in sessions that attach RTT (console sessions: GUI
  tabs, `hwio chester app console`); flash-style CLI sessions keep their `target.wrong_family` behaviour. It reads FICR
  after the connect (or the CTRL-APs after a failed connect); an MCU that cannot be identified, or a protected console
  MCU, connects as before. On another target of the profile the session stops fatally (no auto-reconnect, D8); the
  found target id is in `detail` ("found target: lte"). Texts: "The J-Link is connected to the LTE MCU." / "The console
  runs on the application MCU; the J-Link cable is on the other connector." / "Move the J-Link cable to the APP
  connector, or update the LTE firmware." — the connector by its board label (the target id upper-cased), as in the
  wizard. `DeviceSession::identifyTarget(target)` is an operation (`OperationKind::Identify`, light, ADR-0157) whose
  result carries `ChipFamilyMatch {expectedTargetId, foundTargetId, protectedChip}` in `OperationResult::familyMatch`
  (the plan's `Result<ChipFamilyMatch>` as the asynchronous result of an operation, like every session operation;
  only probe-level errors fail it). GUI: the target picker, `targets`, `setTargetId`, `canChangeTarget` and
  `DeviceTabViewModel::setConsoleTarget` are removed; `configure()` maps any target to `device::consoleTargetId(profile)`
  (first target with an RTT console); a tab saved with "lte" is restored and saved again as "app". One profile: the
  Choose device page shows its name (`profileChoice`, `profileName`). The banner is part of PageHost (warning colours,
  [Update LTE firmware…] when the wizard is registered, [Retry]); the ErrorPanel is hidden for this error.
- **E (ADR-0234).** `SessionHold` (move-only, QPointer to the session, counted holds; `DeviceSession::hold()`,
  `held()`): RTT detached and the console reports Stopped without an error, no auto-reconnect, `reconnect()` ignored,
  operations run and leave the target as it is (only the probe is reopened after nRF Util); the last release
  reconnects. `ApplicationBackup`: range = the target's flash (refused above 4 MiB), trailing erased 4 KiB pages
  trimmed, file `<backups>/<PIB serial or "unknown">-<UTC yyyyMMdd-HHmmss>.hex` via QSaveFile, owner-only, newest 10
  kept (by the stamp in the name); new `firmware::toIntelHex()`. `LteUpdateProcedure` (libs/device): the targets are
  the session's console target and the profile's first other target; connector labels are the ids upper-cased
  ("APP", "LTE"). Additions beyond the plan's step list: an empty application flash needs no copy (step 7 then asks
  for firmware); a failed erase, LTE update or restore can be retried (back to its connector step: [Try again]); the
  LTE source carries `allowUicr`. Step 7 flashes with `halt = false` (programs, verifies, resets). GUI: the wizard is
  an adapter over the procedure with one Embedded component (`qml/flash/LteUpdateStep.qml`) and the step
  illustration for the connector steps; the tab is busy for the whole procedure ("Paused for the LTE firmware update",
  close asks). Framework additions (plan 0007 amended): `Wizard::canCancel()` and the Cancel-confirmation button texts
  ("Cancel anyway" / "Back"). Flash page: the LTE kind shows the text and [Update LTE firmware…]; the Flash bar says
  "Use Update LTE firmware… above: the J-Link has to be moved between the connectors."; LTE flashing from the page is
  gone (the former Flash page LTE tests moved to wizard tests). CLI `--guided`: refused with `--json` or without an
  interactive stdin and stderr (`usage.invalid_argument`); a test-only `HWIO_TEST_INTERACTIVE` (builds with
  HWIO_TEST_OVERRIDES, like HWIO_CATALOG_URL) lets the end-to-end test drive it; it prints the procedure's prompts,
  asks "Continue without a copy of the application? [y/N]" when the copy fails, and when there is no copy at the end it
  stops after the LTE update and says to flash an application with `hwio chester app flash` (exit 0). The fake J-Link
  library got `"connectors"` (a file names the connector the cable is on; each connector has its own chip).
- **Found by the mock E2E (G2) and fixed:** (1) when the J-Link cable moves between a connector check and the next
  operation, the procedure now goes back to that connector step instead of failing: an erase / LTE update / restore
  that finds the other MCU (`target.wrong_family`) waits again, and a failed backup read is followed by one identify
  (another MCU answering → back to the APP step; the application MCU itself → "no copy", BackupFailed). Device test
  `ACableMovedAfterTheCheckWaitsForTheConnectorAgain`. (2) The Done step's [Open console] closes the wizard.
- **Pre-existing, not changed:** Settings opened for the first time with a section (`openSettings("advanced")`) does
  not scroll to it; the second time it does (SettingsPage scrolls before the sections are laid out).

### Verification (G1, G2)

- G2 mock E2E (`../screenshots-demo/e2e-0033.sh`, `qml-e2e-0033/`, screenshots in `out-0033-mock-e2e/`): Choose device
  without a target; the J-Link moved to LTE during a session → banner, no reconnect for 4 s, [Retry] after the move
  back → Connected; Flash page LTE kind; the whole wizard (start on the wrong connector, copy + erase, LTE update, the
  copy written back, console Connected again, "Paused for the LTE firmware update" in the device bar meanwhile);
  Maintenance without / with the expert setting; BLE with 30 noisy devices: after the averages settled 1–2 order
  changes in 10 s (none while frozen). The LTE `.hex` path was used (tst_gui has no nRF Util); bundle and modem run in
  the GUI unit tests and the CLI end-to-end test. CLI `--guided` end to end in `flash_e2e_test`.
- G1 (2026-11-04): ctest linux-debug 1619/1619, linux-asan 1618/1618, linux-tsan 1560/1560 passed (HIL skipped);
  clang-tidy 0 findings; clang-format, all_qmllint, check_translations (0 unfinished cs), check-lfs, check-includes and
  product_name_check clean. G2 also ran `hwio chester lte flash --guided` in a real pseudo-terminal (exit 0, the copy
  written back).


### HIL G3 — H5 (2026-11-05, rc.3, CHESTER-M, J-Link on the LTE connector, nRF Util 8.2.1 / device 2.23.0, HARDWARIO LTE firmware 1.7.0)

- **H5 (cause: the APP firmware pin-resets the LTE MCU; reported since ADR-0273)** `hwio chester lte erase --yes` + `hwio chester lte flash` (bundle: modem via nRF Util, then the
  application via J-Link) fails intermittently: `JLINKARM_EndDownload returned -3/-5` ("No CPU core or target device
  has been selected"). Affects CLI `lte flash`, `--guided` and the GUI LTE wizard after an erase / recover.
  - First suspicion: after nRF Util the probe connects as `NRF9151_XXCA` (the profile's first part), FICR reports
    0x9160 and the probe was connected again as `NRF9160_xxAA` *in place* (`Device =` + `JLINKARM_Connect` on a
    connected library session returns in ~18 ms without initializing the target). Changed (branch
    `strnad/fix-h5-lte`; kept by user decision 2026-11-05): `JLinkProbe::connect()` closes and opens the probe again (same serial number,
    OpenMode, SWD, speed) when the open session already selected another device; unit tests in
    `jlink_probe_test.cpp`, session test `LteFlashOnAnotherPartOpensTheProbeAgainBeforeTheDownload` (the scripted
    library fails EndDownload with -5 when the device changes on an open session).
  - Second change (kept by user decision 2026-11-05): `JLinkLibrary::open()` installs the log / warning / error
    handlers again after every Open; after a close + open the library log was silent (that is why the first failure
    logs showed nothing).
  - **HIL with both changes: still 8 failures in 24 bundle runs** (passed 6/8, 5/8, 5/8; the last round with a local,
    reverted 1.5 s pause before reopening). The connect after the reopen is a full one (InitTarget, AP scan) as `NRF9160_xxAA`;
    the library log now shows the real error, `Failed to erase sectors` 5–7 s into the download. In 5 of the 8
    failures **nRF Util itself** failed 4 s into the modem update (`Failed to read from memory address 0x50003100:
    Failed to write DebugPort register 0x08: Unknown error in the J-Link DLL (-1)`), with our process holding no USB
    handle of the probe. Not failing: erase + `lte flash application.hex` 6/6; erase + nRF Util run directly + a
    separate `lte flash application.hex` 5/5; `lte status` / `lte check` (2.98 V). Failures came in clusters.
  - **Root cause (second round, probe re-plugged, machine idle): an external pin reset of the LTE MCU.** POWER
    RESETREAS (0x50005400) cleared before each run: every failure had RESETPIN set (nRF Util 2/2, our application step
    2/2), no success had it (~20 runs). With the J-Link idle (only a 4-byte read every 10–30 s) after a modem update and
    with the application erased, RESETPIN appeared again about every 2–3.5 minutes. Neither our code (reset strategy
    NORMAL = SYSRESETREQ) nor nRF Util drives the reset pin then; the most likely source is the CHESTER application
    firmware on the APP MCU resetting an LTE MCU that does not answer (not checked on the APP MCU: it was not touched).
    That window (~30 s modem + ~10 s application in a ~150 s cycle) matches the ~30 % failure rate. Not ours either:
    nRF Util alone (its own erase + its modem update, no hwio process) failed 2/10, same error.
  - Measurements (all `--json` nRF Util command lines as the app runs them): bundle after `lte erase` 6/10; the same with
    the profile's first J-Link device set to `NRF9160_xxAA` (local experiment, reverted) 6/10, so the NRF9151 connect
    is not involved; `lte erase` + nRF Util modem 8/10; `nrfutil device erase` + nRF Util modem 8/10; nRF Util-only
    erase + modem + application 8/10 (application 8/8 after a good modem step).
  - The guided update (`lte flash --guided`, GUI wizard; ADR-0234) erases the application code on the APP connector
    before the LTE update, so the application firmware cannot reset the LTE MCU there; the plain `lte flash` (used in
    this HIL) leaves it running.
  - **Fix (user decision 2026-11-05, ADR-0273):** `TargetProfile::resetReason` (CHESTER `lte`: 0x50005400, bit 0);
    the session worker clears it (write 1 to clear, after a FICR check that the probe reaches that MCU; the CPU is
    neither reset nor halted for it) before a flash / modem update / bundle of the LTE MCU (a bundle: before step 1;
    when that was not possible, again before step 2). After a failed step whose error a reset can cause (category
    probe, target, firmware or timeout; not wrong family, other connector, protected, cancelled) it identifies the MCU
    again, reads the register and, with the reset-pin bit set, reports the new `target.reset_during_update` (wizard
    LteUpdate; the original error id, its detail and the register value in the detail). A failed read, another MCU
    answering or a clear bit keep the original error. Plain `hwio chester lte flash` (any input; human output on
    stderr, the log always) warns that `--guided` stops the application first; the GUI has no plain LTE path (ADR-0234).
    Docs: troubleshooting common problem and error entry ×5, `cli.md`; error texts cs/de/sk/pl. Tests:
    `Operations.LteStepFailureAfterAPinResetIsResetDuringUpdate`, `LteDownloadFailureWithoutAPinResetKeepsItsError`,
    `UnreadableResetReasonKeepsTheOriginalError`, `ModemUpdaterMissingIsNotResetDuringUpdate`,
    `AppFlashDoesNotTouchAResetReason` (scripted RESETREAS), `FlashCli.LteBundleWithTheFakeNrfUtil` (the warning). No
    retry.
  - **HIL (2026-11-05, `main` 3b61500 debug build, CHESTER-M, J-Link on the LTE connector, APP MCU not touched, machine
    idle):** `hwio chester lte erase --yes` + plain `hwio chester lte flash` (cached 1.7.0 bundle), 22 runs (10 asked
    for + 12 more for the failure path): **14 passed** (bundle 41.5 s, erase 1.2 s), **8 failed**, all exit 6.
    - Every run logged `Reset reason register 0x50005400 cleared` (never "not cleared"); the warning was printed on
      stderr in every human run (third line, after "Using nRF Util …" and the firmware line; in the log always) and
      the log showed the reopen `Target device changes from NRF9151_XXCA to NRF9160_xxAA - opening the J-Link again`.
    - Successful runs: no `Reset reason after the failure`, no `target.reset_during_update`.
    - **Modem step failed 5×** (runs 11, 13, 15, 20, 22; nRF Util "Failed to write DebugPort register 0x08", 2–31 s
      into the update): all reported `target.reset_during_update` ("The LTE MCU was reset during the update.", exit
      6), detail `firmware.modem_update_failed: …; reset reason 0x00030001 (reset pin)`, log
      `Reset reason after the failure: 0x00030001` (run 11's log was rotated away before it was copied; its human
      output matched).
    - **Application step failed 3×** (runs 9, 10, 17; `Failed to erase sectors`, `JLINKARM_EndDownload returned -5`
      2.6–6 s into the download): **not converted** — `target.program_failed` ("Programming the LTE MCU failed.",
      exit 6), log `Reset reason not read after the failure (another MCU answers)` ~110 ms after the failure. A
      separate `hwio chester lte mem read 0x50005400 -l 4` right after run 17 exited read `01 00 03 00` (RESETPIN
      set). **Bug (fixed below):** `explainFailure()` (`session_worker.cpp`) calls `prepareProbe()`, which keeps the
      broken connection (`m_probeTarget == &target && isConnected()`) instead of connecting again after the pin
      reset; `identify()` then fails (no `FICR` / `CTRL-AP` log line between the failure and the message, no
      J-Link connect lines) and the failed identify is logged as "another MCU answers" (the `!chip` case shares that
      message, its error detail is dropped). The modem path works because the probe was released for nRF Util and
      is connected afresh.
    - `--json` (runs 21 pass, 22 fail): nothing on stderr; the failure envelope had code `target.reset_during_update`,
      category target, wizard `lte-update`, detail with the original id and `reset reason 0x00030001 (reset pin)`.
    - Erase + `hwio chester lte flash application.hex` (from the cached 1.7.0 bundle): passed (10.3 s), register
      cleared, the reopen logged and 71 J-Link library log lines after it (`Device "NRF9160_XXAA" selected`, the
      full connect).
    - End state: erase + bundle passed (run 31); `hwio chester lte status`: CPU running, J-Link device
      `NRF9160_xxAA` — HARDWARIO LTE firmware 1.7.0 (bundle) running.
  - **Fix of the application-step bug (2026-11-05, branch `strnad/hil-h5`):** `explainFailure()` now forces a fresh
    connection before the identify + RESETREAS read: new `JLinkProbe::reopen(why)` (close + open with the same serial
    number and OpenMode, SWD again; `reopenForDevice()` now uses it; on the session worker thread as every J-Link
    call), `m_probeTarget` reset so that `prepareProbe()` connects again (full connect). A failed reopen is logged
    and `prepareProbe()` opens the probe again or fails (logged); every failure keeps the original error. Separate log
    lines: `identify failed: <detail>`, `protected`, `another MCU answers`. The plain `lte flash` warning is now the
    first line of the human output (before "Using nRF Util …" and the firmware line); JSON unchanged. Tests:
    `Operations.LteDownloadFailureOnALostLinkReadsTheResetReasonAfterAReopen` (scripted library: the pin reset during
    EndDownload leaves the session "connected" but every memory / CoreSight access fails until a close; plain flash
    and bundle; fails without the fix), `Probe.ReopenOpensTheSameProbeAgainAndTheNextConnectIsFull`,
    `Probe.ReopenOfAClosedProbeDoesNothing`, `FlashCli.LteBundleWithTheFakeNrfUtil` (warning first). ctest
    linux-debug unit 1669 (one unrelated flaky `ConsoleViewModelTest.LearnsLazilyWhenReadingOnConnectIsOff` under
    -j4, passed 3/3 alone), linux-asan `hwio_device_tests` 468/468 and `hwio_hw_jlink_tests` 149/149; clang-tidy-21
    0 findings on the changed files, clang-format-21 clean, check_translations 0 unfinished (no new user-visible
    strings).
  - **HIL of the fix (2026-11-05, `strnad/hil-h5` debug build, same setup, APP MCU not touched, machine idle):**
    `hwio chester lte erase --yes` + plain `hwio chester lte flash` (cached 1.7.0 bundle), 13 runs until the second
    application-step failure: **8 passed** (41.3–41.6 s), **5 failed**, all exit 6 with `target.reset_during_update`.

    | Run | Result | Failed step | Original error (in the detail) | Into the step | Log after the failure |
    |---|---|---|---|---|---|
    | 1–4, 6, 8, 10, 12 | pass | — | — | — | no reset-reason line, no `target.reset_during_update` |
    | 5 | exit 6 | application | `target.program_failed` (EndDownload -3) | ~8.2 s (programming) | reopen, `Reset reason after the failure: 0x00030001` |
    | 7 | exit 6 | modem | `firmware.modem_update_failed` (DebugPort 0x08) | ~4.5 s | reopen, `0x00030001` |
    | 9 | exit 6 | modem | `firmware.modem_update_failed` (DebugPort 0x08) | ~14.5 s | reopen, `0x00030001` |
    | 11 | exit 6 | modem | `firmware.modem_update_failed` (DebugPort 0x08) | ~24.5 s | reopen, `0x00030001` |
    | 13 | exit 6 | application | `target.program_failed` (EndDownload -5, `Failed to erase sectors`) | ~2.8 s (erasing) | reopen, `0x00030001` |

    - Every failure: "The LTE MCU was reset during the update.", detail ending `reset reason 0x00030001 (reset pin)`,
      the log line `Reading the reset reason after the failure - opening the J-Link again` and the register read
      0.2–0.33 s after the failure; a separate `hwio chester lte mem read 0x50005400 -l 4` right after each failed
      run read `01 00 03 00` as well. Both application-step failures are converted now (before the fix: 0 of 3).
    - Every run logged `Reset reason register 0x50005400 cleared`; the warning was the first stderr line in all 13
      runs.
    - End state: erase + bundle passed (run 14, 41.5 s); `hwio chester lte status`: CPU running, J-Link device
      `NRF9160_xxAA` — HARDWARIO LTE firmware 1.7.0 (bundle) running.
    - Still open: the guided flow (`lte flash --guided`, GUI wizard) with the cable moved by the user.
