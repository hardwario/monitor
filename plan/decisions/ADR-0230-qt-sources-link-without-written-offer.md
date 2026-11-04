# ADR-0230 — Qt sources: a link to the exact source archive, no written offer

- **Status:** Accepted (supersedes the written offer of ADR-0119 and ADR-0206)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (RC preparation session)

## Context

ADR-0119 put a written offer for the Qt sources into `licenses/Qt-SOURCE.txt` (HARDWARIO a.s. sends the sources on
request for three years) and made its legal review a release gate. Since ADR-0206 HARDWARIO Monitor itself is MIT;
the obligation for the Qt sources comes only from shipping the unmodified Qt libraries under LGPLv3.

## Options considered

1. **Like most open-source Qt applications: the LGPL text, the notice and a link to the exact upstream source
   archive of the shipped Qt version on download.qt.io; no written offer (chosen).**
2. Keep the written offer (a three-year commitment of the company, legal review).
3. Attach the sources of the shipped Qt modules to every GitHub release.

## Decision

- `Qt-SOURCE.txt` names the Qt version, says that Qt is linked dynamically, replaceable and unmodified, refers to
  `Qt-LGPL-3.0.txt` for the user's rights and links the exact source archive and the module sources of that
  version on download.qt.io. It contains no written offer.
- The release gate "legal review of `Qt-SOURCE.txt`" is dropped.
- If Qt is ever modified or the upstream archive stops being available, option 3 (sources next to the release)
  becomes necessary and gets a new ADR.

## Consequences

- No commitment of the company to send sources; the compliance relies on the upstream archive staying available,
  as is common practice for unmodified Qt.
- Packages still ship `Qt-LGPL-3.0.txt`, `GPL-3.0.txt` and keep Qt dynamically linked and replaceable (ADR-0119).
