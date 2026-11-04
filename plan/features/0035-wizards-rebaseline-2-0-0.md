# 0035 — Wizards re-baseline for 2.0.0: probe picker step, amendments to 0008 / 0009 / 0010

- **Status:** Approved (2026-11-04 by Matej Strnad)
- **Milestone:** 2.0.0 (M4 wizards, re-planned; ADR-0245)
- **Author:** Matej Strnad (with Claude), 2026-11-04
- **Related ADRs:** ADR-0245, ADR-0248 (accepted 2026-11-04); ADR-0249, ADR-0250, ADR-0251, ADR-0252, ADR-0253,
  ADR-0254 (accepted 2026-11-04); ADR-0065, ADR-0066, ADR-0067, ADR-0071, ADR-0072, ADR-0076, ADR-0077, ADR-0181, ADR-0184,
  ADR-0185, ADR-0227, ADR-0233, ADR-0234, ADR-0236
- **Depends on:** [0007](0007-m4-wizard-framework-diagnostics.md) phases A–C (implemented: `Wizard`, `WizardRegistry`,
  `WizardLauncher`, `DiagWorker`, `CheckListModel`, `WizardDialog.qml`), [0033](0033-rc2-feedback.md) (one target per
  tab, `lte-update` wizard, expert actions — merged on `main` d4d6eea), [0012](0012-m5-ble-gui-pairing.md) (BLE GUI),
  [0024](0024-hwio-prefix-rename.md) (`hwio` rename)
- **Amends:** [0008](0008-m4-first-run-setup.md), [0009](0009-m4-connect-and-troubleshooting.md),
  [0010](0010-m4-flash-wizard.md) — re-approved with these amendments on 2026-11-04
- **Works with:** [0036](0036-jlink-firmware-update-gui.md) (J-Link firmware update hooks in 0008/0009),
  [0037](0037-docs-links.md) ("Learn more" and docs links in wizard steps)

## Context

The user decided on 2026-11-04 that all three M4 wizards ship in 2.0.0 (ADR-0245): 0008 first-run setup, 0009 connect
+ troubleshooting, 0010 guided flash (application firmware only). Their texts were approved on 2026-10-16 and no
longer match the code:

- **ADR-0233** — a tab has no target choice; it always opens the profile's console target (CHESTER `app`). A J-Link
  on the LTE connector gives `target.other_connector` with a banner in `PageHost.qml` ([Update LTE firmware…] [Retry]).
  0009's Device step (profile + target cards) and 0010's kind step are obsolete.
- **ADR-0234** — LTE firmware goes only through `LteUpdateWizard` (`app/gui/src/wizard/lte_update_wizard.*`,
  `WizardId::LteUpdate`, "lte-update").
- **ADR-0236** — Erase everything / Recover / LTE maintenance are behind Settings → Advanced
  (`advanced/expertActions`); the `protected` topic already hands off `open-flash-page:recover:<target>` (0033).
- **0012** — the Choose device page has a J-Link / Bluetooth transport bar; BLE errors name the BlePairing wizard
  (plan 0032, Draft).
