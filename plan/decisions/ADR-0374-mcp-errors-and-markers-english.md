# ADR-0374 — MCP texts are English, also errors and console markers

- **Status:** Accepted (supersedes ADR-0240; restores ADR-0103 for errors)
- **Date:** 2026-11-13
- **Decided by:** Matej Strnad (plan 0050)

## Context

ADR-0240 let `hwio::Error` message, cause and suggestion reach MCP clients in the GUI language because the texts
are translated at creation (~120 overriding call sites) and could not be rendered in English later. Testing as an
AI client showed Czech errors and Czech console markers (`— obnoveno —`, written translated into the shared
`ConsoleLog` by the GUI) in MCP results.

## Options considered

1. Keep ADR-0240; fix only the markers.
2. Send the English default text of the error code; call-site wording is lost over MCP.
3. Carry the English source texts in `hwio::Error` (and English marker text in `ConsoleLine`), filled at every
   creation site (chosen).

## Decision

Option 3. `hwio::Error` gains `messageEn`, `causeEn`, `suggestionEn` (English source with arguments), filled by
`makeError()` and by one helper at every call site that overrides a text. `ConsoleLine` gains `textEn` for marker
lines. Everything MCP sends uses the English variants; the GUI, its log view and console files keep the UI
language. The CLI is English as before.

## Consequences

MCP output is English in every UI language (ADR-0103 holds without exception). All error-creating call sites
must use the helper; a unit test guards against empty English texts. `Error` grows by three strings.
