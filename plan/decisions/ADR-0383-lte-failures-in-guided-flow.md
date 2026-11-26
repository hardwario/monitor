# ADR-0383 — LTE update failures in the guided flow: no reset-pin rewrite, LOW_VOLTAGE classified, guided advice

- **Status:** Accepted (amended by ADR-0391 (2026-11-23): `target.low_voltage` opens the troubleshooting wizard's no-power topic)
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (rc.4 issues, plan [0051](../features/0051-rc4-issues-5-11.md))

## Context

Issue #9: a modem update failure in the guided flow (application already erased) became `target.reset_during_update` (ADR-0273) with the advice to use the guided update, while nRF Util reported "Failed to configure Coresight: LOW_VOLTAGE". The pin reset cannot come from the erased application.

## Options considered

1. Guided flow: no rewrite to `target.reset_during_update` (chosen).
2. Classify nRF Util LOW_VOLTAGE as its own error (chosen).
3. Guided-specific advice for the failures that remain (chosen).

## Decision

- Operations started by the guided procedure (GUI wizard, `--guided`, remote) carry a guided flag; `explainFailure()` then logs the reset reason but keeps the original error. The plain flow keeps ADR-0273. Supersedes ADR-0273 only for the guided flow.
- nRF Util output containing `LOW_VOLTAGE` becomes `target.low_voltage` (category target): too low supply voltage; check the CHESTER's power supply or batteries and the J-Link cable, then try again.
- In the guided flow `firmware.modem_update_failed` and `target.low_voltage` use the guided suggestion: "Check the power supply of the CHESTER and the J-Link cable on the LTE connector, then click Try again." No guided-flow text points to the guided update.

## Consequences

Correct advice inside the wizard. A real application-caused reset in the guided flow is impossible by design; other pin resets show as the original nRF Util/J-Link error with the reset reason in the log. Troubleshooting docs ×5 updated.
