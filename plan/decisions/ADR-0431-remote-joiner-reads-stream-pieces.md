# ADR-0431 — Remote joiner: device output of a host may be pieces of a stream, not lines

- **Status:** Accepted
- **Date:** 2026-11-24
- **Decided by:** Matej Strnad (2026-11-24, "Approve all", plan 0061)

## Context

Plan 0044 took the relay protocol from the web Terminal: an `at` `read` event and an element of a `sync` entry's
`outputs` are one line (ADR-0334). The HARDWARIO mobile app is a third host. It sends one Bluetooth notification per
`read` event (several lines with `\r\n`, cut anywhere) and the whole output of a command as one `outputs` string
(plan [0061](../features/0061-remote-support-field-fixes.md), amendment 1, A1.3). Monitor's joiner passes each of
them to `sanitizeDeviceText()` as one line, which keeps only the text after the last carriage return: support sees
empty lines, single prompts and a few last lines instead of the answers. The web Terminal draws the same messages
as text and loses nothing. The mobile app sends no hello and ends sharing with its own event `at {event: "end"}`.

## Options considered

- (a) The joiner has two modes. Line mode (start; always after a Monitor host's hello): as today. Stream mode,
  entered for the rest of the session when a `read` or an `outputs` element contains a line feed: the text goes
  through `device::LineAssembler` (lines across events, unfinished line at most 4096 bytes), a Zephyr prompt is not
  a line and the first empty line after a command is not output, as in Monitor's own Bluetooth console (ADR-0082).
- (b) No state: every event is split at line feeds on its own. Nothing is lost; a line cut by a notification shows
  in two pieces and the prompt shows as a line (what the web Terminal shows).
- (c) No change in Monitor; the mobile app sends lines. Installed app versions stay broken, and the app's own viewer
  expects pieces.
- The app's `end` event: (a) ends the joiner's session like the tunnel `end` (ADR-0354); (b) ignored as today.

## Decision

Proposed: (a) for the output, (a) for `end` (plan 0061, D8 and D11).

## Consequences

- The host kind is recognised by content, not by a declared capability; a Monitor host cannot trigger stream mode
  (its lines contain no control characters, and its hello fixes line mode).
- `Transcript::fromJson` splits a multi-line `outputs` element into lines; the transcript bound counts lines as today.
- More lines of a non-Monitor host become visible, also secrets it does not hide: to be decided together with
  ADR-0432.
- The interop checklist of plan 0044 gets the mobile app as a host and as a viewer. The mismatch in the other
  direction (the app as a viewer of a Monitor host joins lines without line breaks) is the app's to fix.
- A host that sends a notification twice produces doubled or mixed lines; Monitor does not remove duplicates.
