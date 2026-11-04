# ADR-0209 — AI-made translations ship before a native-speaker review

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0030)

## Context

The German, Slovak and Polish catalogs of plan 0030 are translated by an AI agent (Czech catalog as a guide, a
shared glossary). Nobody on the team is a native speaker of these languages. They could be hidden until reviewed,
offered as "preview", or offered as normal languages right away.

## Options considered

1. Offer them only after a native-speaker review (hidden until then).
2. Offer them right away as normal languages; a native-speaker review follows, corrections in later releases.
3. Offer them right away, marked as preview and not chosen automatically from the system language.

## Decision

Option 2. de, sk and pl are ordinary entries of `supportedLanguages` from the first release that contains them.
A native-speaker review (e.g. by distribution partners or customers) is an open item of plan 0030; reported
mistakes are fixed in the `.ts` files like any other bug. `i18n/GLOSSARY.md` records the chosen terminology so
reviewers and later translations stay consistent.

## Consequences

- Users in DE/SK/PL get a localized GUI immediately; some wording may be unidiomatic until reviewed.
- No "reviewed" flag or preview label in the code; nothing to remove later.
- The review is tracked outside the code (plan 0030 risks / follow-up), not enforced by CI.
