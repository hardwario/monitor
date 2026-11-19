# 0050 — MCP fixes from AI-client testing: device list, English texts, reacquire, unchanged flash, consent info

- **Status:** In progress (approved 2026-11-13, Matej Strnad)
- **Milestone:** 2.0.0-rc.5
- **Author:** Claude (with Matej Strnad)
- **Related ADRs:** ADR-0373 (list_devices lists BLE devices like `ble scan`), ADR-0374 (MCP texts are English,
  also errors and console markers; supersedes ADR-0240), ADR-0375 (reacquire_probe waits for the console),
  ADR-0376 (flash reports unchanged contents), ADR-0377 (consent-gated results say how they were allowed);
  ADR-0099 (tool set), ADR-0101 (consent), ADR-0103 (MCP texts English), ADR-0027 (localization)
- **Depends on:** plan 0013 (MCP core), plan 0014 (GUI MCP and consent)

## Context

On 2026-11-13 Claude tested the MCP server of the installed 2.0.0-rc.4 as an AI client (GUI endpoint, Czech GUI,
CHESTER-M on J-Link 801047277; the register bug found there is plan 0048). Five things made the server harder to
use or to trust:

1. **`list_devices` is ~25 KB.** It lists every BLE device the GUI has seen nearby (≈140 `MP1_*` advertisers with
   `profile: null`) next to the one open CHESTER. It is the first call every client makes. `hwio ble scan` shows
   only devices with a known profile unless `--all` is given.
2. **Czech text in MCP results.** (a) `hwio::Error` message, cause and suggestion come in the GUI language, by
   ADR-0240 (`target.not_halted: Procesor běží. — Nejprve zastavte jádro.`). (b) GUI console markers are written
   into the shared `ConsoleLog` already translated (`DeviceTabViewModel::addMarker(tr("— resumed —"))`,
   `app/gui/src/viewmodels/device_tab_view_model.cpp`), so `read_log` / `read_terminal` return `— obnoveno —`;
   that contradicts ADR-0103 (results English) and is a bug.
3. **`reacquire_probe` returns `"state":"stopped"`.** `session_tools.cpp` returns `connectionResult()` right after
   `reacquireProbe()` queued the RTT restart; a second later `status` says connected.
4. **`flash` does not say it skipped programming.** Two flashes of an image identical to the flash contents took
   1.2 s; J-Link logged `Flash download: Bank 0 @ 0x00000000: Skipped. Contents already match`, our result said
   `status: ok, verified: true` and our log said `Programmed 679936 bytes`. (The 14 s erase of scope `code` is the
   nRF52840 page-erase time, not a bug.)
5. **Consent-gated results do not say how they were allowed.** After "Allow for this client" in the GUI prompt,
   later flashes ran without a dialog; the client cannot tell a prompt from a remembered answer or the CLI's
   `--allow`.

## Goals / Non-goals

Goals:
- `list_devices` is small by default and can still list everything.
- Everything an MCP client receives is English (ADR-0103), including `hwio::Error` texts and console markers; the
  GUI keeps showing its language.
- `reacquire_probe` returns the state after the console is back (bounded wait).
- `flash` (and `write_flash`, `flash_lte` application hex, `erase` scope code that uses the download path) report
  whether J-Link programmed or skipped, when J-Link says so.
- Consent-gated tool results carry how each category was allowed.

Non-goals:
- No change of the GUI flash page (it could show "already up to date" later), of the remote support session
  texts, or of console files' existing content.
- No change of the erase method (page erase keeps the UICR, ADR-0366 context).
- CLI human texts stay English as today; no new GUI strings except where noted.

## User experience

GUI: unchanged, except that the consent prompt answer is now also reported to the client (no visible change).
MCP (all English):

