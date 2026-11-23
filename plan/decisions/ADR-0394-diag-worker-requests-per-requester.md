# ADR-0394 — DiagWorker replaces a pending request only of the same requester

- **Status:** Accepted
- **Date:** 2026-11-23
- **Decided by:** — (plan [0056](../features/0056-wizards-as-views-rc5.md) D-10, awaiting Matej Strnad)

## Context

The application has one `DiagWorker` (ADR-0068). A new request replaces a pending request of the same kind; the
dropped caller gets `requestDropped` and resubmits (review 0009 F7). With one wizard at a time this was rare. With a
wizard per tab plus the global setup wizard (ADR-0386, ADR-0391), two wizards waiting for checks replace each other's
pending request and resubmit in turn — a loop of queued calls on the GUI thread for as long as a third request runs.

## Options considered

1. Replace only a pending request of the same kind **and the same requester**; raise `maxQueued` from 4 to 12.
2. Leave it as is.
3. One worker thread per tab.

## Decision

Proposed: option 1. `ChecksRequest` carries a requester token (`std::uint64_t` from `DiagWorker::newRequester()`, a
counter; each wizard and `StartupCheck` takes one; 0 keeps the old same-kind rule). `maxQueued` = 12 (8 tabs, the global wizard, `StartupCheck`, report collect and
save). A full queue still drops the oldest pending request with `requestDropped`. Report requests keep the
same-kind rule.

## Consequences

- Amends ADR-0068 (the worker's queue rule). Concurrent wizards each keep their newest request; one worker thread
  stays.
- The queue bound and the header comment of `diag_worker.h` change; `DiagWorkerTest` covers two requesters.
