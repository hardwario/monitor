# ADR-0240 — Backend errors reach the AI in the GUI language (amends ADR-0103)

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0014 revision session)

## Context

ADR-0103 says everything sent over MCP is English, also from the GUI. `hwio::Error` texts are translated when the
error is created (`makeError`, call-site texts in `hwio::device`, `hwio::jlink`, `hwio::firmware`, about 120
places), and nothing can render them in English later. In a Czech GUI an AI would get Czech message, cause and
suggestion. Plan 0013 deferred the question to 0014 (the CLI is English already).

## Options considered

1. **Amend ADR-0103: backend error texts reach the AI in the GUI language (chosen).** No backend change; nothing is
   lost; the stable error id stays English.
2. Render the English default text of the error code (+ detail) over MCP; call-site texts are lost in the GUI.
3. Add the English source texts (and arguments) to `hwio::Error` and fill them at every creation site (a plan of
   its own, about 120 call sites).

## Decision

Option 1.
- Over MCP from the GUI, `hwio::Error` message, cause and suggestion are sent as created, i.e. in the GUI language
  (English or Czech). The error id (`probe.not_found`, ...) is always English and is what clients rely on.
- Everything else sent over MCP stays English as in ADR-0103: tool, resource and prompt descriptions, schema texts,
  results, the MCP call-site texts of `hwio_mcp` / `hwio_mcp_tools` and the GUI variant of `mcp.tool_not_allowed`.
- The CLI is unchanged (English only, ADR-0027).

## Consequences

- ADR-0103 gets an "Amended by ADR-0240" note.
- An AI using a Czech GUI sees mixed-language tool results (English structure, Czech error texts). Modern AI
  clients understand Czech; support compares error ids, not texts.
- Option 3 stays possible later as its own plan; then this ADR is superseded.
