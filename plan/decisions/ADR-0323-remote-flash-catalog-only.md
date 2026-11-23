# ADR-0323 — Remote flashing only by firmware catalog ID

- **Status:** Accepted
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

A remote flash request carries only a catalog firmware ID. The **host** downloads and verifies the firmware through
the existing catalog and cache (ADR-0037). No firmware or other binary crosses the relay. Unknown IDs are rejected
with `remote.firmware_not_in_catalog`.

## Consequences

- Support cannot flash custom or development builds remotely. Those still need the customer to flash a file locally.
- The relay never carries executable code.