- **0024** — `hio` → `hwio` (CLI `hwio doctor`, namespace `hwio::gui`, `hwio::diag`).
- The probe list exists only inside `app/gui/qml/pages/ChooseDevicePage.qml` (the "Probe" card, about lines 232–420,
  bound to the tab's `ProbePickerViewModel`); all three wizards need it as the Embedded `probe-picker` step.
- The error table (`libs/core/src/error.cpp`) has codes the 2026-10 plans do not map: `probe.io_failed`,
  `target.rtt_write_failed`, `probe.firmware_update_failed` (Troubleshooting), `target.not_connected`
  (ConnectDevice), `usb.world_accessible`, `ble.permission_denied` (FirstRunSetup), `target.other_connector`
  (LteUpdate).
- The user docs (`docs/<lang>/troubleshooting.md`, plan 0029) say "Some error texts mention a setup wizard; it is not
  available yet" — wrong once 0008 ships.

This plan does two things: (A) it extracts the probe picker into a reusable Embedded step, and (B) it records the
amendments of 0008 / 0009 / 0010 in their texts so they can be re-approved and implemented in the order
picker → 0008 ∥ 0010 → 0009 (ADR-0252, accepted).

## Goals / Non-goals

**Goals**
1. `probe-picker` Embedded step usable by 0008 (read-only), 0009 and 0010 (selection), without changing the Choose
   device page's behaviour.
2. Amendments of 0008 / 0009 / 0010 against the current code (sections below), written into the three plans as
   "Amendment by 0035" notes.
3. Start-up check + `setup/*` keys + settings schema bump (0008, ADR-0071 kept).
4. Troubleshooting topics re-derived from `docs/en/troubleshooting.md` with HARDWARIO review (ADR-0251, accepted).
5. Coverage: every error code that names FirstRunSetup / ConnectDevice / Troubleshooting / FlashFirmware has a start
   step or topic; BlePairing per ADR-0250 (accepted).
6. Translations cs + de + sk + pl complete (ADR-0248); docs pages in five languages updated where they mention the
   wizards.

**Non-goals**
- BlePairing wizard and the setup Bluetooth step (plan 0032; ADR-0250 recommends after 2.0.0).
- The J-Link firmware update itself (plan 0036); this plan only reserves the hook rows.
- Docs URL table (plan 0037); wizard steps use whatever `docsUrl()` offers when 0037 lands.
- New flash or diagnose capabilities; real CHESTER art and connector names (open question 22).

## User experience

### A. Probe picker step (shared by 0008 / 0009 / 0010)

- The Probe card of the Choose device page (title "Probe", Refresh, "Looking for J-Link probes…", the rows with serial,
  product, firmware date and the "In use" tags) becomes a component used in two places. The Choose device page looks
  and behaves exactly as today (tab switch on an in-use row, Connect below).
- In a wizard step (ADR-0252, accepted):
  - rows used by another tab of this window are disabled with "In use (tab %1)" — a modal wizard does not switch tabs;
  - rows used by another program are disabled as today ("In use by another program (%1)");
  - exactly one free probe is preselected; the step's [Next] is enabled when a free probe is selected (0009, 0010);
  - 0008's test step is read-only (no selection highlight, every row informational);
  - no Connect button; the J-Link Software error panel is replaced by the wizard's own check rows.
- Empty list: "No J-Link found. Connect the J-Link to a USB port of this computer." plus the step's checks
  (`probes`, `usb-access` on Linux, `probe-holders`).

### B. Amendments (summary; the full text is in each plan)

| Plan | Amendment by 0035 |
|---|---|
| 0008 setup | Welcome gets a language combo (ADR-0254, accepted); step 3 also handles `usb.world_accessible` and shows the ADR-0185 `uaccess`-only rule; step 5 uses the `probe-picker` step read-only; the `probe-firmware` row gets [Update J-Link firmware…] when 0036 is registered (ADR-0255, accepted); `hio` → `hwio`; Bluetooth step stays in 0032 (`ble.permission_denied` has no wizard until then, ADR-0250); schema bump 2 → 3; translations cs + de + sk + pl. |
| 0009 connect | No Device step, J-Link only, app only (ADR-0249, accepted): Probe · Cable · Power · Check · Done. `target.wrong_family` / `target.other_connector` on Check → "Move the cable to the APP connector" ([Show me how] → Cable) or [Update LTE firmware…] (`lte-update`). `target.not_connected` starts at Check. Choose device empty state: disabled "Help me fix this" → [Guide me]. |
| 0009 troubleshooting | Topics re-derived from `docs/en/troubleshooting.md` "Common problems" (ADR-0251; table below). New codes mapped; `modem-failed` and the new `other-connector` topic hand off to `lte-update`; `probe-firmware` gets [Update J-Link firmware…] (0036); a manual `bluetooth` topic in the topic list (no wizard codes, ADR-0250). |
| 0010 flash | Application firmware only (ADR-0245), no kind step (ADR-0253, accepted): Probe · Cable · Firmware · Confirm · Flash · Done. LTE: link [Update LTE firmware…] → `lte-update`. `firmware.out_of_range` with an LTE image → hand-off to `lte-update`. W1 (`FirmwareSourceViewModel` extraction) is already done (0021). [Open console] is always offered (app is the console target). |

### C. Troubleshooting topics (re-derived; replaces 0009's topic table)

Source: `docs/en/troubleshooting.md` § Common problems and the per-code entries. "A" = automatic check, "M" = manual
instruction, "H" = hand-off. Texts are condensed from the docs and reviewed by HARDWARIO (ADR-0251).

| Topic id | Docs section (anchor in 0037) | Error codes | Steps |
|---|---|---|---|
| `no-probe` | No J-Link found | `probe.not_found`, `probe.io_failed` | A `probes` · A `usb-access` (Linux) · A `probe-holders` · M unplug/replug, other port and cable, LED on · A `probes` → H [Connect] (connect-device) |
| `probe-busy` | The J-Link is used by another program | `probe.open_failed`, `probe.in_use_by_other_process`, `probe.held_by_program` | A `usb-access` (Linux; Failed → H setup wizard step 3) · A `probe-holders` (names + PIDs) · M close the other Monitor window / `hwio` / SEGGER tools · M replug · A Diagnose `probe` |
| `no-power` | The device has no power or does not respond | `target.no_power` | M power hint + slot `chester-power` · A Diagnose `probe`, `power` · M flat cable fully inserted (guide of the console target) · A `power` |
| `no-connect` | (same section) | `target.connect_failed`, `target.wrong_family` | A Diagnose `probe`..`identify` · M cable guide for the console target; wrong family of the LTE MCU → "The J-Link is on the LTE connector. Move it to the APP connector." · M [Use 1000 kHz] · A Diagnose |
| `other-connector` | The J-Link is on the LTE connector | (`target.other_connector` names LteUpdate; the topic is listed for the no-error entry only) | M "Move the cable to the APP connector and press Retry." · H [Update LTE firmware…] (`lte-update`) |
| `no-console` | No console (RTT) found | `target.rtt_not_found`, `target.rtt_buffer_not_found` | A Diagnose (all) · M [Reset] · A `rtt` · M "Flash firmware with the CHESTER console" H [Flash firmware…] (0010) · M "Use Automatic speed" |
| `console-stuck` | (same section) | `target.rtt_write_timeout`, `target.rtt_read_failed`, `target.rtt_write_failed` | A Diagnose `probe`, `power`, `connect` · M [Reset] · A `rtt` · M cables; auto-reconnect |
| `probe-firmware` | — (code entries) | `probe.firmware_outdated`, `probe.firmware_update_failed` | A `probe-firmware` · M [Update J-Link firmware…] (0036, ADR-0255; tab disconnected first, ADR-0246) with `hwio jlink update-firmware` as CommandBlock alternative · A `probe-firmware` |
| `protected` | Flashing fails or the MCU is protected | `target.protected` | M recover erases everything incl. PIB · H `open-flash-page:recover:app` (0033) |
| `program-failed` | (same section) | `target.program_failed`, `target.erase_failed`, `firmware.verify_failed` | A Diagnose `probe`..`identify` · M [Use 1000 kHz] · M stable power · H [Try again] (0010 if registered, else Flash page) |
| `recover-failed` | (same section) | `target.recover_failed` | M power off 10 s · A Diagnose `probe`, `power`, `connect` · H Flash page (Recover shown once, ADR-0236) |
| `modem-failed` | LTE modem firmware needs nRF Util | `firmware.modem_update_failed` | A `nrfutil` (Failed → H setup step 4) · M cable/power, 1–3 min · H [Update LTE firmware…] (`lte-update`) |
| `bluetooth` | Bluetooth device not found or pairing fails | — (BLE codes name BlePairing, ADR-0250) | A `bluetooth` · M turn on, closer, disconnect from phones · M remove pairing in system settings, pairing agent on Linux · link "Learn more" (0037) |

The topic list ("What is the problem?") shows, in this order: No J-Link found · The J-Link is used by another program ·
The device has no power or does not respond · The J-Link is on the LTE connector · The console stays empty ·
Flashing fails · J-Link firmware · LTE modem firmware · Bluetooth. "J-Link Software not found or too old" and "USB
permissions on Linux" are not topics: they hand off to the setup wizard (steps 2 and 3).

### D. Error-code coverage after this plan

| Wizard | Codes and start step |
|---|---|
| FirstRunSetup (0008) | `dependency.jlink_not_found`, `_load_failed`, `_too_old` → J-Link step (`dependency.jlink_function_missing` names no wizard and stays so); `usb.access_denied`, `usb.world_accessible` → USB step; `dependency.nrfutil_*` → nRF Util step. `ble.permission_denied` → **no wizard** until 0032 (ADR-0250; `error.cpp` changed, 0032 restores it). |
| ConnectDevice (0009) | `probe.not_found` → Probe; `target.no_power` → Power; `target.connect_failed`, `target.wrong_family` → Cable; `target.not_connected` → Check. |
| Troubleshooting (0009) | the topic table above. |
| FlashFirmware (0010) | `firmware.hex_parse_error`, `firmware.invalid_image`, `firmware.not_found_in_catalog` → Firmware; `firmware.out_of_range` → Firmware with the LTE hand-off when the image fits the LTE MCU. |
| LteUpdate (0033) | `target.other_connector` (unchanged). |
| BlePairing (0032) | excluded from the coverage test while not registered (ADR-0250). |

## CLI

None. The CLI equivalents stay `hwio doctor`, `hwio chester app check`, `hwio chester app flash`; their "What to do"
lines are unchanged.

## Design

### A. Probe picker extraction

```
app/gui/qml/components/ProbeList.qml        NEW: the Probe card (header + Refresh + scanning row + rows), properties
                                            `picker: ProbePickerViewModel`, `wizardMode: bool`, `readOnly: bool`
app/gui/qml/pages/ChooseDevicePage.qml      uses ProbeList { picker: page.picker; wizardMode: false }
app/gui/qml/wizard/ProbePickerStep.qml      NEW: Embedded step: ProbeList { wizardMode: true } + the step's CheckList
app/gui/src/wizard/probe_picker_step.*      NEW: registerProbePickerStep(AppViewModel&) → WizardRegistry::addEmbedded(
                                            "probe-picker", qrc:/qt/qml/HardwarioMonitor/qml/wizard/ProbePickerStep.qml)
app/gui/src/viewmodels/probe_picker_view_model.*   + wizard mode, + selectedSerial()
app/gui/src/viewmodels/app_view_model.*    + makeProbePicker(QObject* parent) (shares ProbeScanner and tabUsingProbe)
```

`ProbePickerViewModel` additions:

```cpp
// Wizard mode (ADR-0252): rows used by another tab are not selectable and choose() never emits
// tabActivationRequested; connectSelected() does nothing.
void setWizardMode(bool wizardMode);
[[nodiscard]] std::optional<std::uint32_t> selectedSerial() const;   // nullopt when none selected
```

- Ownership: a wizard creates its picker with `app.makeProbePicker(this)` (Qt parent = the wizard, deleted with it)
  and returns it from `Wizard::embeddedViewModel()` while the step is current. `setActive(true)` while the step is
  shown, `false` otherwise (the scanner is acquired only while visible, ADR-0093). The destructor already releases the
  scanner.
- Threading unchanged: `ProbeScanner` result → GUI thread → each active picker (`updateFromScanner()`).
- Bounds: one picker per open wizard (ADR-0066: one wizard at a time) + one per tab.

### B. Settings keys and schema (0008)

| Key | Type | Default | Owner |
|---|---|---|---|
| `setup/completedVersion` | string | absent | `SetupWizard`, `StartupCheck` |
| `setup/showAtStart` | bool | `true` | `SetupWizard`, `StartupCheck` |

`currentSettingsSchema` 2 → 3 in `libs/core/include/hwio/core/settings.h`, migration 3 = no-op that writes nothing
(existing users have no `setup/completedVersion`, so the wizard opens once after the upgrade — **intended**: they get
the 2.0.0 setup check once; see decision row "Existing users"). If another plan bumps the schema first, this plan
takes the next number (rule in `settings.cpp`: exactly one migration per step).

### C. Start-up check (0008, ADR-0071 kept)

```
app/gui/src/startup_check.*   StartupCheck : QObject (child of AppViewModel); run() after the main window is shown
```

- Submits the `jlink-software` check to `DiagWorker` (≤ 5 s); the result arrives on the GUI thread; decision table of
  0008 (absent key → Welcome; failed check + showAtStart → "J-Link Software is needed" variant; else nothing).
- Does nothing while another wizard is open (WizardViewModel busy) — then it tries once more when that wizard closes.
- Settings read-only (`settings.newer_schema`): the wizard is not opened automatically (it could not record
  completion and would open on every start).

### D. Coverage test

`app/gui/tests/wizard/wizard_coverage_test.cpp` (GTest): for every `ErrorCode` with a `wizard` in `error.cpp`:
FirstRunSetup → `SetupWizard::startStepFor(code)` is not empty; ConnectDevice → `ConnectWizard::startStepFor`;
Troubleshooting → `topicFor(code) != nullptr`; FlashFirmware → `FlashWizard::startStepFor`; LteUpdate → registered;
BlePairing → skipped while ADR-0250 holds (the test names the ADR in its skip message). The test lands with the first
wizard and grows with each (0008 → 0010 → 0009).

### E. Docs (plan 0029 pages, five languages)

- `troubleshooting.md` § USB permissions on Linux and § `usb.access_denied`: remove "the setup wizard … is not
  available yet"; point to Help › Setup wizard….
- `getting-started.md` § Help: the four wizard items of the Help menu; Choose device "Guide me".
- `flashing.md`: "Flash firmware (guided)…".
- Every change in `en`, `cs`, `de`, `sk`, `pl` (ADR-0221).

### Error cases

No new error codes. `ble.permission_denied` loses its wizard (ADR-0250, accepted) until 0032.

## Decisions requiring user confirmation

| Question | Options | Recommendation | Decision (by user) | ADR |
|---|---|---|---|---|
| Wizards in 2.0.0 | all three / setup only / none | — | **all three; 0010 app firmware only, LTE via `lte-update`** (2026-11-04) | ADR-0245 |
| Translations for 2.0.0 | cs + de + sk + pl complete / cs only | — | **all four, 0 unfinished** (2026-11-04) | ADR-0248 |
| Connect wizard scope after ADR-0233 / BLE | J-Link only, app only, no Device step / + Bluetooth branch / keep 0009 Device step | J-Link only, app only, no Device step | **J-Link only, app only, no Device step** (decided by Matej Strnad 2026-11-04) | ADR-0249 |
| BlePairing (0032) in 2.0.0 | defer (coverage test excludes BlePairing, `ble.permission_denied` without wizard) / ship as a fourth wizard | defer — note: all three M4 wizards still ship | **defer — note: all three M4 wizards still ship** (decided by Matej Strnad 2026-11-04) | ADR-0250 |
| Troubleshooting content owner | derived from docs + HARDWARIO review, art/connector names placeholders / HARDWARIO writes / keep 2026-10 texts | derived from docs, HARDWARIO reviews | **derived from docs, HARDWARIO reviews** (decided by Matej Strnad 2026-11-04) | ADR-0251 |
| Start-up trigger | keep ADR-0071 / first start only / never | keep ADR-0071 | **keep ADR-0071** (decided by Matej Strnad 2026-11-04) | ADR-0071 |
| Existing rc users (no `setup/completedVersion`) | wizard opens once after the upgrade / mark completed in the schema-3 migration | opens once (they never saw it) | **opens once (they never saw it)** (decided by Matej Strnad 2026-11-04) | — |
| Probe picker in wizards + implementation order | shared `ProbeList.qml` + own view model, in-use rows disabled; picker → 0008 ∥ 0010 → 0009 / reuse tab's picker / ADR-0065 order | as listed | **as listed** (decided by Matej Strnad 2026-11-04) | ADR-0252 |
| Flash wizard kind step | none (LTE link → `lte-update`) / keep two cards | none | **none** (decided by Matej Strnad 2026-11-04) | ADR-0253 |
| Language on the setup Welcome step | combo on Welcome / Settings only | combo on Welcome | **combo on Welcome** (decided by Matej Strnad 2026-11-04) | ADR-0254 |

## Implementation steps

Phase A — probe picker (before any wizard)
- [ ] A0 Prerequisite: this plan and the amended 0008/0009/0010 re-approved; decisions above answered
- [ ] A1 `ProbeList.qml` extracted; ChooseDevicePage uses it; existing Qt Quick tests (`tst_shell.qml`,
  `tst_screenshots.qml`) green without screenshot changes
- [ ] A2 `ProbePickerViewModel` wizard mode + `selectedSerial()` + GTest (`probe_picker_view_model_test.cpp`)
- [ ] A3 `ProbePickerStep.qml`, `registerProbePickerStep`, `AppViewModel::makeProbePicker` + Qt Quick test with the
  test wizard (`app/gui/tests/support/test_wizard.*`)

Phase B — 0008 (parallel with C)
- [ ] B1 Implement 0008 as amended (its S0–S5); schema 3; `StartupCheck`; coverage test started
- [ ] B2 `ble.permission_denied` wizard → none (if ADR-0250 accepted); docs sentence fix in five languages

Phase C — 0010 (parallel with B)
- [ ] C1 Implement 0010 as amended (its W0, W2–W5; W1 done)

Phase D — 0009 (after B and C)
- [ ] D1 Implement 0009 as amended (its T0, T2–T6) with the topic table of § C; coverage test complete
- [ ] D2 Docs (`getting-started`, `flashing`, `troubleshooting`) in en, cs, de, sk, pl

Phase E — finish
- [ ] E1 `update_translations`; cs + de + sk + pl 0 unfinished (`check_translations`); screenshots of every new step
  in light/dark, EN + DE (longest) at 960 × 600
- [ ] E2 Verification below; roadmap and plan statuses updated

## Tests and verification

- **GTest:** picker wizard mode (in-use rows not selectable, no `tabActivationRequested`, `selectedSerial`); coverage
  test (§ D); each amended plan's tests as listed in 0008 / 0009 / 0010; schema 2 → 3 migration (no-op, read-only
  newer schema untouched); `StartupCheck` decision table incl. "another wizard open" and read-only settings.
