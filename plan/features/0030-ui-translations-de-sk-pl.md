# 0030 — GUI translations: German, Slovak, Polish

- **Status:** Draft
- **Milestone:** post-RC localization (v2.x)
- **Author:** Matej Strnad (plan drafted with Claude)
- **Related ADRs:** ADR-0027 (localization, extended), ADR-0167 (documentation links in the UI language),
  ADR-0208, ADR-0209, ADR-0210, ADR-0211 (new)
- **Depends on:** `v2-dev` @ 770a51f (MIT licence, all milestones merged). Work branch:
  `strnad/i18n-de-sk-pl-e2ab03`.

## Context

ADR-0027 ships v2.0 in English (source) and Czech and promises that *adding a language = a new `.ts` file + one
table entry*. HARDWARIO customers in Germany, Slovakia and Poland use CHESTER; the GUI (`hwio-monitor`) should be
offered in German (`de`), Slovak (`sk`) and Polish (`pl`). The CLI stays English only (ADR-0027) and MCP texts
stay English (ADR-0103) — nothing changes there.

Current state (checked on 770a51f):

- `i18n/hwio_cs.ts`: 948 messages, 6 of them plural (`numerus="yes"`), 0 unfinished. `i18n/hwio_en.ts`: plural
  forms only.
- `cmake/HWioTranslations.cmake`: `HWIO_TRANSLATED_TS_FILES` = `hwio_cs.ts`; `update_translations`,
  `check_translations` (freshness + "N unfinished Czech (cs) entries"); `.qm` embedded into `hwio_gui` and
  `hwio_core_tests` under `/i18n`.
- `libs/core/include/hwio/core/i18n.h`: `supportedLanguages` = `{en, English}`, `{cs, Čeština}`. The table feeds
  `LanguageManager::isSupported` / `resolveSystemLanguage` (setting `system` picks the first supported language
  from `QLocale::system().uiLanguages()`, fallback `en`) and the language combo box in Settings → General
  (`SettingsViewModel::languages()`: "System" + native names in table order).
- `LanguageManager::installTranslators` loads `hwio_<lang>.qm` plus Qt's `qtbase_<lang>` and
  `qtdeclarative_<lang>` from `QLibraryInfo::TranslationsPath` (missing ones are logged and skipped).
- `cmake/HWioPackaging.cmake`: `HWIO_QT_LOCALES cs`, `HWIO_QT_CATALOGS qtbase qtdeclarative qtconnectivity`;
  `cmake/HWioDeploy.cmake.in` copies the existing `<catalog>_<locale>.qm` files into the package.
- Release gate: `release.yml` greps `Translations: 0 unfinished Czech`; `packaging/RELEASE.md` checklist.
- `tools/translation_lint.py` (CTest `translation_lint`) globs `i18n/*.ts`: placeholders, markup, `&`,
  combining marks, invisible characters — the new files are covered automatically.
- Documentation links (ADR-0167, `docsUrl`, CHESTER profile `firmwareDocs`) exist in `en` and `cs`; other
  languages fall back to English (the first entry) — no change needed.
- Qt 6.8 catalogs available for the new languages: `qtbase_{de,sk,pl}`, `qtdeclarative_{de,sk,pl}`,
  `qtconnectivity_{de,pl}` (no `qtconnectivity_sk`; for comparison there is no `qtdeclarative_cs` either).

## Goals / Non-goals

Goals:
- Full `hwio_de.ts`, `hwio_sk.ts`, `hwio_pl.ts`: every entry translated (including plural forms: de 2, sk 3,
  pl 3), placeholders (`%1`, `%n`, `%L1`) and markup/`&` mnemonics preserved, consistent terminology.
- The three languages selectable in Settings → General and resolved from the system language when `ui/language`
  is `system` (subject to D3).
- Qt's own catalogs for the new languages bundled in every package (D4).
- `check_translations` reports unfinished entries for every language (D5), release gate per D1.
- A terminology glossary (`i18n/GLOSSARY.md`) so future strings and reviewers translate consistently.

Non-goals:
- Translating the CLI, MCP texts, log messages, website or customer documentation (plan 0029).
- Right-to-left or non-Latin scripts; per-language layout tweaks beyond fixing truncation found in the review.
- Translated documentation links (no de/sk/pl docs exist; English fallback per ADR-0167).

## User experience

- Settings → General → Language: "System, English, Čeština, Deutsch, Polski, Slovenčina" (ADR-0210).
  Switching is live as today (ADR-0027).
- A user with a German/Slovak/Polish system language and `ui/language = system` gets the GUI in that language.
- Numbers and dates follow the selected language's `QLocale` (already the case).

## CLI

None (the CLI is English only, ADR-0027).

## Design

