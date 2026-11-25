# ADR-0432 — Remote session: secrets a joiner receives, and secrets in command form from a Monitor host

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, "Approve all", plan 0061)

Would amend ADR-0356 (redaction toward the relay) and apply ADR-0186 to the joiner.

## Context

In a session hosted by the HARDWARIO mobile app, a `lrw config appkey …` line of a `config show` answer reached
support's Monitor with its value and was saved with "Save view…" (plan
[0061](../features/0061-remote-support-field-fixes.md), amendment 1, A1.8). Two separate facts:

1. The mobile app and the web Terminal hide nothing. Monitor's joiner shows what it receives; it has no console
   file. ADR-0186 hides secrets in files and shows them on screen; ADR-0356 says that through a Monitor host
   "support cannot read a claim token or passkey". ADR-0426 (plan 0060) makes "Save view…" hide secrets, on screen
   they stay.
2. A Monitor host hides `<key>: <value>` output only. A secret in command form, as `config show` prints it, goes to
   the relay with its value, although the console file hides this form (ADR-0186 amendment of 2026-11-02).

The fix of the blank lines (ADR-0431) makes more lines of a non-Monitor host visible, among them `claim token` and
`ble passkey` of `info show`.

## Options considered

Joiner (plan 0061, D9):

- (a) For a host without a hello the joiner applies the profile's rules before a line enters the console log:
  screen, Copy, "Save view…" and the CLI show `<redacted>`.
- (b) Only "Save view…" hides (ADR-0426), with a test for a remote tab; the screen shows the value.
- (c) Nothing.

Monitor host (plan 0061, D10):

- (a) The host uses the console file's redaction (`device::ConsoleRedaction`) for `read` lines and the transcript.
- (b) No change; the consequence text of ADR-0356 is corrected.

## Decision

Proposed: (a) and (a).

## Consequences

- Support sees the same hidden values whoever hosts the session; a value support needs is read out by the customer.
- The joiner's hiding uses the CHESTER profile's lists; secrets of other firmware are not known to it, and what the
  relay and other participants already received cannot be taken back. Only the host can keep a secret local.
- One redaction implementation for the console file, "Save view…", the host's relay traffic and the joiner; to be
  merged with the output prefixes of plan 0059 (ADR-0416).
- ADR-0356 gets an "Amended by ADR-0432" note when this is accepted.