- **Qt Quick Tests:** Choose device page unchanged (existing tests); `ProbePickerStep` in light/dark; keyboard-only
  selection; every new wizard step at 960 × 600 in German and Czech.
- **Sanitizers:** ASan/UBSan full suite; TSan with the scanner result racing wizard close (picker destroyed while a
  scan result is queued) and the start-up check racing window creation.
- **clang-tidy / clang-format / qmllint:** zero findings (`linux-tidy`, `all_qmllint`).
- **HIL / manual (by the user, Linux, Windows, macOS):** fresh user account → setup wizard → probe step with 0, 1, 2
  probes and a probe used by another tab; connect wizard with the cable on the LTE connector; flash wizard from an
  empty tab; troubleshooting topic list in each language.

## Risks and open points

- **Three plans in one release:** the plans are re-approved together; slipping 0009 last keeps 0008/0010 shippable
  (0009's hand-offs are hidden while it is not registered).
- **Content (open question 22):** connector names and art stay placeholders; HARDWARIO review of the topic texts is a
  Done condition of 0009 (ADR-0251).
- **Settings schema collision** with another branch bumping to 3 — resolved at merge by renumbering (rule in § B).
- **German length:** wizard footers and check rows at 960 × 600; screenshots in DE are part of E1.
- **Docs drift:** topic texts and `troubleshooting.md` must change together; the 0037 anchor test catches missing
  anchors but not content drift — review rule in `docs/README.md`.
