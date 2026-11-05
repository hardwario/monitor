# 0036 — J-Link firmware update in the GUI

- **Status:** In progress (implementation 2026-11-04 on `strnad/impl-0036`; HIL and the 0008/0009 hooks open)
- **Milestone:** 2.0.0 (split from [0017](0017-m8-parity.md) § B on 2026-11-04)
- **Author:** Matej Strnad (with Claude), 2026-11-04
- **Related ADRs:** ADR-0132 (placement and dialog; session part superseded), ADR-0246 (accepted 2026-11-04:
  disconnect first), ADR-0248 (translations), ADR-0255, ADR-0256, ADR-0257 (accepted 2026-11-04); ADR-0030, ADR-0031,
  ADR-0058, ADR-0066, ADR-0093, ADR-0099, ADR-0105, ADR-0184, ADR-0261 (accepted 2026-11-04, review follow-up), ADR-0271 (accepted 2026-11-05, integration with 0014)
- **Depends on:** M1 backend (`device::updateProbeFirmware()`, `libs/device/include/hwio/device/probe_tools.h`),
  0007 (`DiagWorker`, check `probe-firmware`). Hooks in [0008](0008-m4-first-run-setup.md) and
  [0009](0009-m4-connect-and-troubleshooting.md) appear when those wizards exist (order in
  [0035](0035-wizards-rebaseline-2-0-0.md)); this plan does not wait for them.

## Context

The old app offered a J-Link firmware update when the probe firmware was outdated (legacy parity row, 0002). v2 has:

