# ADR-0262 — nRF Util install assist runs the user's download once; no bundling, no copying

- **Status:** Accepted (plan 0038, D1, D2, D10)
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (2026-11-05, in chat; plan 0038 approved with the recommended options)

## Context

rc.2 feedback: installing nRF Util (download, PATH, chmod, `nrfutil install device`) is too complicated. ADR-0006 and
ADR-0016: vendor tools are never shipped. Nordic's downloaded `nrfutil` is a launcher that bootstraps its core into
`$NRFUTIL_HOME` (default `~/.nrfutil`) on its first run; our locator already searches `~/.nrfutil/bin`.

## Options considered

1. **The user downloads nrfutil in the browser via our direct link; the app runs it once with `install device`
   (chosen).**
2. The app downloads nrfutil from files.nordicsemi.com itself (the app becomes a downloader of a vendor binary;
   ADR-0006 change; proxy/TLS handling in our code).
3. Docs only.

For the file itself: run it in place (chosen) / copy it to `~/.nrfutil/bin` / store the Downloads path as the custom
path. For MCP: no tool (chosen) / a read-only status tool.

## Decision

Option 1. The app runs the downloaded file **in place**, once, only after an explicit user action (GUI click; CLI
confirmation or `--yes`). It never copies the file and never stores its Downloads path as the `nrfutil/path`
setting; afterwards the normal locator finds `~/.nrfutil/bin/nrfutil`. No MCP tool runs it (D10).

## Consequences

- ADR-0006 and ADR-0016 unchanged: nothing is shipped.
- The app depends on Nordic's bootstrap layout; if Nordic changes it, the app reports "installed, but not found" and
  the user can set the path.
- New functions only in `libs/hw/nrfutil` (rc.4, ADR-0269).
