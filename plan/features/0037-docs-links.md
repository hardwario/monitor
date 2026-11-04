# 0037 — Documentation links: per-topic, per-language table to GitHub at the release tag

- **Status:** Approved (2026-11-04 by Matej Strnad)
- **Milestone:** 2.0.0 (takes over 0017 § E2 "links from About, wizards and error panels")
- **Author:** Matej Strnad (with Claude), 2026-11-04
- **Related ADRs:** ADR-0247 (accepted 2026-11-04: GitHub at the release tag), ADR-0248 (translations), ADR-0258,
  ADR-0259 (accepted 2026-11-04); ADR-0138, ADR-0184, ADR-0203, ADR-0220 (in-app part superseded), ADR-0221, ADR-0222
- **Depends on:** [0029](0029-user-documentation.md) (`docs/<lang>/` pages, Done), plan 0030 (de / sk / pl UI
  languages, Draft on `strnad/i18n-de-sk-pl-e2ab03`; this plan works with whatever languages the `LanguageManager` offers)
- **Used by:** [0035](0035-wizards-rebaseline-2-0-0.md) (wizard steps, `bluetooth` topic), [0036](0036-jlink-firmware-update-gui.md)

## Context

`libs/core/src/docs_urls.cpp` (plan 0028 D18, ADR-0203) has two topics (`Monitor`, `ConsoleBatch`), both returning the
docs.hardwario.com HARDWARIO Monitor page and ignoring the language (ADR-0220). Callers:

- `AppViewModel::documentationUrl()` (`app/gui/src/viewmodels/app_view_model.cpp` ~line 1148) hard-codes `u"en"`;
- `ConsoleViewModel::openBatchDocs()` (`console_view_model.cpp` ~line 753) passes `AppViewModel::language()`, which
  returns the *setting* — `"system"` by default, not a language code (bug: it must use `effectiveLanguage()`);
- `FirmwareSourceViewModel::docsUrl()` uses the device profile's `firmwareDocsUrl` (CHESTER catalog pages on
  docs.hardwario.com, en/cs) — not part of this table.

The user decided (ADR-0247) that in-app links go to
`https://github.com/hardwario/monitor/blob/v<appVersion>/docs/<lang>/<page>.md#<anchor>` with English fallback, from one
table that can later switch to docs.hardwario.com. The docs have an `<a id="<error id>">` anchor for every error code in
all five `troubleshooting.md` files (89 each); section anchors are GitHub heading slugs, which differ per language.

## Goals / Non-goals

**Goals**
- Topic table in `docs_urls.cpp`: page + anchor per topic, language per ADR-0221 (user pages in en/cs/de/sk/pl,
  reference pages en only), ref per ADR-0247 / ADR-0259.
- Every caller passes the real UI language (`effectiveLanguage()`).
- New topics for Help, wizards (0035), ErrorPanel "Learn more" per error id (ADR-0258, accepted).
- CTest: every topic anchor and every error id exists in each language's page.

**Non-goals**
- Publishing on docs.hardwario.com (open question 29, later; only the table changes then).
- The device profile's firmware docs links (stay en/cs, decision row).
- CLI links (the CLI prints no URLs today; unchanged).

## User experience

- Help → **Documentation ↗** opens `docs/<lang>/index.md` of the installed version.
- Console → ⓘ next to Batch opens `console.md#batch-files` in the UI language.
- **ErrorPanel** (and WarningBanner details): a link **Learn more ↗** after "What to do" opens
  `troubleshooting.md#<error id>` (ADR-0258, accepted). Not shown for errors without an id in the docs (none today;
  enforced by the test).
- Wizard steps (0035) may show a link from the table (e.g. setup J-Link step → `install.md#segger-j-link-software`,
  troubleshooting `bluetooth` → `troubleshooting.md#bluetooth-device-not-found-or-pairing-fails`). These are constant
  table URLs and satisfy ADR-0184's "linkUrl is a constant URL, never runtime data" (the version and language are app
  constants, not device or user data).
- Settings → About: "Documentation ↗" (same as Help).

## CLI

None.

## Design

### Table (`libs/core/include/hwio/core/docs_urls.h`, `libs/core/src/docs_urls.cpp`)

