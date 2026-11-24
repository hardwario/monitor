# ADR-0415 — Console sharing for every device profile

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended (plan [0059](../features/0059-remote-support-new-devices.md), "Approve as recommended")

## Context

Plan 0044 assumes CHESTER everywhere. The joiner always applies the `chester` profile
(`remote_console_session.cpp:178-181`). The host advertises flash / erase / recover / reset for any J-Link tab
(`host_operations.cpp:60-67`, `remote_share_view_model.cpp:95-115`). Share is offered on any connected console
(`remote_share_view_model.cpp:50-54`), so a Nordic chip or STICKER tab could be shared as soon as plans
0057 / 0058 add one. No such tab exists on main yet. Support would then see a CHESTER with operations that all fail. The STICKER debug firmware has a Zephyr shell on RTT with help
(`sticker-firmware/app/debug.conf:47-58`).

## Options considered

- Profile-driven console sharing (the hello names the profile, the joiner applies that profile's rules or a
  neutral one); target names translated on the joiner (or: sent by the host in its language).
- Gate Share to CHESTER until this is done, or leave it as it is.
- Command tree: the same learner as a local tab, or none.

## Decision

Proposed (plan 0059, D-5 / D-6 / D-7 / D-8):

- The host hello names the profile and a display name with the identified part. The joiner uses that profile's
  console rules when it knows the profile, else a built-in neutral `generic` console profile.
- The joiner translates target names from profile + target id and falls back to the host's sanitised display name.
- The command tree is learned and sent as for CHESTER (ADR-0364). A shell that does not answer like Zephyr gets
  none.
- Until plan 0059 phase A is in, Share is offered only on `chester` tabs (step A0). Plan 0057's GUI phase carries
  the same guard if it ships first.

## Consequences

Remote tabs stop showing CHESTER rules for other devices. Every new profile automatically gets console sharing
with correct naming. Operations still need the matrix of ADR-0417.
