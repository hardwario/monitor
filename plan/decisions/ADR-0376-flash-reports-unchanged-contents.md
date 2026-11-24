# ADR-0376 — flash reports unchanged contents from the J-Link messages

- **Status:** Accepted
- **Date:** 2026-11-13
- **Decided by:** Matej Strnad (plan 0050)

## Context

When the flash already holds the image, J-Link skips programming and logs `Flash download: Bank 0 @ 0x...:
Skipped. Contents already match`; otherwise `<k> range affected (<bytes> bytes)`. `JLINKARM_EndDownload`'s return
value is not documented to tell the cases apart. Our result said only `ok` and our log said `Programmed N bytes`,
so a 1.2 s "flash" looked suspicious.

## Options considered

1. Parse the J-Link per-bank download messages (chosen).
2. Measure whether `JLINKARM_EndDownload` returns something different, fall back to the messages.
3. Read back and compare before downloading.

## Decision

Option 1. The adapter collects the messages of one download (bounded) and derives `unchanged` and
`affected_bytes`. Unknown wording → both are omitted, never guessed. MCP results, CLI `--json` and the log report
them.

## Consequences

Correct, honest reporting for J-Link V9.84 wording; a test pins it. A different J-Link wording silently drops the
fields until the patterns are extended.
