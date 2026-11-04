# ADR-0257 — J-Link firmware update runs on an owned worker; the probe is reserved on the GUI thread

- **Status:** Accepted (plan 0036)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

`device::updateProbeFirmware(registry, location, sn)` is synchronous (10–30 s) and reads the `SessionRegistry`,
which is owner-thread only (the GUI thread in the app). The `ProbeScanner` enumerates probes every 2 s with its own
library copy while a picker is visible.

## Options considered

1. **A `ProbeFirmwareWorker` with one owned `std::jthread` per update (joined at the end and on shutdown); the GUI
   thread checks `checkAvailable(sn)` and holds a registry reservation for the serial (no tab can connect to it,
   pickers show "Updating firmware…"); the worker calls a registry-free overload
   `updateProbeFirmware(const RegistryOptions&, location, sn)`; the `ProbeScanner` is suspended while it runs
   (chosen).**
2. A new request kind on the existing `DiagWorker` (shares its queue; its drop/replace rules and cancellation do
   not fit an operation that must not be interrupted).
3. Run it through a probe-only `DeviceSession` (needs a new session operation).

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04).

## Consequences

- New small public API in `libs/device` (overload + `SessionRegistry::reserve()` RAII token) and in `ProbeScanner`
  (`suspend()` token); unit tests on the fake J-Link and a TSan run.
- One update at a time per application (a second request is refused with "Another J-Link update is running").