1. **Catalogs:** `i18n/hwio_de.ts`, `hwio_sk.ts`, `hwio_pl.ts` created by `lupdate` (`language="de"` etc., so
   lupdate writes the right number of `<numerusform>`s), then filled in. The Czech catalog is the guide for
   meaning and length; translations stay close to the English length (narrow buttons, combo boxes). Terminology
   recorded in `i18n/GLOSSARY.md` (e.g. "flash", "console", "probe", "wizard", "J-Link", "RTT" kept as product
   terms where customary).
2. **CMake:** `HWIO_TRANSLATED_TS_FILES` gets the three files; the header comment of
   `cmake/HWioTranslations.cmake` and its "Adding a language" note updated. `check_translations` script mode
   prints `Translations: N unfinished <Language> (<code>) entries` per translated catalog and the summary
   `Translations: 0 unfinished entries in all languages` (or `... N unfinished entries in total`), which
   `release.yml` greps (ADR-0208).
3. **Language table:** `supportedLanguages` gains `{de, Deutsch}`, `{pl, Polski}`, `{sk, Slovenčina}` in the order
   en, cs, de, pl, sk (ADR-0210). `LanguageManager` unchanged.
4. **Packaging:** `HWIO_QT_LOCALES cs de pl sk` (ADR-0211); `HWioDeploy.cmake.in` already skips missing catalogs.
5. **Docs/process:** ADR-0027 gets an "Amended by" note; `CLAUDE.md` localization row, `packaging/RELEASE.md`,
   `release.yml` gate and `packaging/README.md` updated (ADR-0208).

No new dependencies, threading or data formats; `ui/language` gains three valid values (older versions treat
unknown values as `system`, which they already do).

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D1 | Release gate for de/sk/pl | (a) 0 unfinished in every language before a release; (b) best-effort, only cs is a gate | (a), 2026-11-03 | ADR-0208 |
| D2 | Native-speaker review of AI translations | (a) offer only after review; (b) ship now, review later; (c) ship marked as preview | (b), 2026-11-03 | ADR-0209 |
| D3 | Language picker and `system` resolution | native names, en + cs first then by code / all alphabetical / native + English name | native names; en, cs, then de, pl, sk; `system` auto-selects every language, 2026-11-03 | ADR-0210 |
| D4 | Bundled Qt catalogs | all of `HWIO_QT_CATALOGS` for every language (missing skipped) / qtbase + qtdeclarative only | all, 2026-11-03 | ADR-0211 |
| D5 | `check_translations` output | one line per language / Czech only | one line per language + summary line for the gate, 2026-11-03 | ADR-0208 |

## Implementation steps

- [x] ADRs for D1–D5 accepted (ADR-0208..0211); ADR-0027 marked as amended.
- [ ] `HWIO_TRANSLATED_TS_FILES` += de/sk/pl; `update_translations` creates the three catalogs.
- [ ] `i18n/GLOSSARY.md` (en → cs/de/sk/pl key terms).
- [ ] Translate `hwio_de.ts`, `hwio_sk.ts`, `hwio_pl.ts` (all entries, plurals, no `unfinished`).
- [ ] `supportedLanguages` entries; i18n test checks the new codes; `tst_settings.qml` checks the picker list.
- [ ] Test: every supported language other than `en` has an embedded `:/i18n/hwio_<lang>.qm` that loads and
      translates a known string (guards against a table entry without a catalog).
- [ ] `check_translations` per-language unfinished counts (D5); release gate (D1).
- [ ] `HWIO_QT_LOCALES` (D4); docs (`CLAUDE.md`, `RELEASE.md`, `packaging/README.md`).
- [ ] Verification below passes; commit on the work branch.

## Tests and verification

- `cmake --build --preset linux-debug`, `ctest --preset linux-debug --output-on-failure` (incl. `translation_lint`
  over the new files, `tst_settings`, i18n tests).
- `update_translations` leaves the tree unchanged; `check_translations` passes and reports 0 unfinished for
  cs/de/sk/pl.
- `all_qmllint`, clang-format.
- Manual: start `hwio-monitor`, switch to each new language live; walk the main screens (device tabs, console,
  flash, settings, wizards, about) for truncated or overflowing texts; `LANGUAGE=de` with `ui/language=system`.
- Native-speaker review after release (ADR-0209), tracked as an open point.

## Risks and open points

- AI translations may be wrong in domain terms or tone; they ship before a native-speaker review (ADR-0209),
  the glossary keeps corrections consistent. **Open:** find reviewers for de, sk, pl.
- German texts are ~30 % longer than English: truncation in fixed-width controls; found in the manual walk.
- Every future string change now needs four translations before a release (ADR-0208).
- `LanguageManager` loads `qtbase` + `qtdeclarative` but packaging also ships `qtconnectivity` (pre-existing
  mismatch, out of scope; noted as a follow-up).
