# ADR-0417 — Remote operations per device and target

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended (plan [0059](../features/0059-remote-support-new-devices.md), "Approve as recommended")

## Context

ADR-0322 allows flash / erase / recover / reset of `app` / `lte`. The code hard-codes the scopes: erase `Code` on
`app`, `All` otherwise; flash on `app` from the catalog (`host_operations.cpp:81-100, 198-225`). The Nordic chip
(0057) has the targets `nrf52` / `nrf91`, no catalog of its own and a PIB / UICR. STICKER (0058) has `app`,
identity-preserving `Code` erase, erase-all / unlock with `destroysIdentity` (refused over MCP, ADR-0409) and no
recover.

## Options considered

- Nordic: erase (code) + recover + reset; erase (code) + reset; reset only; none.
- Nordic flash: none; a file the customer picks, with the SHA-256 shown on both sides; a URL + SHA-256 from support;
  a CHESTER catalog ID.
- STICKER: identity-preserving erase only; also erase-all with a typed confirmation.

## Decision

Proposed (plan 0059, D-11 / D-12 / D-15):

- A remote operations matrix in `TargetProfile` (`remoteKinds`, `remoteEraseScope`, `remoteFlash`) drives the host's
  `targets` and its validation:
  - CHESTER as today;
  - `nrf52` / `nrf91`: erase `Code` and reset. There is no flash, and no recover: on nRF91 a recover erases the UICR
    and so the PIB, which cannot be restored remotely, and a chip that needs recover has no RTT to share. If the
    user wants parity with CHESTER `app`, recover is the alternative;
  - STICKER `app`: prepared flash (ADR-0418), erase `Code`, reset.
- Never remote:
  - operations with `destroysIdentity` (erase-all, unlock);
  - recover on STICKER;
  - PIB / UICR writes, `modem-flash`, `--allow-uicr`;
  - the provisioning firmware.
- The customer's question names the part and states what is erased and what is kept.

## Consequences

Adding a device means adding matrix data, not code. Remote flash for bare chips needs a later decision (the file
option is the candidate).
