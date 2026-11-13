# ADR-0334 — Remote session: strictly compatible with the existing Terminal relay, 6-digit code

- **Status:** Accepted
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

Supersedes ADR-0320 and ADR-0327. Amends ADR-0322, ADR-0328, ADR-0329 and ADR-0330.

## Context

The session code is read out over the phone, so it must stay a short number. The relay
(`~/terminal/server/index.js`, Socket.IO on terminal.hardwario.com) and the web Terminal are not going to change.
The relay accepts any namespace `/^\d+$/` with no authentication. When a socket connects it emits `newClient` to
**every** socket in the namespace, the new one included. It relays only `at`, `sync` and `write`, each to every
**other** socket in the namespace. Any other event is dropped. It logs the payloads only at `debug` (the default
level is `info`).

## Decision

- Monitor speaks exactly this protocol to the unchanged relay. No relay or web Terminal change is required or assumed.
- The host Monitor creates a 6-digit code 100000–999999 with a CSPRNG (`QRandomGenerator::system()`) and shows it
  grouped for reading aloud ("482 913"). The link is `https://terminal.hardwario.com/?session=<code>`.
  A joiner enters the code (or pastes the link).
- **Code collision / foreign host:** the host only *sends* `at` and `sync`. If it receives `at` or `sync` within 2 s
  after connecting, it picks a new code and reconnects (max 3 tries). Later in the session it warns ("Another
  device uses this code") and ends the session.
- The only extra security is confirmation in Monitor: join approval (ADR-0335) and per-operation confirmation
  (ADR-0324 for operations). Remote operations are tunneled in the existing events (ADR-0336).
- Amendments: ADR-0328 limits are enforced by Monitor alone (the relay has no expiry). ADR-0329: no end-to-end
  encryption is possible while staying compatible, and protocol numbering is dropped. ADR-0330: a joiner takes
  the relay from the configured URL. A pasted link to another host needs confirmation. There is no `#k=` secret.
  ADR-0322: operations use the tunnel from ADR-0336 instead of `hwio:*` events.

## Consequences

- No dependency on the backend team. The feature works with the relay and web Terminal as deployed.
- Anyone who guesses the code while a session is open can connect, read the live console and send lines. Monitor
  limits the damage only through confirmations (ADR-0335). The share dialog says so plainly.
- Tests use a fake relay that copies the server's behaviour (namespace regex, `newClient` to all, broadcast to others).
