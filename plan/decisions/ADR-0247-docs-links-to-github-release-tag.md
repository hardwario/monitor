# ADR-0247 — In-app documentation links go to GitHub at the release tag

- **Status:** Accepted (supersedes the in-app link part of ADR-0220; decides open question 29 for 2.0.0)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2.0.0 planning, plan 0037)

## Context

ADR-0220 kept `hwio::docsUrl()` on the docs.hardwario.com HARDWARIO Monitor page until the user docs (plan 0029,
`docs/<lang>/`) are published there. That publishing is not planned for 2.0.0 (open question 29), so the app links to
a page that does not describe 2.0.0, while the repository has complete pages in five languages.

## Options considered

1. **GitHub at the release tag, per language with English fallback, one table to switch later (chosen).**
2. GitHub `main` (ADR-0138's original link): pages can describe a newer version than the installed one.
3. Keep docs.hardwario.com (ADR-0220).

## Decision

- URL form: `https://github.com/hardwario/monitor/blob/v<appVersion>/docs/<lang>/<page>.md#<anchor>`.
- `<lang>` = the UI language when `docs/<lang>/<page>.md` exists (ADR-0221), else `en` (the reference pages `cli`,
  `cli-migration`, `mcp` are English only).
- All links are built in one table in `libs/core/src/docs_urls.cpp`; switching to docs.hardwario.com later changes
  only that file (and its test).

## Consequences

- Open question 29 is decided for now; publishing on docs.hardwario.com stays a later decision of the user.
- The docs anchors become a contract tested in CI (plan 0037).
- Builds without a release tag need a fallback ref (plan 0037 decision).
