# ADR-0208 — Release gate: 0 unfinished entries in every translated language

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0030)

## Context

ADR-0027 made "0 unfinished Czech (`cs`) entries" a release gate. Plan 0030 adds German (`de`), Slovak (`sk`) and
Polish (`pl`). Either every language is held to the same gate, or the new languages are best-effort and untranslated
texts appear in English.

## Options considered

1. 0 unfinished entries in **every** translated catalog before a release (cs, de, sk, pl).
2. Gate on Czech only; de/sk/pl best-effort, unfinished entries only reported.

## Decision

Option 1. A release requires 0 unfinished entries in every `i18n/hwio_<lang>.ts` listed in
`HWIO_TRANSLATED_TS_FILES`. `check_translations` prints one line per language
(`Translations: N unfinished <Language> (<code>) entries`) and a summary line
`Translations: 0 unfinished entries in all languages` that the release gate checks.

## Consequences

- Every change of a user-visible GUI string needs four translations before the next release (CI on ordinary
  commits only reports the counts; it does not fail on unfinished entries).
- A partially translated language can never ship; adding a new language means translating it completely first.
- `release.yml`, `packaging/RELEASE.md` and `packaging/README.md` refer to all languages, not only Czech.
