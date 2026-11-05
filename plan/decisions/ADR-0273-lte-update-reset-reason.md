# ADR-0273 — An LTE update reports a pin reset of the LTE MCU (target.reset_during_update)

- **Status:** Accepted
- **Date:** 2026-11-05
- **Decided by:** Matej Strnad (rc.3 HIL finding H5, plan [0033](../features/0033-rc2-feedback.md))

## Context

HIL H5 (2026-11-05): `hwio chester lte erase --yes` + plain `hwio chester lte flash` (modem via nRF Util, then the
application via J-Link) failed in about 3 of 10 runs, in nRF Util ("Failed to write DebugPort register") or in our
download ("Failed to erase sectors", `JLINKARM_EndDownload` -5). nRF Util alone failed the same way. The LTE MCU's
POWER.RESETREAS showed the reset-pin bit after every failure and after no success, and the pin reset also came every
2–3.5 minutes with the J-Link idle: the CHESTER application firmware on the APP MCU resets an LTE MCU that does not
answer. The guided update (ADR-0234) erases the application first and is not affected; the plain update leaves the
application running. Before this decision the user saw only the generic program / modem error.

## Options considered

1. After a failed step read the reset reason; on a pin reset report a dedicated error that names the guided update,
   and warn before a plain CLI update (chosen).
2. A bounded retry of the failed step (rejected: the reset comes again; nRF Util fails the same way).
3. Documentation only.

## Decision

- `TargetProfile::resetReason` (address, reset-pin mask; CHESTER `lte`: 0x50005400, bit 0) — profile data, no
  device-specific code.
- Before a flash, modem update or LTE bundle of such a target the session worker connects it, checks with FICR that
  the probe reaches that MCU (never written to another MCU, not a protected one) and clears the register (write 1 to
  clear) while the CPU runs: no extra reset, and an operation that fails before it starts does not leave the MCU
  halted. For a bundle once, before step 1 (when that was not possible, again before step 2). A failure to clear is
  logged and only switches the check off.
- After a failed step of an armed update whose error a reset can cause (category probe, target, firmware or timeout;
  not cancelled, wrong family, other connector or protected) the MCU is identified again and the register read; with
  the reset-pin bit set the error becomes the new `target.reset_during_update` (category target, exit 6, wizard
  LteUpdate). The detail keeps the original error id, its detail and the register value. A failed read, another MCU
  answering or a clear bit keep the original error (the check never hides it).
- The plain `hwio chester lte flash` prints a warning (stderr, human mode; log always) that the application firmware
  can reset the LTE MCU and that `--guided` stops it first. CLI text is English only (ADR-0027). The GUI has no plain
  LTE flash path (the LTE kind opens the wizard, ADR-0234), so it gets no warning.
- Docs: a troubleshooting common problem and the error entry in all five languages (plan 0037 anchors).

## Consequences

- A plain LTE update connects and halts the LTE MCU once more before it starts (milliseconds; on a target that does not
  connect, the usual connect attempts once more before the operation's own).
- The user gets an actionable message instead of a library error; no retry hides the cause.
- Another device whose MCU can be reset by a sibling only needs `resetReason` in its profile.
