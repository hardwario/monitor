# 0054 — AI consent prompt: firmware by name, clearer rows and buttons

- **Status:** In progress (approved 2026-11-19, Matej Strnad: "Approve as shown" — the mock in User experience;
  D-5 added 2026-11-19)
- **Milestone:** 2.0.0-rc.5
- **Author:** Claude (with Matej Strnad), 2026-11-19
- **Related ADRs:** ADR-0389 (accepted); ADR-0101, ADR-0108 (consent prompt, its own window — unchanged),
  ADR-0241 (client with version), ADR-0161, ADR-0164 (warnings), ADR-0377 (consent info in results, plan 0050),
  ADR-0385 (Recent entries by name, plan 0051)
- **Depends on:** plan 0050 (it changes the consent brokers; this plan is implemented on top of it)

## Context

Reported by the user on 2026-11-19 with a screenshot of the prompt "c wants to flash firmware":

```
HARDWARIO Monitor — Allow AI assistant?
c wants to flash firmware
Client       c 1
Device       CHESTER app · 822005109 (tab 1)
Size         645.47 KiB
Catalog ID   59489e4e91d741da930ab15b1ab9e31f
Target       app
Show device ↗
[Deny]                    [Allow once] [Allow flashing for c]
Denied automatically in 1:56 if not answered.
```

The user asked for two things: the firmware by **name** instead of only the 32-digit ID, and better **layout and
wording**. The prompt staying a separate window and the client's own name ("c") are not part of this plan.

What is hard to read today: the one thing the user approves (which firmware) is a hex ID; "Client c 1" glues the
name and the version; "Target app" is the internal target id; the size stands alone above the ID; the two Allow
buttons do not say that the right one is permanent.

## Goals / Non-goals

**Goals:** the prompt names the firmware as the catalog does; every row is understandable without knowing internal
ids; the permanent choice is recognisable.

**Non-goals:** no change to what needs consent, to the categories, the timeout, the queue, Esc = Deny, the window
(own, frameless, always on top — ADR-0108) or the CLI `--allow` flags. No change to the MCP tool arguments.

## User experience

Proposed prompt for the same request:

```
HARDWARIO Monitor — Allow AI assistant?
c wants to flash firmware

Firmware     hio-chester-clime v3.2.0  (645.47 KiB)
             Catalog ID 59489e4e91d741da930ab15b1ab9e31f          ← small, muted, full ID
Device       CHESTER · J-Link 822005109 (tab 1)   Show device ↗
MCU          Application MCU
Client       c, version 1

[Deny]                    [Allow once] [Always allow flashing for c]
Denied automatically in 1:56 if not answered.
```

- **Firmware row first**, because it is what is being approved. Catalog firmware: `<name> <version>  (<size>)` with
  the full ID on a second, small line (never shortened: it is what the user can compare). When the name is not
  known (the catalog page could not be read and the file came from the cache), the row is as today:
  `Catalog ID <id>  (<size>)`. A file: `File  <path>  (<size>)` as today.
- **Device row** keeps the tab's title; "Show device ↗" moves to the end of this row.
- **MCU** instead of "Target", with the profile's title of the target ("Application MCU", "LTE MCU",
  "LoRaWAN module").
