# ADR-0259 — Language-independent docs anchors; untagged builds link to `main`

- **Status:** Accepted (plan 0037)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

ADR-0247 builds links `…/blob/v<appVersion>/docs/<lang>/<page>.md#<anchor>`. GitHub derives heading anchors from the
heading text, which is translated (`#no-j-link-found` vs `#nenalezen-žádný-j-link`); the error-code entries already use
explicit `<a id="…">` anchors that are the same in every language. Development and CI builds have versions like
`2.0.0-dev+abc1234`, for which no tag exists.

## Options considered

Anchors:
1. **Explicit `<a id="…">` anchors with English kebab ids before every linked section, identical in all five
   languages; the table has one anchor per topic (chosen).**
2. A per-language anchor table (one GitHub slug per language and topic).

Untagged builds:
1. **Versions with build metadata (`+…`) or a `-dev` pre-release link to `main`; release and RC versions to
   `v<version>` (chosen).**
2. Always the tag (dead links in dev builds).

## Decision

Option 1 for both (accepted by Matej Strnad, 2026-11-04).

## Consequences

- Plan 0037 adds about 15 `<a id>` lines per language; the anchor test reads `docs/<lang>/*.md`.
- Translators must keep the `<a id>` lines unchanged (rule added to `docs/README.md`).
