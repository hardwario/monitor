# ADR-0363 — Remote LTE firmware update without a catalog ID (guided, host's recommended version)

- **Status:** Accepted (amends ADR-0323 and ADR-0336)
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (rc.4 demo feedback, plan [0044](../features/0044-remote-support-session.md) § Remote LTE
  update)

## Context

ADR-0323 lets support flash only by catalog firmware ID, and only the application core. A local LTE update needs no
catalog ID: Monitor downloads and verifies the HARDWARIO LTE firmware itself and runs the guided update (it saves the
application, tells the customer when to move the J-Link cable, restores the application). Remote support could
therefore not update the LTE firmware at all.

## Options considered

1. The host runs the **guided** LTE update after the customer allows it (chosen).
2. The host runs a plain LTE flash (can fail when the application resets the LTE MCU).
- Version: the host's recommended HARDWARIO LTE firmware (chosen) / support may choose a version.

## Decision

- New tunnel operation `lteUpdate` (no target, no firmware ID). The host's hello advertises it.
- The customer's confirmation names the firmware (e.g. "HARDWARIO LTE firmware 1.7.0") and says that they will have to
  move the J-Link cable; **Deny** is the default, 60 s timeout as for other operations.
- On Allow the host runs the same guided LTE update as **Flash › Update LTE firmware…** / `hwio chester lte flash
  --guided`, with the recommended firmware preselected; the customer does the cable steps. Phase / progress and the
  result (including exit outcome "LTE updated but the application could not be restored") go to support.
- Sharing stays active while the update disconnects and reconnects the device; the joiner sees the state changes.
- Still no firmware or other binary crosses the relay; the host downloads and verifies as locally.

## Consequences

- Support can bring a customer's LTE firmware up to date remotely, but only to the version the customer's Monitor
  recommends.
