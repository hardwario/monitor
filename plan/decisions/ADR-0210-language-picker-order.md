# ADR-0210 — Language picker order and system-language resolution

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0030)

## Context

With five languages the Settings → General language combo box needs an order and labels, and the `system` setting
must decide whether the new languages are picked from the operating-system language.

## Options considered

1. Native names; English (source) and Czech first, then the others alphabetically by code; `system` resolves to any
   supported language.
2. All native names alphabetically.
3. Native name plus English name ("Deutsch (German)").

## Decision

Option 1. `hwio::i18n::supportedLanguages` (the single table, ADR-0027) is ordered `en, cs`, then further
languages alphabetically by ISO 639-1 code: `de, pl, sk`. The picker shows "System" followed by the native names in
table order: English, Čeština, Deutsch, Polski, Slovenčina. `ui/language = system` picks the first supported
language of `QLocale::system().uiLanguages()`, so a German, Polish or Slovak system gets its language
automatically (fallback English, unchanged).

## Consequences

- No code change in `LanguageManager` or the picker; only table entries.
- Users of a de/pl/sk system see the change of language after updating, unless they chose a language explicitly.
