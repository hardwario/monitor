# ADR-0436 — Support's permission before an AI client works with a customer's device

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended ("Approve the rest"; plan [0062](../features/0062-mcp-remote-session.md), D6)

## Context

MCP permission categories are Flash, Erase, MemoryWrite, Uicr and Connect; `send_command` and the read tools need
none. A client's "always allow" covers every device, and plan 0055 adds an "All" row (ADR-0390). An AI client did
not open a remote tab, so no Connect permission was asked either. A customer's console text would go to an AI
provider without any question on support's machine.

## Options considered

1. A new category "customer's device": one prompt per client and remote tab, covering reading and commands for
   that session; operations prompt every time.
2. A prompt for every command.
3. Nothing beyond the setting of ADR-0433.

## Decision

Option 1.

- `ConsentCategory::RemoteDevice`. The first tool call of a client on a remote tab, reading included, shows
  "<client> wants to work with a customer's device" with **Allow for this session** and **Deny** (default, 120 s).
- Before the grant a client gets only a nameless entry "Remote support session" in `list_devices` / `devices`;
  every other tool, resource, resource update and prompt gives `mcp.consent_denied` without content.
- The grant belongs to one client key (ADR-0241) and one remote tab. It ends when the tab leaves or the session
  ends, when the client goes away, and with **Stop AI** in the remote tab's banner.
- It is never remembered: no setting `mcp/consent/…`, not part of the "All" row, not set by `--allow`.
- Operations on a remote tab show the Flash-category prompt every time; "always allow" and client grants do not
  cover a remote device.
- The same rule for every host (Monitor, web Terminal, mobile app); the customer is not asked separately
  (ADR-0435).

## Consequences

- Nothing of a customer's session reaches an AI client before a person on support's machine agreed for that
  session.
- A long session costs one prompt for reading and commands, and one per operation.
- The consent broker gets a grant scope "client + tab" next to "client".
