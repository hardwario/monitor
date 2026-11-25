# ADR-0377 — Consent-gated results say how they were allowed

- **Status:** Accepted
- **Date:** 2026-11-13
- **Decided by:** Matej Strnad (plan 0050)

## Context

The GUI consent prompt (ADR-0101, plan 0014) offers "Allow for this client"; afterwards the same category runs
without a dialog. An MCP client could not tell whether a destructive call was confirmed by a prompt, by an
earlier answer, or by the CLI `--allow` flag.

## Options considered

1. Add a `consent` object to the success result of consent-gated tools (chosen).
2. No change.

## Decision

Option 1. `ConsentDecision` carries a `ConsentGrant` (`Prompted`, `Remembered`, `Flag`). Every tool with consent
categories reports `"consent": {"<category id>": "prompted" | "remembered" | "allowed_by_flag"}`, including
categories requested at run time.

## Consequences

Clients and logs can show how a destructive action was allowed. The broker interface changes (GUI and CLI
brokers, tests).
