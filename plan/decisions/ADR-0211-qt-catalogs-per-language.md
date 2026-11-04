# ADR-0211 — Qt translation catalogs bundled for every UI language

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0030)

## Context

Packages ship Qt's own catalogs (`HWIO_QT_CATALOGS`: qtbase, qtdeclarative, qtconnectivity) for `HWIO_QT_LOCALES`
(ADR-0027, M7); until now only `cs`. Qt 6.8 provides qtbase and qtdeclarative for de/sk/pl and qtconnectivity for
de/pl (there is no qtconnectivity_sk, and no qtdeclarative_cs).

## Options considered

1. Every catalog of `HWIO_QT_CATALOGS` for every non-English UI language, skipping missing ones.
2. Only qtbase + qtdeclarative (what `LanguageManager` loads today).

## Decision

Option 1. `HWIO_QT_LOCALES` lists every non-English language of `supportedLanguages` (`cs de pl sk`); the deploy
script keeps skipping catalogs that the Qt installation does not have. `HWIO_QT_CATALOGS` is unchanged.

## Consequences

- Qt's standard texts (file dialogs, context menus of text fields) appear in the UI language.
- A new language must also be added to `HWIO_QT_LOCALES` (the "Adding a language" note says so).
- `LanguageManager` still loads only qtbase and qtdeclarative; qtconnectivity is packaged but unused (pre-existing,
  follow-up outside plan 0030).
