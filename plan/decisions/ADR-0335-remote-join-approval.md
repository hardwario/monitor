# ADR-0335 — Remote session: the host approves every join

- **Status:** Accepted
- **Date:** 2026-11-09
- **Decided by:** Matej Strnad (remote support session planning, plan [0044](../features/0044-remote-support-session.md))

Supersedes the "commands run without a prompt" part of ADR-0324. Its operation confirmation stays.

## Context

The relay does not say which socket sent a `write`, and it cannot disconnect a single client. The only signal is
`newClient`, which reaches the host whenever anyone connects to the namespace.

## Decision

- On every `newClient` except the one caused by the host's own connect, the host:
  1. stops passing remote `write` lines to the device. Lines that arrive meanwhile are dropped and logged, not queued;
  2. does not send `sync` (no history for the newcomer);
  3. asks: *"Someone joined session 482 913. Allow them to send commands to the device? Allow only if HARDWARIO
     Support has just told you they are joining."* with [Allow] and [End session]. End session is the default button.
- Allow → sends `sync` and the Monitor hello (ADR-0336), then resumes remote commands.
  End session / no answer in 2 minutes → the session ends. Because the relay cannot remove one client, refusing a
  join means ending the session. The customer can start a new one with a new code.
- While a prompt is open, commands from already approved participants are paused too.
- A participant that reconnects after a network drop must be approved again.
- After approval, commands run without a prompt, marked **remote**. Flash, erase, recover and reset still need
  confirmation each time (ADR-0324).
- CLI host: the same prompt on stdin. Without a tty, joins are never approved, so the session is view-only for its
  owner and ends at the first join.

## Consequences

- A guessed code gives no command access without the customer's click.
- Known limit: the relay broadcasts the live `at` stream to every socket. A participant who is not yet approved
  (or refused, until the session ends) still sees the live console output from the moment they connected. The
  share dialog says this.
