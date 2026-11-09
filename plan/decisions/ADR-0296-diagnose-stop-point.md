# ADR-0296 — Diagnose can stop after a given step

- **Status:** Accepted (plan 0010; amends the Diagnose operation of plan 0007 / ADR-0069)
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (2026-11-09)

## Context

The flash wizard's Cable step needs the Diagnose steps `probe` … `identify` only; plan 0010 says the RTT steps "are
not needed for flashing and are not run". `DeviceSession::diagnose(targetId)` always ran all six steps, so the first
implementation cancelled the operation once `identify` was reported — the RTT search had usually started by then.

## Options considered

1. **`DeviceSession::diagnose(targetId, lastStep = {})`: the steps after `lastStep` are neither run nor reported
   (chosen).**
2. A set of step ids to run (any subset): more general, but the steps depend on each other (identify needs the
   connect, RTT needs identify).
3. Keep cancelling after `identify`.

## Decision

Option 1. `lastStep` is a `diagnoseStepIds` id or empty (= all). "power" is not a stop point (its reading may wait for
the connect when the probe cannot measure VTref, plan 0007 notes); it and unknown ids give `usage.invalid_argument`.
The progress step count and the `checks` / `diagnoseStep` reports end with the stop point. Backend only: no CLI option
(`hwio chester … check` keeps running all steps); a CLI flag would need a separate decision.

## Consequences

- The flash wizard calls `diagnose(target, "identify")`; the RTT search never runs in its Cable step.
- Test: `Diagnose.ALastStepNeverRunsTheLaterSteps` (fake probe: no RTT start, no target access for "probe").
