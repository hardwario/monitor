# ADR-0419 — Sharing a STICKER tab without a console

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended (plan [0059](../features/0059-remote-support-new-devices.md), "Approve as recommended")

## Context

STICKER release firmware has no shell or RTT (`sticker-firmware/app/prj.conf`). `RemoteHost` shares a connected
`ConsoleSession`. Support's first step for a misbehaving release unit is usually to flash the debug firmware.

## Options considered

- (a) Share an operations-only session (`console: false`) on a STICKER J-Link tab.
- (b) No: the customer flashes debug locally (Expert mode, 0058 D7) and then shares.

## Decision

Proposed (plan 0059, D-17): (a) for `sticker` only, in phase B, after checking how the connect path reports
"connected without RTT". Remote lines are refused with `remote.console_unavailable` on the support side. `remote.no_console` is only the
customer's message when such a share is not possible. The session turns into a console session
when RTT appears, and the host re-sends its hello.

## Consequences

This needs a host path without console lines and a Share entry on the Flash page. Other profiles without a console
(STM32 chip) stay excluded (ADR-0421).
