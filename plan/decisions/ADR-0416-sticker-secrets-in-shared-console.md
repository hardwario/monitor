# ADR-0416 — STICKER secrets in a shared console

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad, 2026-11-24, as recommended (plan [0059](../features/0059-remote-support-new-devices.md), "Approve as recommended")

## Context

STICKER debug firmware prints and sets its secret key, claim and vendor tokens and LoRaWAN keys with
`config <key> [<hex>]`, as `config secret-key <hex>` without a colon (`sticker-firmware/app/src/app_config.c:616-631`,
`:1273-1300`). The existing output redaction matches only `key: value` (`libs/device/src/profile.cpp:130-160`).
`settings erase / vendor-reset / factory-reset` and `nfc write / clear / regw` destroy identity or tag state
(`app_settings.c:232-252`, `app_nfc.c:2178-2200`). `at read` lines reach every socket in the namespace, including
participants not yet allowed (ADR-0335). The STICKER's key is the customer's only NFC access (ADR-0344).

## Options considered

- Redaction: (a) a new word-prefix output rule for everyone; (b) refuse `config show` remotely; (c) nothing.
- Commands that set keys or destroy identity:
  (a) a deny-list refused from remote on STICKER only;
  (b) the same on every profile, CHESTER included;
  (c) allowed with redaction, as on CHESTER today;
  (d) a STICKER allow-list of remote commands.

## Decision

Proposed (plan 0059, D-9 / D-10):

- The `sticker` `app` target gets sensitive command prefixes for each secret `config` key and
  `settings vendor-reset`. A new rule kind `sensitiveOutputPrefixes` redacts lines that start with those words. It
  applies to the relay, the console file and the history, whoever typed the command.
- On profiles with `refuseRemoteSensitive` (STICKER only), the host does not pass a remote line that sets a sensitive
  value, sets `config serial-number` / `config nonce-counter` (they break NFC access), or is listed in the profile's
  `remoteRefusedCommands` (`settings erase / vendor-reset / factory-reset`, `nfc write / clear / regw`). The
  deny-list holds only while the shell cannot run a command under another name (no `SHELL_CMDS_SELECT`, no aliases). It writes an audit line and tells support. CHESTER
  is unchanged.
- The share view tells the customer that the STICKER's keys are hidden from support and cannot be changed by it.

## Consequences

The redaction is name-based. The list is reviewed with every prepared-firmware bump (ADR-0408), and commands that
were not audited are a known gap. The customer can still change keys locally. Support must ask the customer to do it.
