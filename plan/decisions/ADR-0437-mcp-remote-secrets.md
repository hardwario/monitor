# ADR-0437 — Secrets are hidden at the MCP boundary of a remote tab

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended ("Approve the rest"; plan [0062](../features/0062-mcp-remote-session.md), D10)

## Context

MCP returns console lines as the screen shows them; it has no redaction (ADR-0186 hides secrets in files only).
In a remote session the secrets belong to a customer. A Monitor host hides them toward the relay (ADR-0356), with
a gap for the command form that ADR-0432 closes for new hosts. ADR-0432 also hides at the joiner, but only for
hosts without a hello. A 2.0.0-rc.4 Monitor host sends a hello and still passes `lrw config appkey <value>`. A
real LoRaWAN key reached support's console on 2026-11-24 (plan 0061).

## Options considered

1. Redaction at the MCP boundary for every remote tab, whoever hosts.
2. Rely on ADR-0432.
3. Refuse AI access unless the host is a Monitor with plan 0062.

## Decision

Option 1.

- An AI client works on a hidden view of a remote tab's console: each line passes `device::ConsoleRedaction`
  (plan 0060, ADR-0426) of the profile named in the host's hello, CHESTER without one, **before** MCP matches a
  pattern, evaluates a wait condition, captures a command's output or returns a line (`read_terminal`,
  `read_log`, `send_command`, `wait_for_state`, the `device-log` and `device-terminal` resources). A pattern
  cannot test for a hidden value.
- The same view replaces the session code (ADR-0434) wherever it appears in a line.
- A `send_command` line that sets a sensitive value is refused on support's side with `remote.command_refused`
  and never sent. Markers and activity entries store the redacted command.
- The device entry says `secrets: "hidden"`.
- Local tabs are not changed by this ADR.

## Consequences

- An AI client sees `<redacted>` whoever hosts the session; a value support needs is read out by the customer to
  the person.
- The hiding is name-based: secrets of firmware whose profile Monitor does not know are not recognised. The
  privacy document says so.
- A test feeds made-up key values through every read path and fails when one returns a value.
- MCP still returns secrets of local devices; that is a separate decision.
