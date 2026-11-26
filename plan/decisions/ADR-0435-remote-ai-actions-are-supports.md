# ADR-0435 — For the customer, actions of support's AI client are support's actions; no protocol change

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24 ("no, to the client it shows as classic operations from the operator";
  plan [0062](../features/0062-mcp-remote-session.md), D3; D4, D11 and D17 follow from it)

## Context

With ADR-0433 an AI client on support's side can send commands to a customer's device and request operations. The
draft of plan 0062 proposed to tell the customer's Monitor (a capability `ai`, marked commands, a separate
permission of the customer). The relay broadcasts and the host cannot tell who sent a `write` (ADR-0414), so such
a marking would have been a statement of support's Monitor, not a proof. The web Terminal and the mobile app could
not show it at all.

## Options considered

1. A capability `ai`: marked AI commands and operations, one question to the customer per join approval.
2. No change for the customer: AI commands and operations look like support's.

## Decision

Option 2.

- The session protocol is not changed. A command of support's AI client is the same `write` string as a line
  support types; an operation is the same `op-request`.
- The customer's Monitor shows and audits them as today ("— Remote: command … —", "Support asks to …"). The
  customer's controls are the existing ones: the join approval (ADR-0335), the question before every operation
  (ADR-0324), End sharing.
- AI markers ("— AI (client): … —") and the activity list stay in support's Monitor and are not sent to the
  customer.
- Whether an AI client may act is decided on support's machine (ADR-0433 setting, ADR-0436 permission).

## Consequences

- Works the same with every host: Monitor of any version, web Terminal, mobile app. No interoperability risk.
- The customer's software does not show that an AI client typed a command. Support answers for their AI client as
  for their own typing; the user documentation of remote support says that support may use an AI assistant.
- No host-side code, CLI or translation work in plan 0062.
