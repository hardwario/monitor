# ADR-0387 — Content dialogs are views of the page area; confirmations stay dialogs

- **Status:** Accepted
- **Date:** 2026-11-17
- **Decided by:** Matej Strnad (plan [0052](../features/0052-views-instead-of-dialogs.md))

## Context

Besides wizards the app has six content dialogs (Report a problem, Update J-Link firmware, Join support session,
Share this session, Choose mode, Keyboard shortcuts) with the same size limits as the wizard dialog, plus small yes/no
confirmations, native file dialogs and popups. The user asked to use the app window instead of dialogs.

## Options considered

1. Wizards and content dialogs become views; confirmations, native file dialogs, menus and tooltips stay (chosen).
2. Wizards only.
3. Everything, confirmations inline.

## Decision

- The six content dialogs open as a view in the page area of the current tab (or over Choose device / Settings),
  with a header (title, × Close) and a pinned footer; closing returns to the previous page. One content view at a
  time app-wide.
- Switching tabs closes an open content view, except one running an operation (report collecting: stays and is
  shown again on return; J-Link firmware update: the switch is blocked, as the dialog could not be closed either).
- `ConfirmDialog` stays a small modal over the view that asked. `AlreadyRunningDialog` and the MCP `ConsentWindow`
  stay separate windows. Native file dialogs, menus, popovers, tooltips and toasts are unchanged.

## Consequences

- `AppViewModel.overlayView` + a page-area Loader; the content components become `*View.qml` items.
- No content is limited to 720 × 560 any more; short confirmations keep their focused, modal behaviour.
