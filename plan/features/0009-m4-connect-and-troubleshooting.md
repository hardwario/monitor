# 0009 — M4 connect-device and troubleshooting wizards

- **Status:** In progress (implemented 2026-11-09 on `strnad/impl-0009`; HIL, manual checks, T6 content and the HARDWARIO review of the topic texts open). Re-approved with the [0035](0035-wizards-rebaseline-2-0-0.md) amendments, 2026-11-04. Previously: Approved (2026-10-16, by Matej Strnad; without the pending M5 amendments)
- **Milestone:** M4 (plan 3 of 4, ADR-0065)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0032, ADR-0050, ADR-0053, ADR-0057, ADR-0058, ADR-0066, ADR-0067, ADR-0069, ADR-0072,
  ADR-0073, ADR-0076, ADR-0077
- **Depends on:** [0007](0007-m4-wizard-framework-diagnostics.md) (wizard contract, `Diagnose`, `libs/diag`),
  [0004](0004-m3-gui-shell.md) (probe picker, device tabs)
- **Can run in parallel with:** [0008](0008-m4-first-run-setup.md), [0010](0010-m4-flash-wizard.md)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Amendment (plan [0056](0056-wizards-as-views-rc5.md), 2026-11-23):** both wizards are views of their device tab (ADR-0391, one per tab), refused on a remote-support tab (ADR-0393) and while the setup wizard is open; while one is open, the tab's "Choose another device" is refused with a toast (D-11); the ADR-0300 chain state is per tab; their checks use a DiagWorker requester token (ADR-0394).

> **Pending amendment from M5 ([0012](0012-m5-ble-gui-pairing.md), ADR-0084; applied when 0012 is approved):** the
> coverage test also accepts error codes whose wizard is BlePairing when the BlePairing wizard maps them to a start
> step; the Connect wizard stays J-Link only.

> **Pending amendment from M8 ([0017](0017-m8-parity.md), ADR-0132):** the troubleshooting topic `probe-firmware` gets an
> [Update J-Link firmware] step; the `hio jlink update-firmware` CommandBlock stays as the alternative.

