# ADR-0241 — GUI consent remembers clients by clientInfo.name (review R6)

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0014 revision session)

## Context

ADR-0101 offers "Allow <category> for <client> until it disconnects". Legacy clients have a session id
(`Mcp-Session-Id`); modern clients (2026-07-28) have no session and identify themselves only with the
self-declared `clientInfo.name`, which `libs/mcp` uses as `RequestContext::clientKey` (review R6 of 0013 moved the
question to 0014).

## Options considered

1. **Keep `clientKey` (legacy: session id, modern: `clientInfo.name`) and forget grants on more occasions (chosen).**
   Anyone with the token is trusted anyway (ADR-0102); a name can be faked only by a holder of the token.
2. Remember answers only for legacy sessions; modern clients get "Allow once" / "Deny" only.

## Decision

Option 1.
- Grants ("Allow … for <client> until it disconnects") are keyed by `RequestContext::clientKey`.
- Grants are forgotten when: the legacy session ends; a modern client was idle for 30 min
  (`IConsentBroker::clientGone`); the user revokes them in Settings; the token is regenerated; the server is
  switched off, its port changes or the app quits.
- The prompt and the grant list show `clientInfo.name` (and the version when given).

## Consequences

- A second program with the token that calls itself "Claude Code" inherits Claude Code's grants. This is accepted:
  the token is the security boundary (ADR-0102), and the Settings section shows the grants with Revoke.