```cpp
namespace hwio {
enum class DocsTopic : std::uint8_t {
    Monitor,            // index.md
    Install,            // install.md
    InstallJLink,       // install.md#segger-j-link-software
    InstallUsbLinux,    // install.md#usb-access-to-the-j-link
    InstallNrfUtil,     // install.md#nordic-nrf-util-lte-modem-firmware-only
    GettingStarted,     // getting-started.md
    Console,            // console.md
    ConsoleBatch,       // console.md#batch-files
    Flashing,           // flashing.md
    FlashingLte,        // flashing.md#lte-firmware-update
    FlashingRecover,    // flashing.md#recover
    Bluetooth,          // bluetooth.md
    BluetoothPairing,   // bluetooth.md#pairing
    Troubleshooting,    // troubleshooting.md
    TroubleNoProbe,     // troubleshooting.md#no-j-link-found  (one per "Common problems" section used by 0035 topics)
    TroubleProbeBusy, TroubleNoPower, TroubleNoConsole, TroubleFlashing, TroubleOtherConnector, TroubleNrfUtil,
    TroubleBluetooth,
    Privacy,            // privacy.md
    PrivacyReport,      // privacy.md#diagnostic-report
    Cli,                // cli.md            (English only)
    CliMigration,       // cli-migration.md  (English only)
    Mcp,                // mcp.md            (English only)
};
struct DocsPage { std::string_view page; std::string_view anchor; bool translated; };
[[nodiscard]] DocsPage docsPage(DocsTopic topic) noexcept;                 // for the anchor test
[[nodiscard]] std::span<const std::string_view> docsLanguages() noexcept;  // {"en","cs","de","sk","pl"}
// `language`: a UI language ("de", "de_DE", "pt"); primary subtag; not in docsLanguages() or page not translated → "en".
// `appVersion`: hwio::buildInfo().version by default; ref rule ADR-0259.
[[nodiscard]] QUrl docsUrl(DocsTopic topic, QStringView language, std::string_view appVersion = buildInfo().version);
[[nodiscard]] QUrl errorDocsUrl(std::string_view errorId, QStringView language,
                                std::string_view appVersion = buildInfo().version);
[[nodiscard]] std::string docsRef(std::string_view appVersion);           // "v2.0.0", "v2.0.0-rc.3", "main"
}
```

- One base constant `https://github.com/hardwario/monitor/blob/` and one path pattern
  `<ref>/docs/<lang>/<page>.md#<anchor>` — switching to docs.hardwario.com later replaces these two and the test
  expectations (ADR-0247).
- **Ref (ADR-0259, accepted):** a version with build metadata (`+…`) or a `-dev` pre-release → `main`; otherwise
  `v<version>` (`2.0.0`, `2.0.0-rc.3`, matching the release tags of 0015/0018).
- **Anchors (ADR-0259, accepted):** explicit `<a id="…">` lines with English kebab ids placed before each linked section
  in all five languages, equal to the English GitHub slug (so existing English links keep working), e.g.
  `<a id="no-j-link-found"></a>` before "### Nenalezen žádný J-Link". `docs/README.md` gets the rule "keep `<a id>`
  lines unchanged when translating".
- `libs/core` stays QtCore only (ADR-0004): `QUrl`, `QStringView`.

### Callers (`app/gui`)

- `AppViewModel`: `documentationUrl()` → `docsUrl(Monitor, effectiveLanguage())`; new
  `Q_INVOKABLE QString docsLink(const QString& topicId) const` (QML topic ids `"install-jlink"` …; unknown id → index)
  and `Q_INVOKABLE QString errorDocsLink(const QString& errorId) const`; `openUrl` through the existing
  `SettingsViewModel::openUrl` / `QDesktopServices` path.
- `ConsoleViewModel::openBatchDocs()`: `effectiveLanguage()` (bug fix).
- `ErrorPanel.qml` / `WarningBanner.qml`: "Learn more ↗" bound to `AppViewModel.errorDocsLink(error.id)`; hidden when
  empty or the error is invalid; keyboard reachable; `Accessible.name` "Learn more about this error (opens browser)".
- Wizards: `WizardStep::linkUrl` filled from `docsUrl(...)` in C++ (retranslated on language change).

### Language change

All links are computed on demand (no cached URL), so a live language switch takes effect at the next click.

