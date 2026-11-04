# ADR-0231 — Release: build and sign in parallel with the CI matrix; signing values as variables or secrets

- **Status:** Accepted (amends ADR-0125 and ADR-0224)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (rc.1 release)

## Context

The release workflow ran the full CI matrix (`tests`, ~1 h) before the build jobs, then signing and
notarization: about 1 h 45 min from the tag to the draft release. The `signing` environment holds
`APPLE_API_KEY`, `APPLE_API_ISSUER` and `AZURE_*` as secrets, while the workflow read them as variables only.

## Decision

- `linux`, `macos` and `windows` need only `gate`; they and the sign jobs run in parallel with `tests`.
  `publish` needs `tests`, so no draft release exists unless the full CI matrix passed.
- The five identifiers are read as `vars.X || secrets.X`, so either place works.

## Consequences

- About 50 min less from the tag to the draft release; a failing test now cancels nothing, it only
  blocks `publish` (the signing time is spent anyway).
- PR #1 was merged for rc.1 before its own CI run finished; the release's `tests` job covers that commit.
