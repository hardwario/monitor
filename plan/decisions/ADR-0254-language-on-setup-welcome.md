# ADR-0254 — Language choice on the setup wizard's Welcome step

- **Status:** Accepted (plan 0035)
- **Date:** 2026-11-04
- **Decided by:** Matej Strnad (2026-11-04, accepted the recommendation)

## Context

With five UI languages (ADR-0248) the app follows the system language by default (`system`); changing it needs
Settings → General. The first-run wizard is the first screen a new user sees.

## Options considered

1. **A language combo box on the Welcome step (System default, English, Čeština, Deutsch, Slovenčina, Polski),
   bound to the same setting as Settings → General; the wizard re-translates live (chosen).**
2. No language control in the wizard (Settings only).

## Decision

Option 1 (accepted by Matej Strnad, 2026-11-04).

## Consequences

- The wizard must rebuild its step texts on `LanguageChange` (the `retranslate` pattern of the view models).
- No new settings key.
