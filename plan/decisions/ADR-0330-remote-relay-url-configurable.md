# ADR-0330 — Configurable relay URL

- **Status:** Accepted; amended by ADR-0334 (6-digit code, no `#k=` secret)
- **Date:** 2026-11-06
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

## Decision

Setting `remote/relayUrl` (default `https://terminal.hardwario.com`) in Settings → Advanced, and `--relay URL` in the
CLI. Only `https` URLs are accepted. The share dialog shows the relay host name. A joiner takes the relay from the link
host, which must match the configured relay, or the user confirms it ("This link uses another server: …").

## Consequences

Staging, tests (fake relay) and a possible self-hosted relay work without a rebuild. A link to a foreign server
cannot silently redirect a joiner.
