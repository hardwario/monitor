# ADR-0361 — A setting hidden by the mode is revealed for one visit when an error or guide links to it

- **Status:** Accepted
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (plan [0045](../features/0045-user-modes.md), D-4)

## Options considered

1. Reveal the section / row for this visit with a note (chosen).
2. Switch the mode permanently.
3. Only a hint with a button to switch the mode.

## Decision

When Settings is opened at a section or row the mode hides, it is shown for this visit with the note *"Shown because
it was needed. It is part of the <mode> mode."* Leaving Settings hides it again; the mode is unchanged.

## Consequences

Error suggestions and guides keep working in Basic mode without changing the user's choice.
