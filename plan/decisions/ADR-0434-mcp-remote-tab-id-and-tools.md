# ADR-0434 — The remote tab in MCP: device id, marking and tools

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended ("Approve the rest"; plan [0062](../features/0062-mcp-remote-session.md), D2, D7, D8)

## Context

MCP device ids are `jlink:<SN>` and `ble:<id>` (ADR-0098). A remote tab's identity is its session code, which is
the session's only secret. MCP operations run on a local `DeviceSession`, which a remote tab does not have; the
joiner's operations go through `RemoteOperationClient` (ADR-0336). The joiner has flash (catalog), erase, recover,
reset and the LTE update on CHESTER (ADR-0322, ADR-0363); the request for plan 0062 names flash and LTE update.

## Options considered

- Id: `remote:<n>` (a counter per run of Monitor) / `remote:<code>` / the device's serial from the host's hello.
- Operations for an AI client: flash + LTE update + reset / flash + LTE update only / everything the joiner has.

## Decision


- Id `remote:<n>`; `kind: "remote"`; a `remote` block with `owner: "customer"`, the host kind, the join state,
  what can be requested and `secrets: "hidden"`. Every tool result on such a device carries
  `remote: true`. The session code and the relay URL never appear in a tool result, resource, marker or activity
  entry; a host without a hello is named without the code.
- Tools that work: `list_devices`, `status`, `read_terminal`, `read_log`, `wait_for_state`, `wait_for_connection`,
  `send_command`; operations `flash` (catalog ID or a prepared firmware the host lists, ADR-0418), `flash_lte`
  (the host's recommended firmware, no file or version), `reset`.
- Refused: `connect`, `disconnect`, `reconnect` with `mcp.tool_not_allowed` (join and leave stay human,
  ADR-0433); `erase`, `recover`, `stop`, `start`, `release_probe`, `reacquire_probe`, `halt`, `go`, memory,
  register, PIB / UICR and `write_*` tools with `mcp.capability_missing`.
- `libs/mcp_tools` reaches the remote tab through a new interface `IRemoteDevice` in its own headers; it does not
  depend on `libs/remote`.

## Consequences

- An AI client cannot leak or reuse a session code, and cannot confuse a customer's device with a local one.
- An AI client cannot leave a customer's device without firmware (no erase, no recover); a person still can.
- New profiles of plan 0059 (targets, prepared firmware) appear through the host's hello without changes here.
