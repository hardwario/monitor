# ADR-0367 — Filled accent buttons in the dark theme: white text on a darker blue

- **Status:** Accepted
- **Date:** 2026-11-12
- **Decided by:** Matej Strnad (rc.4 testing, plan [0046](../features/0046-rc4-fixes.md))

## Context

In the dark theme the accent is a light blue (`#79B8FF`) and text on it is near-black (`#0B1A2E`). The user finds
the text on filled blue buttons hard to read.

## Decision

- New theme tokens `accentFill` / `onAccentFill` for filled controls (highlighted buttons, checked segments and
  toggles): dark theme `#1D63D8` with `#FFFFFF`; light theme unchanged (`#0B5FCC` / `#FFFFFF`). Contrast of the text
  at least 4.5:1, checked by a test.
- `accent` stays for text, icons, borders and focus rings on the dark background (the darker blue would be too dark
  there).

## Consequences

- Every filled accent control uses `accentFill`; screenshots change in the dark theme.