- the backend: `JLinkProbe::updateFirmware()` and the short-lived `device::updateProbeFirmware(registry, location,
  sn)` (own library copy, ADR-0030; holds the probe's `ProbeLock`, ADR-0058), used by `hwio jlink update-firmware
  [-n SN] [--yes]` (`app/cli/src/jlink_commands.cpp`);
- the session warning `probe.firmware_outdated` emitted once after connect (`libs/device/src/session_worker.cpp`),
  shown by `PageHost.qml` as the `connectionWarning` `WarningBanner` (text + details + dismiss, no action);
- the error `probe.firmware_update_failed` (`libs/core/src/error.cpp`, wizard Troubleshooting) and its docs entry;
- the diag check `probe-firmware` (`libs/diag/src/environment.cpp`).

ADR-0132 decided the GUI action; 0017 § B planned it with a session pause/resume. On 2026-11-04 the user decided
(ADR-0246) that the tab is **disconnected first** and the existing `updateProbeFirmware()` is used — no session
threading changes.

## Goals / Non-goals

**Goals**
- Banner action **[Disconnect and update…]** on a tab whose session reported `probe.firmware_outdated` (Outdated).
- Confirmation dialog, progress, result (ADR-0132 texts), one update at a time.
- Blocked with a reason while an operation (flash, LTE update, Diagnose) or MCP holds the probe.
- Hooks: setup wizard test-step row (0008) and troubleshooting topic `probe-firmware` (0009) (ADR-0255, accepted).
- Translations cs + de + sk + pl (ADR-0248).

**Non-goals**
- Session pause/resume (superseded by ADR-0246).
- Downgrade / "sync newer" from the GUI (`Newer` stays information only, ADR-0132); the CLI keeps its behaviour.
- MCP tool (ADR-0099 unchanged). CLI changes (parity already exists).

## User experience

**Banner** (replaces the plain `connectionWarning` for this one code, Outdated status only):

> ⚠ The J-Link firmware is outdated (%1, available: %2). **[Disconnect and update…]** [Not now]

- %1 / %2 = firmware dates (`ProbePickerViewModel::firmwareDate` format) of the probe and of the library, carried in
  the session's warning event (Design); unknown → the sentence without the parenthesis.
- [Not now] hides the banner for this probe serial until the app restarts (in-memory set, bounded 32; ADR-0132).
- Disabled with a tooltip when blocked: "Wait for the current operation to finish." (tab busy) · "Used by an AI
  assistant" (MCP, when plan 0014 exists) · "Another J-Link update is running."
- `Newer` status: the existing warning banner text "The J-Link firmware is newer than the J-Link library." without an
  action (unchanged).

**Flow from the banner:** click → the tab disconnects (same path as the device-bar Disconnect; the console log file
closes) → the confirmation dialog:

```
Update J-Link firmware?
<product> · SN <serial>
Current:   <firmware string of the probe>
New:       <firmware string bundled in the J-Link library>
Do not unplug the J-Link during the update (about 10–30 s).
                                       [Cancel]  [Update]
```

- Esc = Cancel, no default button (ADR-0132). Cancel → the tab stays disconnected; the banner is gone (no session) and
  the Choose device page shows the probe again.
- The strings come from `device::inspectProbe()` run on the worker before the dialog opens ("Reading the J-Link…"
  spinner ≤ 5 s); a failure there shows its error panel in the dialog with [Close].
- Update: indeterminate spinner "Updating J-Link firmware…" (no Cancel: the update must not be interrupted; × and Esc
  disabled, the `canCancel()` pattern of `LteUpdateWizard`). Success → toast "J-Link firmware updated." and, when
  started from a tab's banner, the tab connects again to the same probe (ADR-0256, accepted). Failure → the
  `probe.firmware_update_failed` ErrorPanel in the dialog with "Help me fix this" (troubleshooting `probe-firmware`, when
  registered); the tab stays disconnected.
- `before == Current` (somebody updated it meanwhile): toast "The J-Link firmware is already up to date."

**Setup wizard (0008 step 5) and troubleshooting topic `probe-firmware` (0009):** the Warning row / manual step gets
[Update J-Link firmware…] → the same dialog for that serial. If a tab uses the probe: "Disconnect tab %1 first."
(no automatic disconnect from inside a wizard). The topic keeps the CommandBlock `hwio jlink update-firmware -n
<serial>` as the alternative.

## CLI

None. `hwio jlink update-firmware [-n SN] [--yes]` exists and keeps its behaviour (incl. syncing a newer probe);
docs `docs/en/cli.md` unchanged.

## Design

### Modules

```
libs/device/include/hwio/device/probe_tools.h       + updateProbeFirmware(const RegistryOptions&, location, sn)  (registry-free)
libs/device/include/hwio/device/session_registry.h  + [[nodiscard]] ProbeReservation reserve(std::uint32_t sn)   (RAII, owner thread)
app/gui/src/workers/probe_scanner.*                 + [[nodiscard]] ScanSuspension suspend()                     (RAII, GUI thread)
app/gui/src/workers/probe_firmware_worker.*         NEW: one owned std::jthread per job (inspect | update)
app/gui/src/viewmodels/probe_firmware_view_model.*  NEW: app-level, owned by AppViewModel (Qt parent)
app/gui/src/viewmodels/connection_view_model.*      + firmwareOutdated, firmwareBlockedReason, updateProbeFirmware()
app/gui/qml/shell/PageHost.qml                      banner with [Disconnect and update…] [Not now]
app/gui/qml/dialogs/ProbeFirmwareDialog.qml         NEW
app/gui/tests/viewmodels/probe_firmware_view_model_test.cpp, tests/tst_probe_firmware.qml
libs/device/tests/probe_tools_test.cpp (overload), session_registry_test.cpp (reservation)
```

### `ProbeFirmwareViewModel` (GUI thread)

```cpp
class ProbeFirmwareViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")
    Q_PROPERTY(State state READ state NOTIFY changed)          // Idle, Reading, Confirm, Updating, Failed
    Q_PROPERTY(QString product READ product NOTIFY changed)
    Q_PROPERTY(QString serial READ serial NOTIFY changed)
    Q_PROPERTY(QString currentString READ currentString NOTIFY changed)
    Q_PROPERTY(QString availableString READ availableString NOTIFY changed)
    Q_PROPERTY(hwio::gui::ErrorInfo error READ error NOTIFY changed)
public:
    enum State : quint8 { Idle, Reading, Confirm, Updating, Failed };
    Q_ENUM(State)
    // "" when the update can start; otherwise the translated reason (tab busy, MCP, other update, in use by tab N).
    [[nodiscard]] QString blockedReason(std::uint32_t serial) const;
    // From a tab banner: the tab disconnects first (ADR-0246); `reconnectTab` per ADR-0256.
    void requestFromTab(DeviceTabViewModel& tab);
    // From the setup wizard / troubleshooting: refused with blockedReason when a tab uses the probe.
    Q_INVOKABLE void requestForSerial(const QString& serial);
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancel();   // only in Reading / Confirm / Failed
signals:
    void changed();
    void finished(bool updated);
};
```

- **Exclusivity (ADR-0257, accepted):** before the job the GUI thread calls `registry.checkAvailable(sn)` and takes
  `ProbeReservation` (no tab can connect to the serial; pickers show "Updating firmware…" via the existing in-use
  path) and `ScanSuspension` (the scanner stops enumerating while the probe re-enumerates). Both are released when the
  job's result has been handled (RAII members of the view model). The `ProbeLock` inside `updateProbeFirmware()`
  covers other processes (`hwio`, MCP stdio, J-Link Commander are reported by the lock / `probe.open_failed`).
- **Worker (ADR-0257, accepted):** `ProbeFirmwareWorker` starts one `std::jthread` per job, runs
  `inspectProbe(options, location, sn)` or `updateProbeFirmware(options, location, sn)` with a copy of
  `RegistryOptions` and the `LibraryLocation`, stores the `Result` in a mutex-protected slot and wakes the GUI thread
  with one queued call (ADR-0031 pattern). The thread is joined when the result is taken and in the destructor
  (shutdown waits for a running update — the update must not be killed; the quit path shows "Updating the J-Link
  firmware…" until it finishes, bounded by the vendor call). No `std::stop_token` cancellation of the vendor call.
- **Blocking:** tab `busyPage` non-empty (flash, `lte-update`, Diagnose) → "Wait for the current operation to finish.";
  MCP holds the device (ADR-0105, only once plan 0014 runs a GUI MCP server) → "Used by an AI assistant"; another
  update running → "Another J-Link update is running.".
- **Reconnect (ADR-0256, accepted):** after success from a banner, `tab.connectDevice()` on the same serial (the tab
  stays on Choose device if the probe is gone).
- **Bounds:** one job at a time; "Not now" set ≤ 32 serials (oldest dropped); inspect timeout 5 s (vendor default),
  update has no own timeout (vendor call; the docs say 10–30 s).

### `ConnectionViewModel` additions

`firmwareOutdated` (the warning is `probe.firmware_outdated` with status Outdated). Today Outdated and Newer differ
only in the message text (`session_worker.cpp`, connect path ~line 354); the session's warning event gains the
`jlink::FirmwareStatus` and the library's bundled firmware string (`JLinkLibrary::embeddedFirmwareString`, as
`inspectProbe` reads it) so the GUI never parses text. Then:
`firmwareBlockedReason`, `Q_INVOKABLE updateProbeFirmware()` → `ProbeFirmwareViewModel::requestFromTab(*tab)`,
`Q_INVOKABLE notNowProbeFirmware()`.

### Error cases

| Situation | Code | Shown as | Wizard |
|---|---|---|---|
| Probe gone / opened elsewhere before the update | `probe.not_found`, `probe.open_failed`, `probe.in_use_by_other_process` | ErrorPanel in the dialog | Troubleshooting / ConnectDevice |
| Library missing or too old | `dependency.jlink_*` | ErrorPanel in the dialog | FirstRunSetup |
| Update started but failed / probe still differs | `probe.firmware_update_failed` (exists) | ErrorPanel in the dialog | Troubleshooting `probe-firmware` |
| Tab busy / MCP / other update | (not an error) | disabled button + tooltip | — |

## Decisions requiring user confirmation

| Question | Options | Recommendation | Decision (by user) | ADR |
|---|---|---|---|---|
| Update with an open session | disconnect first, existing `updateProbeFirmware()` / pause-resume session | — | **disconnect first; banner "Disconnect and update"** (2026-11-04) | ADR-0246 |
| Translations | cs + de + sk + pl complete | — | **all four** (2026-11-04) | ADR-0248 |
| J-Link FW placement | banner + setup row (0008) + troubleshooting topic (0009) / banner only / + Choose device rows | banner + setup row + troubleshooting topic | **banner + setup row + troubleshooting topic** (decided by Matej Strnad 2026-11-04) | ADR-0255 |
| After a successful update from a banner | tab reconnects / stays disconnected | reconnect | **reconnect** (decided by Matej Strnad 2026-11-04) | ADR-0256 |
| Threading and exclusivity | owned worker per job + registry reservation + scanner suspension / `DiagWorker` request / probe-only session | owned worker + reservation + suspension | **owned worker + reservation + suspension** (decided by Matej Strnad 2026-11-04) | ADR-0257 |
| Update fails while a quit is pending (review 2026-11-04) | cancel the quit and show the error / quit anyway | — | **cancel the quit, show `probe.firmware_update_failed` with its advice; the app stays open** (decided by Matej Strnad 2026-11-04) | ADR-0261 |
| Probe firmware status `Unknown` (review 2026-11-04) | explicit "Update anyway?" + success by the vendor call's return value / as Outdated | — | **confirm dialog says "The J-Link firmware status could not be determined. Update anyway?"; success judged by the return value of `JLINKARM_UpdateFirmwareIfNewer`, not by the status after** (decided by Matej Strnad 2026-11-04) | ADR-0261 |
| What "an AI assistant holds the probe" means (integration with 0014, 2026-11-05) | also an idle connected AI tab and `send_command` / only a running AI operation | — | **only a running AI operation (busy page `"ai"`); an idle AI tab is disconnected by the banner like a user's tab** (decided by Matej Strnad 2026-11-05) | ADR-0271 |
| Kept from ADR-0132 / 0017 (accepted 2026-10-19) | "Not now" per serial until restart; no downgrade for `Newer`; blocked while MCP or an operation runs; no MCP tool | — | accepted (2026-10-19) | ADR-0132 |

## Implementation steps

- [x] P0 Prerequisite: plan Approved; ADR-0255..0257 answered
- [x] P1 `libs/device`: registry-free `updateProbeFirmware` / `inspectProbe` overloads, `SessionRegistry::reserve()`;
  firmware status in the `probe.firmware_outdated` warning event; GTest on the fake J-Link (success, failure, unplug
  simulated, reserved serial refused by `checkAvailable` / connect)
- [x] P2 `ProbeScanner::suspend()` + GTest (no scan while suspended; resumes)
- [x] P3 `ProbeFirmwareWorker`, `ProbeFirmwareViewModel`, `ConnectionViewModel` additions + GTest (state machine,
  blocking reasons, Not now bound, reconnect after success, no reconnect after failure / cancel)
- [x] P4 QML: banner in `PageHost.qml`, `ProbeFirmwareDialog.qml`; Qt Quick tests (light/dark, keyboard, Esc, 960 × 600
  in DE and CS)
- [ ] P5 Hooks: setup wizard row and troubleshooting step (only when 0008 / 0009 are registered; otherwise done when
  they land, tracked in their checklists) — after 0035/0008/0009: the reusable action is
  `ProbeFirmwareViewModel::requestForSerial()` / `blockedReasonFor()` (`AppViewModel.probeFirmware`); the MCP
  "AI assistant" check plugs in through `ProbeFirmwareViewModel::setAiHoldsProbe()` (plan 0014).
  - [x] MCP hook (integration of 0014 + 0036, 2026-11-04): `McpController` installs
    `GuiDeviceProvider::aiOperationRuns(sn)` (a J-Link tab of the probe with busy page `"ai"`) — "Used by an AI
    assistant" while an AI operation runs, also on the banner's own tab; the `ProbeFirmwareViewModel` is now
    created before the extensions are registered. Reverse: MCP `connect` of a reserved probe gets the registry's
    `probe.open_failed` ("The firmware of the J-Link %1 is being updated.") for a new AI tab (closed again) and a
    user's tab. GTest `McpProvider.AiOperationBlocksTheFirmwareUpdate` / `ReservedProbeRefusesConnect`. An idle
    AI-opened tab and a running `send_command` (no busy page) do not block; the banner disconnects an idle AI tab
    like a user's tab (ADR-0271, decided 2026-11-05).
  - [ ] Follow-up (verification 2026-11-05, not caused by the integration): two tests fail under heavy load (TSan,
    load average ~25 on 16 cores) and pass at normal load — `OwnerThread.ThirtyTwoWaitsOnAFastStreamDoNotStall`
    (`libs/mcp_tools/tests/owner_thread_test.cpp`: the `received > 25000` count has no load skip, only the latency
    check has) and `StdinLineSource.DeliversLinesOverlongMarkersAndTheEnd` (`app/cli/tests/mcp_command_test.cpp`:
    the reader thread starts in the constructor and may deliver every line before `setWakeup()`, so 0 wakeups).
    Make both robust (plan 0014 / 0013 owners).
  When the
  troubleshooting wizard is registered, the failure panel's [Close] in `ProbeFirmwareDialog.qml` (the ErrorPanel's
  `customActionText`, ~line 166) must give way to "Help me fix this" (and the dialog close before the wizard opens)
- [x] P6 Docs: `troubleshooting.md` § `probe.firmware_outdated` / `probe.firmware_update_failed` mention the app action
  in en, cs, de, sk, pl; `update_translations`, cs + de + sk + pl 0 unfinished
- [ ] P7 Verification below; 0017 parity row "J-Link firmware outdated → offer update" ticked with evidence

## Tests and verification

- **GTest (fake J-Link library):** overloads behave as the registry versions; reservation blocks connect and a second
  reservation; worker delivers exactly one result; view model: Outdated banner → disconnect → Confirm → Updating →
  success toast + reconnect; failure → `probe.firmware_update_failed`; cancel at Confirm; blocked while busy;
  `before == Current` path; quit while updating waits for the job.
- **Qt Quick Tests:** banner buttons and tooltips; dialog keyboard-only; Esc disabled while Updating; DE/CS 960 × 600.
- **Sanitizers:** ASan/UBSan suite; TSan: worker result racing dialog close, tab close during the update (the
  reservation outlives the tab), scanner suspension racing a running scan.
- **clang-tidy / clang-format / qmllint:** zero findings.
- **HIL / manual (by the user) on Linux, macOS, Windows** with a J-Link on outdated firmware (keep one probe for
  this, 0017 risk): banner → update → console back; unplug during the update → error panel → recovery via J-Link
  Commander; J-Link Commander open → `probe.open_failed`; setup wizard row; troubleshooting topic.

## Risks and open points

- **Test probe:** an outdated probe is hard to keep (no downgrade); fake-only coverage for some branches.
- **Quit during an update** must wait; a forced kill can brick the probe (J-Link Commander repairs it) — documented in
  the error's "What to do".
- **CLI JSON change (2026-11-04):** a probe that does not come back after the vendor update call is now
  `probe.firmware_update_failed` instead of `probe.not_found` / `probe.open_failed` (ADR-0132); `hwio jlink
  update-firmware --json` reports that `code` (the exit code stays 5). An `Unknown` probe is judged by the vendor
  call's return value (ADR-0261).
- **HIL: slow USB re-enumeration.** The verify step reopens the probe right after the update; a probe that takes
  longer to re-enumerate (hubs, Windows driver load) would be reported as a failed update although it was updated.
  Check on all three OSes; if seen, the reopen needs a bounded retry.
- **MCP blocking** is effective only once the GUI MCP server (plan 0014) exists; until then an MCP stdio process is
  caught by the `ProbeLock` (`probe.in_use_by_other_process`). Wired on 2026-11-04 (see P5); only a running AI operation blocks (ADR-0271).
