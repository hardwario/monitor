# ADR-0223 — v2 moves to the repository hardwario/monitor

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (RC preparation session)

## Context

v2 was developed on `v2-*` branches of `hardwario/hio-monitor`, whose `main` is the old Qt application. The first
release candidate needs its own repository so that the release environment, tag protection and package signing
(ADR-0120) can be set up by another maintainer. ADR-0206 allowed rewriting the history in the new repository so
that every commit carries the MIT `LICENSE`.

## Options considered

1. **New repository `hardwario/monitor`, v2 history rewritten, default branch `main` (chosen).**
2. New repository with the history unchanged.
3. Keep v2 in `hardwario/hio-monitor`.

## Decision

- `hardwario/monitor` holds v2 only. Its `main` is the former `v2-dev` (with the HIL fixes and the user
  documentation merged); the open translation work (plan 0030) moved as branch `i18n-de-sk-pl`.
- The history was rewritten with `git filter-repo`: `LICENSE` is the MIT text in every commit; commits that only
  changed `LICENSE` became empty and were dropped. Commit hashes differ from `hardwario/hio-monitor`; hashes quoted
  in plans and ADRs refer to the old repository.
- CI runs on pushes to `main` and on pull requests. The update check, release links, package metadata and the
  release checklist (OIDC subject `repo:hardwario/monitor:environment:release`) use `hardwario/monitor`.
- `hardwario/hio-monitor` keeps the old application on `main` and the old v2 branches as they were.

## Consequences

- Existing clones of the v2 branches cannot be pulled into the new repository; work continues from fresh clones.
- The repository settings of `packaging/RELEASE.md` (environment `release`, tag protection, signing) are set up in
  `hardwario/monitor`.
