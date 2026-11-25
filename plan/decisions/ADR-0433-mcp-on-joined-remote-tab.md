# ADR-0433 — AI clients may use a joined remote tab; sessions themselves stay human

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended ("Approve the rest"; plan [0062](../features/0062-mcp-remote-session.md), D1, D14, D16)

Supersedes ADR-0332 and amend plan 0059, decision D-22.

## Context

ADR-0332 gave MCP no remote-session surface, and plan 0059 D-22 (2026-11-24) kept a joiner's remote tab invisible
to AI tools. For 2.0.0-rc.5 the user wants an AI client on support's machine to work with a customer's device
through a joined session: see the tab, read the console, send commands, request the joiner's operations.

## Options considered

1. Allow it behind a setting that is off by default.
2. Allow it always.
3. Allow it behind a setting that is on by default.

MCP in the CLI: `hwio mcp` holding a joined session in rc.5, or later.

## Decision

Option 1.

- A joined remote tab is available to AI clients when the setting "AI clients may use remote support sessions" is
  on (default off). The details are in ADR-0434 to ADR-0438.
- Unchanged from ADR-0332: an AI client cannot create, join, leave or end a session, share a device, or read a
  session code or link. Sharing a device with an outside party stays a human decision.
- `hwio mcp` gets no remote session in rc.5. A CLI MCP server with a joined session needs its own consent design
  and a later plan.

## Consequences

- ADR-0332 gets "Superseded by ADR-0433"; plans 0044 (non-goal "MCP over the relay; any MCP tool for remote
  sessions") and 0059 (D-22) get a dated note.
- A customer's console text can reach an AI provider when support turns the setting on. The setting, the prompt
  and the privacy document must say so.
- "Every backend feature also gets its CLI command" is not met for this feature in rc.5: the joiner side over
  `hwio mcp` is a recorded gap (the host side needs nothing, ADR-0435).
