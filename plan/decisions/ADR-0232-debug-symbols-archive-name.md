# ADR-0232 — Symbol archives are named `debug-symbols` and stay on the release

- **Status:** Superseded by ADR-0370 (symbols are no longer release assets; the archive names stay)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (rc.2 feedback)

## Context

The rc.2 release listed `hwio-monitor-<version>-symbols-{linux,macos,windows}.zip` next to the packages. Testers
asked what the "symbols" files are and whether they need them. The archives hold the debug symbols of the exact
release build (ADR-0123: split DWARF, `.dSYM`, `.pdb`); without them a crash report of that build cannot be
symbolized. Workflow artifacts expire after at most 90 days, while a release stays as long as its tag.

## Decision

- The archives are named `hwio-monitor-<version>-debug-symbols-<os>.zip` (`<os>` = `linux`, `macos`, `windows`).
- They stay release assets (covered by `SHA256SUMS` and the attestations); the release notes say that users do
  not need them.

## Consequences

- The name says what the files are; the download list stays unambiguous next to the packages.
- Releases up to v2.0.0-rc.2 keep the old name; tools that fetch symbols accept both names.
