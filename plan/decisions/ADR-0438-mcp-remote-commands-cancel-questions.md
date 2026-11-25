# ADR-0438 — MCP over a remote link: command end rule, limits, cancel and questions

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended ("Approve the rest"; plan [0062](../features/0062-mcp-remote-session.md), D9, D12, D13)

Amends ADR-0104 (end of `send_command`) and ADR-0105 (cancel of an AI operation) for remote tabs.

## Context

`send_command` waits for the command's echo and ends at the shell prompt, or after 300 ms without output, or at
the timeout (default 5 s). A remote session reports no prompt and delivers one relay event per line. The host
drops a line silently when the participant is not allowed yet, above 512 bytes and above 20 lines per second. The
tunnel has no cancel message: the customer's Monitor finishes a started operation. A remote LTE update can ask
support questions (ADR-0368).

## Options considered

- End rule: values for a remote link / the local values.
- Cancel: stop waiting / a new cap-gated `op-cancel`.
- LTE update questions: the support person / the customer / the AI client.

## Decision


- `send_command` on a remote tab: refused before sending when longer than 512 bytes or with control characters
  (`usage.invalid_argument`), when the customer has not allowed support yet (`remote.waiting_for_customer`), when
  the session ended (`remote.host_ended`). No echo within 5 s gives `remote.command_not_delivered`; a
  `line-refused` gives its code. Output ends after 1000 ms without output, always at the timeout (default 10 s,
  maximum 120 s); the prompt rule is not used on a remote tab (a remote session reports no prompt). The echo is
  matched against the trimmed line.
- One AI command at a time per remote tab (`internal.busy`), at most 30 per minute per tab
  (`mcp.too_many_requests`).
- Cancel of a remote operation means "stop waiting": the tool call ends with `cancelled` and says that the
  operation continues on the customer's device; the remote tab keeps following it and stays busy until its result.
- Questions of a remote LTE update are answered by the support person in the remote tab. An AI client has no tool
  for it and sees "waiting for support's answer" as progress. Without an answer the host's 60 s rule applies.

## Consequences

- A dropped command is an error with a reason, not a timeout with empty output.
- An AI client cannot stop a flash on a customer's device; neither can support today. An `op-cancel` message is a
  candidate for a later plan.
- A slow link costs up to one second per command.
