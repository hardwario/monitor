# ADR-0382 — Error panels inside a wizard show a troubleshooting link instead of Dismiss / Help me fix this

- **Status:** Accepted (amended by plan 0051 implementation notes: SourceCard panels keep their primary action in a wizard, 2026-11-17)
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (rc.4 issues, plan [0051](../features/0051-rc4-issues-5-11.md))

## Context

Issue #9: in the guided LTE update the error panel's Dismiss emits a signal nobody handles, and "Help me fix this" asks the launcher for a second wizard, which ADR-0066 (one wizard at a time) refuses silently.

## Options considered

1. Hide both inside a wizard and show a Troubleshooting docs link (chosen).
2. Wire Dismiss; Help opens the docs.
3. Help closes the current wizard and opens the error's wizard.

## Decision

`ErrorPanel` gets `inWizard` (set for everything loaded in `WizardDialog`, including embedded `SourceCard`s). When true it hides the primary action and "Help me fix this" and shows a "Troubleshooting" link to the documentation section for the error code (plan 0037 links). The wizard footer (Try again / Cancel) stays the way forward.

## Consequences

No dead buttons in wizards. ADR-0066 unchanged.