```jsonc
// list_devices (default)
{"devices":[{"id":"jlink:801047277", ...}, {"id":"ble:F1:22:..","profile":"chester", ...}],
 "ble_hidden": 139}                       // count of BLE devices left out; 0 / absent with all_ble=true
// read_log marker line
{"dir":"marker","text":"— resumed —", ...}
// error
"target.not_halted: The CPU is running. — Halt the core first."
// reacquire_probe
{"connection":{"state":"connected", ...}, "waited": 0.8}
// flash
{"status":"ok","verified":true,"unchanged":true,"affected_bytes":0,
 "consent":{"flash":"remembered"}, ...}
```

## CLI

- `hwio mcp` (stdio) gets the same tool changes; consent there is always `"allowed_by_flag"`.
- `hwio chester app flash` / `hwio ... flash` `--json`: new optional `unchanged` (bool) and `affected_bytes`
  (integer) fields, only when J-Link reported them. Human output: one extra line `Flash contents already matched;
  nothing was programmed.` when `unchanged` is true. Exit codes unchanged.
- No other CLI changes (the CLI is English already).

## Design

### 1. `list_devices` filter (ADR-0373) — `libs/mcp_tools/src/tools/discovery_tools.cpp`

- New optional boolean argument `all_ble` (default false).
- Default: every open/connected device, every J-Link probe, and BLE devices whose `profile` is non-null (the same
  profile match `hwio ble scan` uses, `app/cli/src/ble_commands.cpp` / `hwio::device` profile registry). With
  `all_ble=true`: everything, as today.
