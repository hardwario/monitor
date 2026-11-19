# ADR-0324 — Remote session consent: commands marked, operations confirmed each time

- **Status:** Accepted; the command part superseded by ADR-0335 (operation confirmation still valid)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

- Console commands from joiners run without a prompt. They are marked **remote** in the host's console (like MCP
  markers) and are rate- and size-limited.
- Every flash, erase, recover or reset request opens a confirmation on the host (GUI dialog, CLI prompt). Deny is the
  default, and the request is denied automatically after 60 s. The CLI without a tty denies all requests.
- Joins and leaves are shown in the host console. **End sharing** is always one click away. Closing the tab,
  disconnecting or quitting ends the session.

## Consequences

Support works fluently in the console. Anything that changes firmware or resets the device needs the customer's
explicit consent.