### Error cases

None new. A link that cannot be opened logs `lcGui` warning (existing behaviour of `openUrl`).

### Bounds

Topic table compile-time (`std::array`, ≤ 64 entries); error ids from `errorTable`.

## Decisions requiring user confirmation

| Question | Options | Recommendation | Decision (by user) | ADR |
|---|---|---|---|---|
| Where in-app docs links go | GitHub at the release tag, per language with en fallback, one table / GitHub `main` / docs.hardwario.com | — | **GitHub at the release tag** (2026-11-04) | ADR-0247 |
| Translations | cs + de + sk + pl complete | — | **all four** (2026-11-04) | ADR-0248 |
| Per-error "Learn more" | add to ErrorPanel + WarningBanner / Help only | add | **add** (decided by Matej Strnad 2026-11-04) | ADR-0258 |
| Anchors across languages; ref for untagged builds | explicit `<a id>` English ids in all languages + `main` for `-dev`/`+meta` / per-language slug table + always tag | explicit ids + `main` fallback | **explicit ids + `main` fallback** (decided by Matej Strnad 2026-11-04) | ADR-0259 |
| Firmware docs links in the device profile | keep en/cs on docs.hardwario.com / move into this table | keep | **keep** (decided by Matej Strnad 2026-11-04) | — |

## Implementation steps

- [ ] L0 Prerequisite: plan Approved; ADR-0258/0259 answered
- [ ] L1 `docs/<lang>/*.md`: `<a id>` anchors for every table topic in en, cs, de, sk, pl; `docs/README.md` rule;
  `getting-started.md` § Help "Documentation ↗" text (GitHub, version) in five languages; `privacy.md` unchanged
  (row "Documentation links … your browser" stays true)
- [ ] L2 `docs_urls.*` table, `docsRef`, `errorDocsUrl` + GTest (`docs_urls_test.cpp` rewritten)
- [ ] L3 Anchor CTest `docs_anchors` (GTest in `libs/core/tests`, `HWIO_DOCS_DIR` compile definition =
  `${PROJECT_SOURCE_DIR}/docs`): for every topic and language the file exists (or the page is English-only and the
  `en` file exists) and contains the anchor (`<a id="x">` or a heading whose GitHub slug is x); every `errorTable` id
  has `<a id="<id>">` in all five `troubleshooting.md`
- [ ] L4 GUI callers (`AppViewModel`, `ConsoleViewModel`), `ErrorPanel` / `WarningBanner` "Learn more" + Qt Quick tests
- [ ] L5 ADR-0220 status line, open question 29 row, `update_translations`; cs + de + sk + pl 0 unfinished
- [ ] L6 Verification below

## Tests and verification

- **GTest:** `docsRef` table (`2.0.0` → `v2.0.0`, `2.0.0-rc.3` → `v2.0.0-rc.3`, `2.0.0-dev+abc` → `main`,
  `2.1.0+g1234` → `main`, empty → `main`); language fallback (`de_AT` → de, `pt` → en, `cli` page in `cs` → en);
  every topic → https, host `github.com`, path `/hardwario/monitor/blob/<ref>/docs/<lang>/<page>.md`; `errorDocsUrl`
  fragment = id.
- **CTest `docs_anchors`:** as L3 (runs in every CI job, no network).
- **Qt Quick Tests:** "Learn more" visible for a valid error, hidden for invalid; keyboard focus order; DE at 960 × 600
  (the link wraps, no clipping).
- **Sanitizers / tidy / format / qmllint:** as usual; zero findings.
- **Manual (by the user, Linux / macOS / Windows):** Help → Documentation in each UI language opens the right page
  of the installed tag; "Learn more" from a real error (`probe.not_found`) jumps to the anchor on GitHub; a dev build
  opens `main`.

## Risks and open points

- **GitHub anchor behaviour:** GitHub prefixes ids with `user-content-` and resolves `#id` links by script; works in
  browsers today. If it ever breaks, only the table changes (and docs.hardwario.com publishing is the long-term fix).
- **Repository rename or private repo** breaks every installed version's links; the base constant is one line, but
  installed versions keep the old URL (GitHub redirects renamed repos).
- **Docs at a tag are frozen:** a docs fix after release reaches users only with the next version (accepted by
  ADR-0247).
