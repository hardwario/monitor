# ADR-0332 — No MCP surface for remote sessions

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

MCP gets no remote-session tools: an agent cannot create, join, end or inspect a session, nor read a link. Lines from
remote joiners in the console log are visible to MCP like any other console line (with the remote origin).

## Consequences

Sharing a device with an outside party is always a human decision in the GUI or CLI.
