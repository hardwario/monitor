# ADR-0389 — The AI consent prompt names the firmware and the MCU

- **Status:** Accepted
- **Date:** 2026-11-19
- **Decided by:** Matej Strnad (2026-11-19, the mock of plan 0054 approved as shown; the catalog lookup D-5 "Like
  the CLI" the same day)

## Context

The consent prompt for an AI client's `flash` (ADR-0101, ADR-0108) shows catalog firmware only as its 32-digit ID,
the MCU as the internal target id ("app") and the client as "name version" glued together. The user asked on
2026-11-19 for the firmware by name and for clearer layout and wording.

## Options considered

- Firmware: **name + version with the full ID as a small second line** / name only / ID only (today).
- Rows: **Firmware, Device (with Show device), MCU, Client** / today's order with new labels only.
- Permanent choice: **"Always allow … for <client>" with a tooltip** / today's "Allow … for <client>".
- The AI flash tool did not read the catalog page (only the download): **read it first, like the CLI** / no lookup
  (the prompt keeps the ID only).

## Decision

- Catalog firmware is shown as `<name> <version>  (<size>)` with the full catalog ID as a small second line; without
  a known name the row shows the ID as before. A file is shown by its full path as before.
- Row order: Firmware, Device (with the "Show device" link in the row), MCU ("Application MCU" / "LTE MCU"
  instead of the target id), Client (`<name>, version <version>`).
- The permanent choice reads "Always allow <category> for <client>" and has a tooltip saying it holds until it is
  removed in Settings › AI assistants (MCP) (the section's real title).
- The window itself (ADR-0108), the categories, the timeout and Esc = Deny are unchanged.
- The MCP `flash` tool with a `catalog_id` reads the catalog page before the download, like the CLI and the Flash
  page (ADR-0166): the page's SHA-256 checks the downloaded HEX file (`firmware.checksum_mismatch`, before any
  prompt), its name and version go into the consent details. When the page cannot be read the flash goes on without
  the check and the prompt shows the catalog ID only (as the CLI does; only `cancelled` ends the call). The tool
  result and the audit log are unchanged (ADR-0377).
- The MCU is shown as "Application MCU" / "LTE MCU" (an unknown target by its id); there is no profile title for a
  target.

## Consequences

- The consent `details` of the flash tool carry the catalog name and version when the catalog page was read.
- The full ID and the full file path stay visible in every case, so a name can never hide what is flashed. The name
  and version are bounded (`maxCatalogFieldBytes`), flattened to one line of printable text and shown as plain text.
- An AI flash by catalog ID makes one more request (the catalog page, at most its lookup timeout) and is verified
  against the page's SHA-256 when the page is readable.
- Several prompt strings change and are translated again in four languages.
