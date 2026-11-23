# ADR-0385 — Recent firmware entries show the application name and version

- **Status:** Accepted
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (rc.4 issues, plan [0051](../features/0051-rc4-issues-5-11.md))

## Context

Issue #11: Recent catalog entries show "Catalog ID <hash>"; the stored entry is `{kind, source, time}` only, though the catalog lookup knows name and version after a selection.

## Options considered

1. Store name and version; look up older entries in the background when the list is shown (chosen).
2. Store for new entries only; old entries keep the ID until used again.

## Decision

- Stored entries get `name` and `version` from the catalog lookup when added; the label is "<name> <version>", the tooltip "Catalog ID <id>". Files keep their file name.
- Entries without a name are looked up when the list is shown: one request at a time, at most 10, each ID once per run; a found name is stored; otherwise "Catalog ID %1" stays.
- Unknown stored fields are ignored, older settings stay valid (amends ADR-0054 only for the Recent list label).

## Consequences

Readable list; up to 10 small catalog requests once after the upgrade.