> **Amendment by [0035](0035-wizards-rebaseline-2-0-0.md) (2026-11-04; ADR-0245, ADR-0248 accepted; ADR-0249, ADR-0250,
> ADR-0251, ADR-0252, ADR-0255 accepted 2026-11-04; re-approved 2026-11-04 by Matej Strnad). Supersedes the conflicting text below where they differ
> (including the two pending amendments above):**
> - **Scope:** ships in 2.0.0 (ADR-0245); implemented last (after 0008 and 0010, ADR-0252).
> - **Connect wizard (ADR-0249):** J-Link only, application MCU only, **no Device step** — steps Probe · Cable
>   · Power · Check · Done. The Device step returns automatically only with a second J-Link profile (plan 0033 D1).
>   Cable shows the console target's `connectionGuide`. On Check, `target.wrong_family` / `target.other_connector` for
>   the LTE MCU → "The J-Link is on the LTE connector. Move it to the APP connector." [Show me how] (→ Cable) and
>   [Update LTE firmware…] (hand-off `lte-update`, ADR-0234). `target.not_connected` starts at Check. `diagnose(target)`
>   always runs on the console target (ADR-0233). The `target.protected` hand-off is
>   `open-flash-page:recover:app` (ADR-0236, 0033).
> - **Probe step:** the Embedded `probe-picker` of 0035 (wizard mode: rows used by another tab disabled, no tab switch).
> - **Troubleshooting:** the topic table is replaced by 0035 § C (derived from `docs/en/troubleshooting.md`, HARDWARIO
>   review, ADR-0251). New codes: `probe.io_failed` (`no-probe`), `target.rtt_write_failed` (`console-stuck`),
>   `probe.firmware_update_failed` (`probe-firmware`). New topics `other-connector` and `bluetooth` (manual, no codes).
>   `modem-failed` hands off to `lte-update` instead of the Flash page.
> - **`probe-firmware` topic:** [Update J-Link firmware…] from plan 0036 (ADR-0255; the tab is disconnected
>   first, ADR-0246), the `hwio jlink update-firmware` CommandBlock as the alternative. This replaces the M8 pending
>   amendment above (ADR-0132's pause/resume is superseded by ADR-0246).
> - **BLE (ADR-0250):** the coverage test skips codes whose wizard is BlePairing while 0032 is not registered;
>   the Connect wizard stays J-Link only. This replaces the M5 pending amendment above until 0032.
> - **Coverage test:** `app/gui/tests/wizard/wizard_coverage_test.cpp` (0035 § D).
> - **Choose device page:** the disabled "Help me fix this" of the J-Link empty state (`ChooseDevicePage.qml`, "Coming
>   soon" tooltip) becomes [Guide me]; Help menu items already exist in `HelpMenu.qml`.
> - **Names (0024):** `hio` → `hwio`, `hio::gui` → `hwio::gui`.
> - **Texts:** cs + de + sk + pl with 0 unfinished (ADR-0248); step T5 reads "cs + de + sk + pl".

## Context

Most support cases are physical: the J-Link is not plugged in, the flat cable is on the other MCU's connector,
CHESTER has no power, another program holds the probe. The old app showed only "Attach Failed."; py-hardwario says
"No J-Link found (check USB cable)" or "Detected low voltage". M1–M2 give precise error codes, and 0007 adds
step-by-step `Diagnose`. This plan turns them into two wizards:
- **Connect device** (`connect-device`) — guides a user from nothing to a connected tab;
- **Troubleshooting** (`troubleshooting`) — one topic per error code, with automatic checks and manual steps.

## Goals / Non-goals

**Goals**
- Connect wizard: device → target → probe → cabling → power → live check → connected tab.
- Uses the connection guide from the device profile (ADR-0077; defined in 0007).
- Troubleshooting topics for **every** error code that names ConnectDevice or Troubleshooting (table below), plus
  a topic list when opened without an error.
- Replace the disabled "Help me fix this" in the M3 Choose-device empty state with an active "Guide me" button.
- EN + CS texts, accessibility.

**Non-goals**
- Real CHESTER cabling art and exact connector names (placeholders; open question 22, ADR-0077).
- J-Link firmware update button (open question 19) — the `probe.firmware_outdated` topic shows the CLI command.
- BLE (M5).

## User experience

### Connect wizard (`connect-device`, per tab)

Step list: Device · Probe · Cable · Power · Check · Done.

1. **Device** [Choice] — "Which device do you want to connect?" Cards: CHESTER (the only profile today; the step is
   skipped when there is exactly one profile) and the target: "Application MCU (app) — console and application
   firmware" / "LTE MCU (lte) — LTE firmware" (default: the tab's target, else `app`).
2. **Probe** [Embedded `probe-picker` + Checks `probes`, `probe-holders`, `usb-access`]
   - The M3 picker (in-use rows, 2 s refresh). Body: "Connect the J-Link to your computer with a USB cable." Slot
     `jlink-usb`.
   - No probe: the checks explain why (USB access on Linux, programs holding probes, "Is the J-Link's LED on?").
     [Check again].
   - Exactly one free probe → preselected; [Next] enabled when a free probe is selected.
3. **Cable** [Info, from `TargetProfile::connectionGuide`] — one step per guide entry (CHESTER: one step per
   target), e.g. "Connect the J-Link flat cable to the programming connector of the application MCU." Slot
   `chester-<target>-cable`. [Next].
4. **Power** [Info] — "Power CHESTER with batteries or the USB-C power input." (`DeviceProfile::powerHintSourceText`,
   final wording in open question 22). Slot `chester-power`. [Next].
5. **Check** [Checks: the six `Diagnose` steps, ADR-0069]
   - The wizard opens a probe-only session for the chosen probe and target (`ensureSession(connectTarget =
     false)`, 0007 Implementation notes; the target is carried by `diagnose(target)`) and runs `diagnose(target)`. Rows tick as steps finish.
   - All Ok → [Connect] (default).
   - A step fails → the row expands with cause and "What to do", and the wizard offers [Show me how] which jumps back
     to the matching step (no power → Power, `target.connect_failed` / `target.wrong_family` → Cable with the
     text "The J-Link seems to be connected to the %1 MCU. Move the flat cable to the %2 connector." when the
     identified family belongs to the other target) and [Check again].
   - `target.protected` (Warning): "The MCU is read-back protected. Recover removes the protection but erases the
     MCU completely." with the hand-off [Open Flash page] (Recover is under Maintenance, ADR-0038). Connect is still
     possible (RTT usually fails on a protected MCU).
   - `target.rtt_not_found` / `rtt-buffers`: "The device has no console. It may need firmware with the CHESTER
     shell." [Flash firmware…] (0010) · [Connect anyway].
6. **Done** — [Connect] promotes the probe-only session to a normal connection (`startConnection`, below); the tab
   becomes Connecting → Connected as with `connectDevice()` (0004).
   When the tab is Connected: "Connected to CHESTER app · 683012345." with [Open console] (default) · [Close].
   - If the tab was already connected to the same probe and target when the wizard started, steps 1–4 are skipped
     and the wizard is a pure check.

### Troubleshooting wizard (`troubleshooting`, per tab)

- **With an error** (from "Help me fix this"): the topic for the error code. The first step shows the error
  (headline, cause) and "Let's find the cause.", then the topic's steps.
- **Without an error** (Help → Troubleshoot connection…): a Choice step "What is the problem?":
  "No J-Link found" · "Cannot connect to the device" · "The console stays empty" · "Flashing fails" ·
  "Another program uses the J-Link" → the matching topic.
- Every topic ends with **Still not working?** [Done]: "Create a diagnostic report and send it to HARDWARIO
  support." [Create diagnostic report…] (0007) · [Close].

Topics (each row: steps in order; **A** = automatic check, **M** = manual instruction with [Next]; `Check again`
re-runs the automatic checks of the step):

| Topic | Error codes | Steps |
|---|---|---|
| `no-probe` | `probe.not_found` | A `probes` · A `usb-access` (Linux) · A `probe-holders` · M "Unplug and reconnect the J-Link; try another USB port and cable; the J-Link LED should light up." · A `probes` again → Ok: "Found — connect now" [Connect] |
| `probe-busy` | `probe.open_failed`, `probe.in_use_by_other_process`, `probe.held_by_program` | A `usb-access` (Linux; Failed → commands of ADR-0072) · A `probe-holders` → list "Close these programs: J-Link Commander (PID 4242) …" · for `in_use_by_other_process`: M "Close the other HARDWARIO Monitor window or the `hio` command (PID %1)" · M "Unplug and reconnect the J-Link." · A Diagnose `probe` |
| `no-power` | `target.no_power` | M power hint + slot `chester-power` · A Diagnose `probe`, `power` (shows the measured voltage) · M "Check that the flat cable is fully inserted at both ends." (slot `chester-<target>-cable`) · A `power` again |
| `no-connect` | `target.connect_failed`, `target.wrong_family` | A Diagnose `probe`..`identify` · M cabling guide for the target (wrong family: "Move the flat cable to the %1 connector.") · M "Lower the J-Link speed" with [Use 1000 kHz] (sets the tab's speed, ADR-0052) · A Diagnose again |
| `no-console` | `target.rtt_not_found`, `target.rtt_buffer_not_found` | A Diagnose (all) · M "Reset the device" [Reset] (session `reset(target, Go)`) · A `rtt` again · M "The firmware may not have the CHESTER console (RTT shell). Flash firmware with console support." [Flash firmware…] (0010) |
| `console-stuck` | `target.rtt_write_timeout`, `target.rtt_read_failed` | A Diagnose `probe`, `power`, `connect` · M "The device may be busy or hung." [Reset] · A `rtt` · M "Check the cables; the console reconnects automatically when auto-reconnect is on." |
| `probe-firmware` | `probe.firmware_outdated` | A `probe-firmware` · M "Update the J-Link firmware: run `hio jlink update-firmware` or open J-Link Commander, which offers the update." (`CommandBlock`) · A `probe-firmware` again |
| `protected` | `target.protected` | M "The MCU is read-back protected. Recovering erases it completely, including the Product Information Block." · hand-off [Open Flash page] as `open-flash-page:recover:<target>` (amended by 0033, ADR-0236: Maintenance unfolded and [Recover…] shown for that MCU once, without the expert setting; with its confirmation, ADR-0038) |
| `program-failed` | `target.program_failed`, `target.erase_failed`, `firmware.verify_failed` | A Diagnose `probe`..`identify` · M "Lower the J-Link speed" [Use 1000 kHz] · M "Make sure the power is stable during flashing (batteries charged or USB-C power)." · hand-off [Try again on the Flash page] |
| `recover-failed` | `target.recover_failed` | M "Disconnect the power for 10 seconds, then connect it again." · A Diagnose `probe`, `power`, `connect` · hand-off [Open Flash page] |
| `modem-failed` | `firmware.modem_update_failed` | A `nrfutil` · M "Check cable and power; the modem update takes 1–3 minutes and must not be interrupted." · M "Use the modem firmware .zip for nRF91 from Nordic (mfw_nrf91…zip)." · hand-off [Open Flash page] |

The topic table is compiled in (`troubleshooting_topics.cpp`, ADR-0067); a unit test asserts that every error code
whose wizard is ConnectDevice or Troubleshooting has exactly one topic or is handled by the Connect wizard.

### ConnectDevice errors

`probe.not_found`, `target.no_power`, `target.connect_failed`, `target.wrong_family` name ConnectDevice. "Help me
fix this" opens the **Connect wizard** with the tab's probe and target preselected, starting at:
`probe.not_found` → Probe; `target.no_power` → Power; `target.connect_failed` / `target.wrong_family` → Cable. The
Check step then confirms the fix.

### Entry points

- "Help me fix this" on ErrorPanels (device bar errors, console page, flash page, probe picker).
- Choose-device page (0004): the empty state's disabled "Help me fix this" becomes **[Guide me]** → Connect wizard
  (0004 amendment); the page also gets a ghost link "Not sure how to connect? Guide me".
- Help → "Connect a device…" / "Troubleshoot connection…".
- Done step of the setup wizard (0008).

### Texts

English source texts as above. Czech is translated in Qt Linguist as part of done (ADR-0027).

## CLI

None new. The CLI equivalents are `hio chester app|lte check` and `hio doctor` (0007). Their human output for a
failed step ends with the topic's first manual instruction ("What to do: …").

## Design

### Modules

```
app/gui/src/wizard/
  connect_wizard.*                      ConnectWizard : Wizard ("connect-device", per tab)
  troubleshooting_wizard.*              TroubleshootingWizard : Wizard ("troubleshooting", per tab)
  troubleshooting_topics.*              compiled-in topic table (ADR-0067)
app/gui/tests/ wizard/connect_wizard_test.cpp troubleshooting_wizard_test.cpp tst_connect_wizard.qml
```

### Topic table (sketch)

```cpp
namespace hio::gui {
enum class TopicStepKind { AutoEnvironment, AutoDiagnose, Manual, Commands, HandOff };
struct TopicStep {
    TopicStepKind kind;
    std::span<const std::string_view> checkIds;   // AutoEnvironment: diag check ids; AutoDiagnose: step ids ("" = all)
    const char* headingSourceText;                // QT_TRANSLATE_NOOP("Troubleshooting", …)
    const char* bodySourceText;
    std::string_view illustrationSlot;            // or "guide:cable" / "guide:power" = from the device profile
    std::string_view action;                      // Manual: "reset" | "speed-1000" | ""; HandOff: "open-flash-page" | "flash-wizard" | "report"
};
struct Topic {
    std::string_view id;
    std::span<const hio::ErrorCode> codes;
    const char* titleSourceText;                  // shown in the topic list
    std::span<const TopicStep> steps;
};
[[nodiscard]] std::span<const Topic> troubleshootingTopics();
[[nodiscard]] const Topic* topicFor(hio::ErrorCode);
}
```

### Behaviour

- **Sessions:** both wizards use the tab's session when one exists (Diagnose on a connected session, ADR-0069);
  otherwise they open a probe-only session through the tab and close it at the end, unless [Connect] follows. Then
  the same session is promoted with `DeviceSession::startConnection(attachRtt = true)` (0007 amendment to 0002):
  the probe and its `ProbeLock` stay held, so no other process can take the probe in between.
- **Busy:** while Diagnose runs the tab is busy (`setBusy("connect-device", "Checking the connection…", …)`), so the
  Flash page cannot start an operation at the same time (`internal.busy` would be returned anyway).
- **Manual actions:** `reset` → `session->reset(target, Go)`; `speed-1000` → the tab's per-probe speed setting
  (ADR-0052) set to 1000 kHz with a toast "J-Link speed set to 1000 kHz for this probe."
- **Wrong connector hint:** `target.wrong_family` carries the found part; the wizard looks up which target of the
  profile owns that part (`TargetProfile::parts`) and names it.
- **Cancel:** cancels a running Diagnose (`cancel(OperationId)`), closes a probe-only session it opened, leaves a
  connected tab untouched.

### Error cases (GUI-originated)

| Situation | Code | Shown as | Wizard |
|---|---|---|---|
| Probe picked in the wizard is used by another tab | (not an error) | row switches to that tab, as in 0004 | — |
| Diagnose while the tab runs an operation | `internal.busy` (0003) | check row "Another operation is running — wait for it to finish." | — |
| Probe-only session cannot be opened | the open error (`probe.*`) | first check row Failed; topic `probe-busy` offered | — |

### Illustration slots (ADR-0076)

Uses `jlink-usb`, `chester-app-cable`, `chester-lte-cable`, `chester-power` (defined in 0007). No new slots.

### Bounds

Topics ≤ 32, steps per topic ≤ 8, Diagnose per "Check again" 1 at a time (repeated clicks are ignored while one
runs), probe-only sessions per tab 1.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Live checks | session operation / standalone / try to connect | `Diagnose` operation | ADR-0069 |
| Programs holding the probe | ask only / process scan | process scan | ADR-0073 |
| Cabling guidance location | device profile / GUI code | device profile | ADR-0077 |
| CHESTER connector names and wording | describe now / placeholder | placeholder, HARDWARIO content task | ADR-0077, open question 22 |
| Illustrations | slots + placeholders | slots + placeholders | ADR-0076 |

## Implementation steps

- [x] T0 Prerequisite check: 0007 done (Diagnose, `libs/diag`, wizard contract); 0004 done (otherwise stop)
- [ ] T1 (moved to 0007 step A7: connection guide in the profile)
- [x] T2 `ConnectWizard` (steps, start step per error, skip rules, session promotion with kept lock) + GTest
- [x] T3 Topic table + `TroubleshootingWizard` + coverage test (every ConnectDevice/Troubleshooting code handled)
- [x] T4 Choose-device page "Guide me" (0004 amendment), registration, Help menu items
- [x] T5 Accessibility; Czech translation; `.ts` fresh; screenshots
- [ ] T6 Content: HARDWARIO supplies connector names, power wording and art (open question 22) — required for Done

## Tests and verification

- **GTest (fake J-Link library):** connect wizard happy path; each Diagnose failure leads to the right step and
  back; `target.wrong_family` names the other connector; already-connected tab skips to Check; cancel during
  Diagnose; probe lock kept across the session promotion (a second process cannot take it — `ProbeLock` test);
  every topic runs end to end with fake results; manual actions call `reset` / set the speed.
- **Coverage test:** the set of codes with wizard ConnectDevice/Troubleshooting in `errorTable` equals the codes
  covered by the Connect wizard's start mapping plus the topic table.
- **Qt Quick Tests:** each step in light/dark; 960 × 600 in Czech; keyboard walkthrough; check rows expand.
- **Sanitizers:** TSan with Diagnose progress racing Cancel and dialog close.
- **HIL (CHESTER + J-Link):** a user walkthrough from an unplugged J-Link to a connected console; power off during
  Check; cable on the wrong connector; J-Link Commander open; firmware without RTT.
- **Manual (by the user, before Done):** walkthrough on Linux, Windows and macOS; Czech texts at 960 × 600.

## Risks and open points

- **Connector names and art** are missing (open question 22); the wizard is functional but generic until then.
- **Cross-target Diagnose** depends on the M2 spike 0.5b (open question 18). If one J-Link cannot reach both MCUs
  from one connector, `target.wrong_family` is the expected result for the other target and the text must say
  "move the cable" rather than "switching".
- **Speed change** affects only this probe (ADR-0052 per-probe memory); the toast says so.

## Implementation notes (2026-11-09, `strnad/impl-0009`)

Branch `strnad/impl-0009` = 0035 picker + 0008 + 0010 (merge f5116c1) + this plan. Decisions of the implementation
(Matej Strnad, 2026-11-09): Q1 → ADR-0297 (`Wizard::handOffError`), Q2 → ADR-0298 ([Use 1000 kHz] applies at
once), Q3 → ADR-0299 (troubleshooting on a tab without a probe), Q4 → the note below; choices 1–9 accepted as
recorded here.

**Files.** `app/gui/src/wizard/connect_wizard.*` (`ConnectWizard`), `troubleshooting_wizard.*`
(`TroubleshootingWizard`, `registerConnectAndTroubleshootingWizards`, called from `registerBuiltInPages` after the
setup wizard), `troubleshooting_topics.*` (the table of 0035 § C), `diagnose_runner.*` (`DiagnoseRunner`: Diagnose /
[Reset] on the tab's or a probe-only session, the busy state, releasing the probe-only session; shared by both
wizards), `app/gui/qml/wizard/WizardErrorStep.qml` (control `wizard-error`: an error's cause and suggestion as plain
text, option `showMessage`). Tests: `app/gui/tests/wizard/connect_wizard_test.cpp`,
`troubleshooting_wizard_test.cpp`, `wizard_coverage_test.cpp` (ConnectDevice + Troubleshooting cases, topic table
bounds and list order), `app/gui/tests/tst_connect_wizard.qml` (tst_gui registers the real Connect wizard for this
file only; the other QML tests keep the test wizard as `connect-device`).

**Q4 (Diagnose stop point, ADR-0296).** A topic's automatic Diagnose step runs up to its last listed step; where that is
`power` (not a stop point) it runs up to `connect`, and the step shows only the listed rows (`no-power`: "probe,
power" and "power").

**Accepted choices (1–9 of the question round):**
1. Connect wizard start without an error: a connected tab is a pure check (Check, no Back); a tab with a probe starts
   at Cable (ADR-0074, the wizard picker would show the tab's own probe as "In use (tab N)"); with `probe.not_found` the
   tab returns to its picker (`chooseAnotherProbe`) and its serial is preselected; a tab without a probe always starts
   at Probe. Back into Probe also returns the tab to its picker.
2. Connect Done: "Connecting…" (Info) while the tab connects; "Connected to CHESTER app · <SN>." (Done) with
   [Open console]; a failed connection shows "Could not connect" with the tab's error in `wizard-error` and
   [Check again] → Check.
3. [Create diagnostic report…] closes the wizard and opens the report dialog (`ReportViewModel::show`).
4. Topic steps have short titles for the step list; the error introduction and the topic Choice are not listed; the
   end step is titled "Support".
5. The introduction: heading = the error's message (plain text), body "Let's find the cause.", the cause and the
   suggestion in `wizard-error` with `{showMessage: false}` (ADR-0184, ADR-0290, ADR-0292).
6. The test wizard keeps the id `connect-device` in the tests that use it by being registered before
   `registerBuiltInPages` (the first registration wins); the "unregistered wizard" tests use `ble-pairing`
   (ADR-0250); the setup-wizard tests use the real Connect wizard.
7. `probe-firmware`: a Commands step with `hwio jlink update-firmware -n <SN>` per outdated probe (else without
   `-n`) and one [Update J-Link firmware <SN>…] per outdated probe from the facts of the topic's `probe-firmware`
   check (ADR-0291 pattern), disabled with `blockedReason` (e.g. "Disconnect tab 1 first.", ADR-0246).
8. Plan 0036 P5: `ProbeFirmwareDialog.qml`'s failure panel offers "Help me fix this" instead of [Close] when the
   error's wizard is registered and no wizard is open; it closes the dialog and then opens the wizard with the error
   (new `ErrorPanel.showWizardButton` so the panel's own button is not shown twice).
9. Plan 0035 D2 (docs) done on this branch in en, cs, de, sk, pl.

**Further choices (not in the plan; please review):**
- Consecutive automatic environment checks of 0035 § C are one Checks step (the table sketch's `checkIds` span), e.g.
  `no-probe` step 1 = `probes`, `usb-access` (Linux), `probe-holders`. A manual instruction followed by a hand-off is one
  step with the hand-off button (`other-connector`, `no-console` step 4, `protected`, `program-failed` step 4,
  `recover-failed` step 3, `modem-failed` step 3). Every automatic step has [Check again]; [Next] is enabled when the
  checks have finished (also after a failure).
- A failed (or Warning) environment check whose error names the setup wizard offers [Setup wizard…], which hands the
  error off (ADR-0297) — `usb-access` → USB step, `nrfutil` → nRF Util step (0035 § C "Failed → H setup step 3 / 4").
- `no-probe` last step: when `probes` passes, the heading becomes "Found — connect now" with the button [Connect]
  (`open-wizard:connect-device`).
- Topic list entry "The device has no power or does not respond" opens `no-power`; `no-connect`, `console-stuck`,
  `protected` and `recover-failed` are reached by their error codes only (their list titles exist for completeness).
- Connect Check outcome order: wrong connector (`target.wrong_family` / `target.other_connector` on `connect` or
  `identify`) → no power → connect failed → probe failed → protected → no console (`rtt` / `rtt-buffers` Failed) →
  other failure → passed. Another failure whose error names a registered wizard other than this one gets
  [Help me fix this] (ADR-0297; e.g. a probe held by another program → troubleshooting `probe-busy`). Protected and
  no-console also offer [Check again].
- The tab is busy (`setBusy(<wizard id>, "Checking the connection…", …)`) while Diagnose or [Reset] runs, in both
  wizards; the Cancel confirmation asks only while one runs. [Reset] ends with the toast "The device was reset." or
  the error's message.
- Texts written for steps the plan does not word (headings, step titles, button texts, the probe-step body "The
  checks need a J-Link. Choose the one connected to the device."), condensed from `docs/en/troubleshooting.md`
  (ADR-0251; HARDWARIO review open).
- Choose device: the empty state's [Guide me] is shown only while `connect-device` is registered (as the flash
  link); the ghost link "Not sure how to connect? Guide me" is in the J-Link view above "Flash firmware with a
  guide". The translations of "Guide me" reuse the Flash page's (cs "Provést mě", de "Anleitung", sk "Previesť ma",
  pl "Poprowadź mnie").
- Plan 0010's [Help me fix this] now hands the error off (ADR-0297): troubleshooting opens at the topic (test
  `FlashWizardTest.HelpMeFixThisOpensTheMatchingTopic`).

**Verification (2026-11-09, this machine):** linux-debug ctest 1783 tests (1769 passed, 14 HIL/corpus/load skipped),
the new GTests (`ConnectWizardTest.*`, `TroubleshootingWizardTest.*`, `WizardCoverage.*`) passed 5× in a row and
`tst_gui.tst_connect_wizard` 3×; full `linux-tidy` zero findings; `all_qmllint` clean; `check_translations` up to date
with 0 unfinished cs/de/sk/pl; clang-format-21 clean; `tools/check-lfs.sh --untracked` passes. Not run: ASan/TSan
builds. HIL with CHESTER and the manual checks (Linux, Windows, macOS; Czech at 960 × 600) are open. Screenshots (light/dark, EN/CS, 960 × 600) were generated with
`HWIO_SCREENSHOT_DIR` and are not in the repository.


## Review fixes (2026-11-09, independent review of `strnad/impl-0009`)

Decisions by Matej Strnad (2026-11-09): **A** → ADR-0300 (the tab owns the wizards' probe-only session; it passes
between wizards chained on the tab and is released when idle after the last one closes without a session-consuming
hand-off); **B** → the Connect wizard opened from `probe.not_found` gives the tab its original probe back when it is
closed without another probe chosen.

| # | Finding | Fix | Tests |
|---|---|---|---|
| 1 | Flash wizard closed during the Cable-step Diagnose: Diagnose not cancelled, session leaked | The destructor cancels it; `busyCancelText` includes the Cable check (Cancel asks, then cancels); the tab releases the session when the operation has ended (ADR-0300) | `FlashWizardTest.ClosingDuringTheCableCheckCancelsIt` |
| 2 | A session kept by a hand-off was never closed by the receiving wizard | ADR-0300 (`DeviceTabViewModel::ensureWizardSession`, `releaseWizardSessionWhenIdle`, `keepWizardSession`; `WizardViewModel::openedOnTab` / `closedOnTab`; `AppViewModel::openChainedWizard`) | `FlashWizardTest.HelpMeFixThisOpensTheMatchingTopic`, `.AHandOffToSettingsReleasesTheSession`, `ConnectWizardTest.AChainedWizardTakesOverTheSession`, `.AHandOffToTheSetupWizardReleasesTheSession`, `TroubleshootingWizardTest.ClosingTheLastChainedWizardReleasesTheSession` |
| 3 | `DiagnoseRunner::keepSession` stayed set after a failed connect | The per-wizard flags are gone (ADR-0300) | `ConnectWizardTest.CheckAgainAfterAFailedConnectionLeavesNoSession` |
| 4 | Troubleshooting: Next after Back entered the probe step again (own tab "In use") | `primary()` skips the probe step while the tab has a probe; dead `canBack` code removed | `TroubleshootingWizardTest.BackAndNextSkipTheProbeStep` |
| 5 | A Diagnose step that is not shown failed and the shown row only said "Not checked" | `DiagnoseRunner`: the first shown row it kept from running gets its status, result and error | `TroubleshootingWizardTest.AHiddenFailedStepExplainsTheShownRow` |
| 6 | `onHandOffError` fell back to the current tab when the source tab had closed | Nothing opens when the source tab is gone | `ConnectWizardTest.AHandOffToAClosedTabOpensNothing` |
| 7 | `StartupCheck` never decided when a wizard's checks replaced its pending `DiagWorker` request | `DiagWorker::requestDropped(generation)` (queued); the start-up check and the wizards submit a dropped request again | `StartupCheckTest.AReplacedRequestRunsAgain` |
| 8 | The Done heading used the raw target id | "Connected to CHESTER · Application MCU (nRF52840) · <SN>." (`ProbePickerViewModel::targetTitle`) | `ConnectWizardTest.FullFlowFromAnEmptyTab` |
| B | — | `Wizard::aboutToClose()` (new, default nothing; not called when the tab closes); `ConnectWizard` reconfigures the tab with its original probe when it is still on its picker | `ConnectWizardTest.CancelRestoresTheTabsProbe` |

Choices: decision B is applied whenever the Connect wizard returned a tab that had a probe at the start to its picker
(also Back into the Probe step), restoring the probe the tab had when the wizard started. The release waits for the
tab's busy state and the running operation, and covers a wizards' session in any state other than promoted.

Verification of the review fixes (2026-11-09): linux-debug ctest 1794 tests (1780 passed, 14 skipped); wizard GTests
4× in a row; `tst_gui.tst_connect_wizard` 3×; full `linux-tidy` zero findings; `all_qmllint` clean;
`check_translations` 0 unfinished; clang-format clean; LFS check passes. linux-asan: GUI wizard/tab tests (125) and
device-related unit tests (411) pass. linux-tsan: device-related unit tests (407) and the GUI wizard GTests pass;
`tst_gui.tst_connect_wizard`, `tst_flash_wizard` and `tst_setup_wizard` report one data race at the end of a
data-driven test row: the probe scanner thread logs through the Qt Test message handler while `QTestData` is
destroyed (Qt Test + `ProbeScanner` logging, the same class as review 0005 round 1; `tst_flash_wizard` and
`tst_setup_wizard` are unchanged by this branch). Fixed afterwards (2026-11-09): the race is in Qt Test's message
handler, which reads the current row's tag for every message; on `main` the existing Qt Quick tests (`tst_wizard`,
`tst_shell`, `tst_components`, `tst_probe_firmware`) do not show it under linux-tsan. `tst_gui` now routes messages
of worker threads past Qt Test's handler (straight to stderr in Qt's format); GUI-thread messages still go through
Qt Test. Qt Test installs its handler when a TestCase starts, so the data-driven wizard tests install the routing in
`initTestCase()` (`testSupport.routeWorkerThreadMessages()`). No TSan suppression. The three tests passed 3× each
under linux-tsan, all 23 Qt Quick tests once under linux-tsan, the full linux-debug ctest (1794) passed.
