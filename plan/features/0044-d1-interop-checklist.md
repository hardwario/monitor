# 0044 D1 — Interop checklist (remote support session)

Manual verification of plan [0044](0044-remote-support-session.md) step D1 with the real relay and web Terminal. Run it
before 2.1 and before every later Monitor release (plan 0044 § Risks). Tick the boxes in a copy of this file in the
release notes or the HIL results.

## 0. Setup

- Monitor: a Debug build (`cmake --preset linux-debug`) for the local relay (http on loopback is accepted only by
  builds with `HWIO_TEST_OVERRIDES`), a release build (or the package) for terminal.hardwario.com.
- Local relay: `tools/remote/local-terminal.sh` (copies `~/terminal`, builds its Dockerfile, serves
  `http://127.0.0.1:5000`; `docker logs -f hwio-local-terminal` shows the relay's debug log). Stop with
  `tools/remote/local-terminal.sh --stop`.
- Two machines (or two user accounts) for Monitor ↔ Monitor; Chrome with Web Bluetooth for the web Terminal host.
- A CHESTER with a J-Link and one with Bluetooth; a catalog firmware ID for the flash case.

## 1. Local relay (repeatable, automated part done by the implementation)

- [x] `hwio remote host` (fake J-Link) ↔ `hwio remote join --command` through the local real relay
      (`server/index.js` in Docker): code, join approval, command, response. Done 2026-11-10 (see the result in
      the hand-over note).
- [x] The same with real hardware (CLI only): `hwio remote host -n <SN> --relay http://127.0.0.1:5000` ↔
      `hwio remote join`, one machine, one CHESTER (J-Link EDU Mini), HEAD 7276fe4, 2026-11-11: code after the
      collision window; join question answered "y" → `app config show` response at the joiner; `--reset --target app`
      allowed → "reset done" and the CHESTER restarted; denied → `remote.operation_denied` at the joiner; the host's
      audit lines ("— Remote: … —") for every join, command, request and answer.
      **Found:** a live joiner never received the host's tunnel `end` (the real relay drops an event that arrives
      together with the disconnect). Fixed in 0c5101c (ADR-0358); re-checked on the CHESTER: Ctrl+C on the host →
      the joiner ends 0.6 s later with `remote.host_ended`, the relay log shows the `end`.
- [x] GUI host ↔ CLI joiner, same machine and CHESTER, 2026-11-11 (driven over AT-SPI, GUI under XWayland, the
      relay URL put into an isolated settings file): Connect → **Share with support…** → the dialog names the server
      127.0.0.1 → **Start sharing** → banner "Shared with support — code 387 763"; the join dialog "Someone joined your
      session" → **Allow** → the joiner's `app config show` answered; the console file has every "— Remote: … —"
      line; **End sharing** → the joiner ends 0.4 s later with `remote.host_ended`.
- [x] CLI host (CHESTER) ↔ GUI joiner, 2026-11-11 (the code typed by hand, the rest over AT-SPI): **Join support
      session…** → tab "CHESTER — remote, Connected" with the live CHESTER output; the Flash page shows **Remote
      operations** (Ask to flash / erase / recover / reset); **Ask to reset** denied on the host → "The customer
      denied the request."; Ctrl+C on the host → the tab "Disconnected — The remote session ended." and the banner "The
      session ended. The console stays here so that you can save it."
- [x] Relay log (`docker logs`): only `at` / `sync` / `write` are relayed; payloads appear only at debug level
      (level 20). Checked 2026-11-11.

## 2. The four combinations on terminal.hardwario.com

| # | Host | Joiner | Check |
|---|---|---|---|
| 2.1 | Monitor GUI (J-Link) | Monitor GUI | [ ] code readable aloud; join question, **End session** default; Allow → history (sync) + live output; joiner commands run, marked "— Remote: … —" on the host; **End sharing** |
| 2.2 | Monitor GUI (BLE) | web Terminal (Chrome, **Join Session**) | [ ] the web Terminal shows the history and live output, its commands run after Allow; the web Terminal shows no garbage from the `hwio` tunnel messages |
| 2.3 | web Terminal (Chrome, **Create Session**, BLE) | Monitor GUI | [ ] tab "Session NNN NNN — web Terminal"; history and live output; commands reach the device; Flash page says only the console is available; a code under 100000 (leading zero) joins |
| 2.4 | web Terminal | web Terminal | [ ] unchanged behaviour (regression check of the deployed relay) |

## 3. Monitor ↔ Monitor operations (HIL, J-Link CHESTER)

- [ ] Flash a catalog firmware ID: the host's question names the firmware (name, version, ID) and the core, **Deny**
      is the default; Allow → the joiner's progress and result; the host's console file has the request and the result.
- [ ] Erase (app, lte), Recover, Reset (app, lte): each asks, each result is reported by the host.
- [ ] Deny and the 60 s timeout → the joiner shows "The customer denied the request".
- [ ] A second request while one runs or is asked → "busy".
- [ ] A firmware ID that is not in the catalog → `remote.firmware_not_in_catalog`, no question on the host.

## 4. Security and limits

- [ ] A second joiner while one is allowed → the question again; commands of everyone pause until it is answered.
- [ ] **End session** in the join question ends the session for everyone; the code no longer works.
- [ ] A stranger who joins sees the live output (known limit, ADR-0335) but no command reaches the device.
- [ ] Host network drop (Wi-Fi off 30 s) → the banner says it reconnects; afterwards the join question comes again.
- [ ] Idle: no traffic for 28 min → warning with **Keep sharing**; 30 min → the session ends (both sides).
- [ ] Closing the tab, disconnecting the device and quitting end sharing.
- [ ] `hwio remote host` without a terminal on stdin (`< /dev/null`) ends at the first join.
- [ ] Corporate proxy (system proxy configured): the connection uses it.

## 5. All operating systems

- [ ] Linux, macOS, Windows: packages contain Qt WebSockets (`Qt6WebSockets` library next to the app), sharing and
      joining work on each.