- Result gains `ble_hidden` (integer ≥ 0): BLE devices left out by the default filter.
- The `hwio://devices` resource (`resources.cpp`) uses the default filter too (it says "same shape as
  list_devices"). `connect` still accepts any `ble:<id>` from an `all_ble` listing.
- The tool description says so, and that `all_ble=true` lists every nearby advertiser.

### 2. English texts over MCP (ADR-0374, supersedes ADR-0240)

**Errors.** `hwio::Error` (`libs/core/include/hwio/core/error.h`) gains the English source texts:

```cpp
struct Error {
    ErrorCode code;
    std::string message, cause, suggestion;            // translated (UI language), as today
    std::string messageEn, causeEn, suggestionEn;      // English source text, arguments filled in
    std::string detail;                                // not translated, as today
    std::optional<WizardId> wizard;
};
```

- `makeError()` fills the `*En` fields from `DefaultTexts` (the `QT_TRANSLATE_NOOP3` source strings).
- Call sites that override a text (~120 places, e.g. `nrfutil.cpp`, `rtt_channel.cpp`, `jlink_library.cpp`,
  `environment.cpp`; find them with `grep -E '(\.message|\.cause|\.suggestion) = '`) use one new helper in
  `hwio_core` that sets both texts from the same source and arguments, e.g.

  ```cpp
  // Sets error.<field> to the translation and error.<field>En to the English source, both with
  // QString::arg() applied to `args` in order.
  void setText(Error& error, ErrorText field, const char* context, const char* source,
               std::initializer_list<QString> args = {}, const char* disambiguation = nullptr);
  ```

  so the translation catalog (`lupdate`) still sees the literal. A unit test checks that no `Error` created
  through the public API has an empty `*En` field where the translated one is non-empty.
- If an argument itself is a translated text (e.g. `ConnectionViewModel::shortCause(error)`), the English variant
  uses the English counterpart (`messageEn`).
- `hwio_mcp` `toolErrorResult()` (`libs/mcp/src/tool_call.cpp`) and every MCP place that renders an `Error` use the
  `*En` fields. GUI, log file and CLI rendering are unchanged (the CLI has no translations, so both are English).
- Serialization: wherever `Error` is serialized for IPC (remote session, `gui.sock`) the `*En` fields travel too,
  when the format already carries the texts; otherwise unchanged (list the places in the implementation; any
  format change is a stop-and-ask).

**Console markers.** `ConsoleLine` (`libs/device/include/hwio/device/console_line.h`) gains
`std::string textEn;` used only for `Direction::Marker` lines (empty = same as `text`).
`ConsoleLog::appendMarker(Channel, std::string text, std::string textEn = {})`.
`DeviceTabViewModel::addMarker()` passes both (a small helper that translates and keeps the source, the same
`arg()` values). MCP `read_log`, `read_terminal`, `wait_for_state` (`last_lines`) and `send_command` output use
`textEn` when non-empty. GUI display, console files and the remote session keep `text` (non-goal).
Markers created by MCP itself (`— AI (client): tool —`) are English already.

### 3. `reacquire_probe` waits (ADR-0375) — `libs/mcp_tools/src/tools/session_tools.cpp`

- Becomes an asynchronous `ToolCall` (like `wait_for_connection` in `console_tools.cpp`): after
  `reacquireProbe()` succeeds it waits until the console state is `connected` or a terminal error state, at most
  `Limits::reacquireWait = 10 s` (new, `libs/mcp/include/hwio/mcp/limits.h`). Never blocks the owner thread.
- Result: `connection` (as today) plus `waited` (seconds, 0.1 resolution). On timeout it is **not** an error: the
  current state is returned and the description tells the client to call `wait_for_connection`.
- Cancellation (client gone) stops the wait; the reacquire itself is not undone.

### 4. Unchanged flash (ADR-0376) — `libs/hw/jlink`, `libs/device`, `libs/mcp_tools`, `app/cli`

- `JLinkProgrammer::download()` (`programmer.cpp`) already flushes the library's messages around
  `JLINKARM_EndDownload`. It collects the messages of that download (bounded: at most 64 lines kept) and parses
  per bank:
  - `Flash download: Bank <n> @ 0x<addr>: Skipped. Contents already match` → bank unchanged, 0 bytes;
  - `Flash download: Bank <n> @ 0x<addr>: <k> range(s) affected (<bytes> bytes)` → bank changed, `bytes`.
  The patterns live in one place with a comment citing the J-Link V9.84 wording and this plan.
- New result type `DownloadReport { std::optional<std::uint64_t> affectedBytes; std::optional<bool> unchanged; }`
  returned by `program()` / `writeFlash()` paths (and passed up through the `libs/device` operation result).
  `unchanged = true` only if every bank line said Skipped; any bank with bytes → `false`; no recognised line → both
  `nullopt` (never guessed).
- The `Programmed N bytes in M segment(s)` log line becomes `Programmed N bytes …` / `Flash contents already
  matched; nothing programmed (N bytes in M segment(s))`.
- MCP `flash`, `write_flash`, `flash_lte` (application hex only) and `erase` (when it uses the download path) add
  `unchanged` / `affected_bytes` to their result and output schema when known. CLI flash `--json` and human output
  as in "CLI".

### 5. Consent info in results (ADR-0377) — `libs/mcp`, `app/gui/src/mcp`, `app/cli`

- `ConsentDecision` becomes `hwio::Result<ConsentGrant>` with
  `enum class ConsentGrant : std::uint8_t { Prompted, Remembered, Flag };`
  (`libs/mcp/include/hwio/mcp/consent.h`). GUI broker: AllowOnce / AllowForClient at the prompt → `Prompted`; an
  earlier AllowForClient answer → `Remembered`. CLI `FlagConsentBroker` → `Flag`.
- `ToolCall::requestConsent()` records the grant per category; the server adds
  `"consent": {"<category id>": "prompted" | "remembered" | "allowed_by_flag"}` to the success result (and output
  schema) of every tool with consent categories, including categories requested at run time (`uicr`). The
  `connect` category is included when it was requested (GUI).
- The existing safety net (success refused when a static category was not granted) is unchanged.

### Errors

No new error codes. Existing ones unchanged.

### Limits

`Limits::reacquireWait = 10 s`; download message collection ≤ 64 lines; `list_devices` unchanged maximum.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| What `list_devices` lists by default | Like `ble scan` (profiled BLE, `all_ble`) / no BLE unless asked / all with a cap | **Like `ble scan`** (2026-11-13) | ADR-0373 |
| Language of MCP error texts | English everywhere (supersede ADR-0240) / keep ADR-0240 / English default per code | **English everywhere** (2026-11-13) | ADR-0374 |
| `reacquire_probe` result | Wait for the console (bounded) / return at once as "starting" | **Wait for the console** (2026-11-13) | ADR-0375 |
| How flash learns "unchanged" | Parse J-Link log lines / measure EndDownload / own compare | **Parse the J-Link log line** (2026-11-13) | ADR-0376 |
| Consent info in results | Add `consent` field / no change | **Add `consent` field** (2026-11-13) | ADR-0377 |
| Split | One plan / two plans | **One plan** (2026-11-13) | — |
| Branch | New from `origin/main` / on top of plan 0048 | **New branch `strnad/mcp-client-fixes` from `origin/main`** (2026-11-13) | — |

## Implementation steps

- [x] 1. `list_devices` / `hwio://devices` filter, `all_ble`, `ble_hidden`, description; tests.
- [x] 2. `Error` `*En` fields, `makeError()`, `setText()` helper; convert every text-overriding call site; MCP
      rendering uses `*En`; tests incl. the "no empty English text" test.
- [x] 3. `ConsoleLine::textEn`, `appendMarker()` overload, GUI `addMarker()` passes both; MCP readers use `textEn`.
- [x] 4. `reacquire_probe` asynchronous with bounded wait, `waited`, `Limits::reacquireWait`; tests (fake clock).
- [x] 5. J-Link download message parsing, `DownloadReport` through `libs/device`; MCP and CLI fields; log line.
- [x] 6. `ConsentGrant`, brokers, `consent` result field and output schemas; tests for prompted / remembered / flag.
- [x] 7. `update_translations` (new/changed strings), translate every `i18n/hwio_<lang>.ts` (0 unfinished).
- [x] 8. Build, ctest (debug, asan, tsan), tidy, format, qmllint, check_translations — all with
      `nice -n 19 ionice -c 3`.
- [ ] 9. Manual / HIL checks (below); status → Done.

## Tests and verification

Unit / integration:
- discovery tools: 1 open J-Link, 1 profiled BLE, 50 unprofiled BLE → default lists 2 + `ble_hidden: 50`;
  `all_ble` lists 52; resource matches the default.
- core: `makeError` for every `ErrorCode` has non-empty `*En` equal to the source text; `setText` with arguments in
  a test translator (pseudo-translation) gives translated `message` and English `messageEn`.
- mcp: `toolErrorResult` with a translated error renders the English text; with an installed pseudo-translator a
  tool error over the GUI provider is English.
- console: a marker appended with a translation is returned English by `read_log` / `read_terminal` /
  `wait_for_state`, while `text` (GUI) stays translated.
- reacquire: fake console becomes connected after 0.5 s → result `connected`, `waited ≈ 0.5`; never connects →
  returns after 10 s (fake clock) with the current state, no error; client cancel stops the wait.
- J-Link: fake library emits "Skipped. Contents already match" → `unchanged: true, affected_bytes: 0`;
  "1 range affected (688128 bytes)" → `false, 688128`; two banks mixed → `false`, sum; no message → fields absent.
  Extend `tests/fake_jlink` to emit these log lines (scenario option).
- consent: GUI broker AllowOnce → `prompted`; second call after AllowForClient → `remembered`; CLI `--allow flash`
  → `allowed_by_flag`; `uicr` at run time appears in `consent`.
- CLI e2e: flash `--json` with the fake library shows `unchanged` / `affected_bytes`.

Static: linux-tidy zero findings, clang-format clean, qmllint, `check_translations` 0 unfinished.

Manual / HIL (CHESTER-M, J-Link V9.84, GUI in Czech), each destructive step needs the user's go-ahead in chat
and their approval in the GUI prompt:
- `list_devices` in the office: small result, `ble_hidden` > 0; `all_ble=true` lists them.
- `read_registers` while running → English `target.not_halted` message; `read_log` shows `— resumed —` after a
  flash although the GUI shows `— obnoveno —`.
- `release_probe` → `reacquire_probe` → `state: connected`.
- `flash` the device's current backup twice → second result `unchanged: true`; flash a different image →
  `unchanged: false`, `affected_bytes` > 0; the log line wording matches.
- First flash prompted (`consent.flash: prompted`), after "Allow for this client" the next one is `remembered`;
  `hwio mcp --allow flash` → `allowed_by_flag`.

## Risks and open points

- Step 2 touches ~120 call sites in many libs; it is mechanical but large. If a call site builds its text in a way
  the helper cannot express (e.g. concatenated translated fragments), stop and list them for the user rather than
  inventing a pattern.
- The J-Link messages are not a documented API; another J-Link version may word them differently. Then the fields
  are omitted (never wrong), and a test pins the V9.84 wording.
- Remote support session (plan 0044) and console files keep translated marker text; a later plan may switch them.
- `ble_hidden` depends on the scan cache; the number changes between calls.

## Implementation notes (2026-11-13)

- **Error texts.** `hwio/core/localized_text.h` (hwio_core): `setText(error, field, context, source, args,
  disambiguation)` as planned (call sites keep the literal in `QT_TRANSLATE_NOOP(context, ...)` so the
  catalog entries are unchanged — `update_translations` found 0 new strings), plus `TextArg` (a value, or a
  `LocalizedText` {translated, english} for translated arguments), `localizedText()`, `setText(error, field,
  LocalizedText)`, `setUntranslatedText()` (CLI / MCP English-only call sites), `clearText()`, `textOf()`.
  Arguments are filled by one multi-argument `QString::arg()` (review R13). `toJson(error,
  ErrorLanguage::English)` is used by every MCP renderer (`toolErrorResult`, resource / prompt read errors,
  `connection.error` of tools); an empty English text falls back to the translated one.
- Translated arguments with English counterparts: `targetDisplayNameText()` (libs/device), check titles
  (`localizedText("hwio::diag", titleSourceText)`), `textOf(error, Message)` (remote "Reported by the
  customer's Monitor"), GUI wrappers (`batchError`, `guiError`, `stopBatchWithError`, `invalidOption`)
  now take `LocalizedText`; `ErrorInfo` keeps the English texts (C++ only) for `GuiDeviceProvider`.
- **IPC serialization:** no format carries hwio::Error texts across a process boundary (the remote session
  sends error ids only, `gui.sock` no errors); no format change.
- **Left with the English default text (not converted):** `FirmwareSourceViewModel` appends a translated
  sentence to the suggestion (`+=`, a pattern the helper cannot express): `suggestionEn` stays the default
  English suggestion. The diag-step title in `diag.check_timeout` from a running diagnose
  (`session_worker.cpp`, `DiagnoseStepInfo::title` has no English) is used in both languages.
- **Markers:** GUI markers carry English (`DeviceTabViewModel::markerText`); `shortCauseText()` gives the
  English short cause. Not English yet: the busy text inside `— paused: %1 —` (setBusy has only the
  translated text) and the MCP's own markers (`— AI (...): ... —`, `hwio::mcp` context, translated in the
  GUI), which go through `IDeviceProvider::marker(QString)` / `beginBusy(QString)`; the plan assumed they
  were English. Changing that interface was left for a decision.
- **reacquire_probe:** `ToolLimits::reacquireWait` (tests shrink limits, the project's "fake clock").
  Finishes on `connected`, or on `stopped` after the attempt's `connecting`; returns at once when the
  console is already connected (nothing released). A client that calls it before the release's `stopped`
  arrived gets that immediate answer.
- **Download report:** `IJLinkApi::flushMessagesContaining(marker, maxLines)`; `IProgrammer::program()` /
  `eraseCode()` return `Result<DownloadReport>`; `WorkflowContext::download` → `OperationResult::download`.
  The fake library: chip `downloadReport` (computed V9.84 lines) and `downloadMessages` (given lines).
  CLI JSON uses the plan's `affected_bytes` (other CLI keys are camelCase, e.g. `durationMs`).
- **Consent:** GUI "always allow" rules (Settings) report `remembered` (no own value in ADR-0377). The
  `consent` property is added by the server to the output schema of every tool with static categories.