- **Client** last: `<name>, version <version>`; the row is absent without a version, as today.
- **Right button:** "Always allow flashing for c" (and "Always allow erasing / memory writes / UICR writes /
  connecting devices / these for …"). A tooltip: "Until you remove it in Settings › AI assistant."
- Warnings (PIB loss, UICR, memory write) stay below the rows, unchanged.

## CLI

None. (`hwio mcp` over stdio uses `--allow` flags, no prompt.)

## Design

- `libs/mcp_tools/src/tools/firmware_tools.cpp`: *Correction (step 1, 2026-11-19):* the draft said the flash
  operation already reads the catalog page. It does not: `FlashCall` only called
  `tools.catalog()->download(*id, DownloadOptions{})` (no `lookup()`, no `expectedSha256`; firmware_tools.cpp:167 at
  afcbda4), and the consent `details` were built from the downloaded image alone (firmware_tools.cpp:184-213).
  The cache stores no name either (firmware_cache.h:55-60). The CLI (`app/cli/src/flash_commands.cpp:230-245`) and the
  Flash page (`firmware_source_view_model.cpp:511, 650`) do read the page. Decision D-5 (user, 2026-11-19): **like the
  CLI** — for a `catalog_id`, `FlashCall` first starts `catalog->lookup(id)` and, when it finished, the download with
  `expectedSha256` = the page's SHA-256. The lookup runs before the download, not in parallel; `cancelPending()`
  cancels it. An unreadable page (network error, timeout, not a catalog page, HTTP 404) is logged and the flash goes
  on without `expectedSha256` and without the name — the prompt shows the ID row as today. This is identical to the
  CLI, which also continues after every lookup failure except `cancelled` (a 404 is not treated as "this ID does not
  exist" there either; the HEX download then answers `firmware.not_found_in_catalog`). A HEX file without the
  page's hash fails with `firmware.checksum_mismatch` before the consent prompt. The consent `details` gain
  `firmware_name` and `firmware_version` (strings, each at most `maxCatalogFieldBytes` = 128 bytes, from
  `firmware::CatalogInfo`; `parseCatalogPage()` already rejects longer fields) when the page was read; absent
  otherwise. The tool result and the audit log are unchanged (ADR-0377).
- `app/gui/src/mcp/consent_view_model.cpp`: row order and texts as above; the row model gains an optional `note`
  (the small second line) and the device row an `action` flag for the link. *Correction (2026-11-19):* there is no
  profile title of a target (`TargetProfile` has only `displayName` "application" / "LTE", profile.h:65-68), so the
  MCU text comes from one helper in the consent view model, as in the mock: "Application MCU", "LTE MCU", otherwise
  the target id (no chip name; `DeviceDescription` and the describer are unchanged). The view model also bounds and
  flattens the name and version itself (one line of printable text, at most `maxCatalogFieldBytes`, cut with "…"),
  so a value it gets never spans lines or hides the ID.
- `app/gui/qml/mcp/ConsentWindow.qml`: the delegate shows `note` under the value (small, muted,
  `Text.WrapAnywhere`); the link sits in the device row; the tooltip of the right button. The right button wraps its
  text when it does not fit the 480 px window (Czech, long client names) instead of clipping it. The tooltip names
  the real Settings section: "Until you remove it in Settings › AI assistants (MCP)." (the draft said
  "AI assistant"; the section's title is "AI assistants (MCP)").
- Changed strings are translated in cs, de, pl, sk (`update_translations`, glossary).
- The audit log and the MCP tool result are unchanged (plan 0050 decides what they contain).

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| D-1 Firmware row | name + version, full ID small below / name only / ID only (today) | **name + version, full ID small below** (2026-11-19) | ADR-0389 |
| D-2 Row order and labels | as in the mock / keep today's order, only rename | **as in the mock** (2026-11-19) | ADR-0389 |
| D-3 Permanent button | "Always allow flashing for c" + tooltip / keep "Allow flashing for c" | **"Always allow … for <client>" + tooltip** (2026-11-19) | ADR-0389 |
| D-4 Anything else in the layout or wording | — | nothing further (approved as shown, 2026-11-19) | — |
| D-5 Catalog lookup in the AI flash tool | like the CLI (page first, its SHA-256 checks the download, unreadable page → ID only) / no lookup (ID only) | **like the CLI** (2026-11-19) | ADR-0389 |

## Implementation steps

- [x] 1. Verify where the catalog page is read relative to the consent request (it was not: see Design); D-5: the
      lookup before the download like the CLI, the two `details` fields; tests (`CatalogFlash.*`).
- [x] 2. `ConsentViewModel` rows, labels, note, button text; view-model tests for catalog / cached without page /
      file / LTE / erase / connect prompts.
- [x] 3. `ConsentWindow.qml` delegate, link in the device row, tooltip; Qt Quick test (`tst_mcp*.qml`) incl. Czech
      at the window's fixed width 480.
- [x] 4. `update_translations`, translate cs, de, pl, sk (0 unfinished).
- [x] 5. Docs: the prompt's description and screenshot text in `docs/<lang>/mcp.md` where present.
- [x] 6. Build, ctest (debug, asan, tsan), tidy, format, qmllint, `check_translations` — one preset at a time,
      `nice -n 19 ionice -c 3`, `--parallel 5`.
      Verified 2026-11-23: debug 1867/1868 (the one failure is the flaky `StdinLineReader.NotifyAfterEachPushAndBlockingWhenFull`,
      unrelated), qmllint, format, includes, `check_translations` 0 unfinished, tidy 0 findings, ASan 1867/1867, TSan
      1800/1800 with `CatalogFlash.*` left out of the TSan run like the other QNetworkAccessManager tests (approved by
      the user 2026-11-23).
- [ ] 7. Manual with the fake J-Link and a real MCP client: flash by catalog ID, flash a file, erase. Status → Done.

Verification notes (2026-11-23):

- clang-tidy: `ConsentViewModel::details()` had cognitive complexity 29 (limit 25). Split into helpers in the
  anonymous namespace of `consent_view_model.cpp` (`appendWrittenRow`, `deviceRow`, `appendTargetRows`,
  `appendWarningRows`); same rows, same order, same texts. No other tidy finding in the files of this plan.
- `test_consentWindowCatalogCzech` passes (the wrapped remember button and the rows fit 480 px in Czech).
- Results: debug build passed; debug ctest failed 1 of 1868 (`StdinLineReader.NotifyAfterEachPushAndBlockingWhenFull`
  in the untouched `app/cli/tests/terminal_test.cpp`, a newly seen timing flake: it failed 1 of 3 runs alone;
  discounted). Passed: tidy (0 findings), clang-format, `all_qmllint`, `check_translations` (0 unfinished), `check-includes`, ASan
  (1867/1867) passed.
- Open: TSan fails `CatalogFlash.AnUnreadablePageFlashesWithTheIdOnly` and `CatalogFlash.ACancelDuringTheLookupEndsTheCall`
  (1802/1804): a "data race" between Qt allocating inside `HttpFetch` (`FirmwareCatalog::lookup`, main thread) and
  `QCoreApplicationPrivate::sendPostedEvents` deleting on Qt's `QNetworkAccessManager` thread — the uninstrumented-Qt
  report for which the project leaves every QNetworkAccessManager test out of TSan (`libs/firmware/tests`,
  `JLinkTools.FlashFromTheCatalog` in `libs/mcp_tools/tests/CMakeLists.txt`). Adding `CatalogFlash.*` to that TSan
  filter needs the user's decision; step 6 stays open until then.

## Tests and verification

As in the steps; the security property to keep under test: the full catalog ID and the full file path are always
shown, a firmware name can never replace or hide them, and a name longer than the limit is cut with "…".

## Risks and open points

- The firmware name comes from a web page; it is shown as plain text (`Text.PlainText` stays) and is bounded.
- Plan 0050 is uncommitted in the main checkout and touches the same files; this plan is implemented after it.
- The LoRaWAN module target of plan 0040 (another branch) needs its title "LoRaWAN module" added to the MCU helper in
  `consent_view_model.cpp` when the branches meet; until then an unknown target shows its id.
- D-5 adds one request to the catalog page before every AI flash by catalog ID (at most `lookupTimeout`, 15 s,
  when the server does not answer); offline the flash goes on with the ID only.
- Docs: only `docs/en/mcp.md` exists (no other language has an MCP page), and it does not describe the GUI prompt;
  its `flash` row now mentions the SHA-256 check.
