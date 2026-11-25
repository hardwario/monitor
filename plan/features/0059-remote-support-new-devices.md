# 0059 — Remote support session for the new devices (Nordic chip, STICKER)

- **Status:** In progress (approved 2026-11-24, Matej Strnad: "Approve as recommended" — D-1 to D-23 as
  recommended; A0–A4 implemented 2026-11-24, see "Implementation notes")
- **Milestone:** after 2.0.0 (phases ship separately; see "Release order")
- **Author:** Matej Strnad, Claude
- **Related ADRs:** proposed ADR-0414 (protocol capabilities and compatibility), ADR-0415 (console sharing for every
  profile), ADR-0416 (STICKER secrets in a shared console), ADR-0417 (remote operations per device and target),
  ADR-0418 (remote flash of prepared firmware, amends ADR-0323), ADR-0419 (sharing a tab without a console),
  ADR-0420 (STICKER over NFC: not shared in this plan), ADR-0421 (STM32 chip, CHESTER LoRaWAN module and Bluetooth
  devices stay out of scope). Builds on ADR-0322, ADR-0323, ADR-0324, ADR-0329, ADR-0332, ADR-0334, ADR-0335,
  ADR-0336, ADR-0354, ADR-0356, ADR-0363, ADR-0364, ADR-0365, ADR-0368, ADR-0393 (branch `strnad/wizards-rc5`),
  ADR-0395..0404 (plan 0057), ADR-0405..0410 (plan 0058), ADR-0340..0349 / 0411..0413 (plans 0041–0043).
- **Depends on:**
  - 0044 (In progress: D1 interop still open).
  - 0057 (Approved; backend A1–A4 on `strnad/impl-0057-generic-nordic`; CLI / GUI phases B–D not done; **no pull
    request**).
  - 0058 (Approved 2026-11-24, not implemented, no pull request).
  - 0039 (pull request #18, open). STICKER is an STM32WLE5, and 0058 builds on the 0039 framework.
  - **0041** (backend + CLI on `strnad/impl-0041-sticker-nfc-281818`, **no pull request**). It defines the
    `sticker` profile that 0058 amends, so the STICKER console depends on it.
  - 0042 / 0043 only for the NFC evaluation.

  Neither `nrf` nor `sticker` exists on main (`libs/device/src/profiles/` holds only `chester.cpp`).

## Context

Matej Strnad, 2026-11-24: "will it have support for remote control?" — no, none of the new device plans includes it —
"can we do it? And STICKER have console with debug FW, and this should be supported."

The remote support session of plan 0044 shares one connected console tab (J-Link RTT or Bluetooth NUS) through the
web Terminal's relay and lets a Monitor joiner ask for flash / erase / recover / reset, each confirmed by the customer.
It was built when CHESTER was the only device. Since then three plans add devices:

| Device | Plan | Transport | Console | Remote today |
|---|---|---|---|---|
| Nordic chip (`nrf`, nrf52 / nrf91) | 0057 | J-Link | RTT, names or index fallback; Zephyr shell only if the firmware has one | No Nordic tab exists yet (0057 GUI not built). Once one exists, Share would not be blocked, but the session would describe the device as a CHESTER (F16–F19) |
| STICKER over J-Link (`sticker`, target `app`) | 0058 | J-Link (SWD, STM32WLE5) | RTT shell + log **only in debug / provisioning firmware**; release has none | Not implementable yet: 0058 not built |
| STICKER over NFC | 0041–0043 | PC/SC reader, ST25DV mailbox, AES-CCM | none (request / reply) | Not a console session; nothing to share |
| STM32 chip, CHESTER `lrw` | 0039 / 0040 | J-Link | none | out of scope (ADR-0421) |

This plan makes the remote session device-neutral where it must be. It adds the STICKER debug console and the Nordic
chip's console as first-class shared consoles, defines which operations each device allows remotely, and evaluates the
STICKER over NFC. It keeps full compatibility with the unchanged relay, the web Terminal and Monitor 2.0.0-rc.4 / rc.5
peers.

### What in 0044 is CHESTER-specific (verified, see "Verified facts")

- **Protocol.** `op-request.target` is the enum `app | lte` (F3). An unknown target fails decoding, and the host then
  **sends no answer** (F8), so the joiner waits out its 90 s response timeout (F10). The host hello describes the device
  with `name / serial / transport / firmware` and `ops` / `opKinds` only. It has no profile and no targets (F4, F14).
  Every error code in `op-denied`, `op-result` and `end` must be one the receiver knows, or it rejects the message (F6).
  `v` must be 1 (F5).
- **Operation mapping.** Erase is `EraseScope::Code` on `app` and `EraseScope::All` on any other target. Reset is
  `ResetMode::Go`. Flash is a catalog ID on `app` only (F15). The host advertises all four kinds whenever it has a
  J-Link session (F16), and the GUI builds operations for any J-Link tab (F19).
- **Wordings.** The customer's question and the support-side results say "the application core", "the LTE modem core",
  "LTE MCU" and "the customer's CHESTER" (F12, F13). The joiner's Flash card has exactly two target buttons,
  "Application core" and "LTE modem core", plus a catalog-ID field (F20).
- **Profile on the joiner side.** A remote tab always uses the `chester` profile for its console rules (F17).
- **CLI host.** `hwio remote host` opens the console with the CHESTER app options, and its operations use the CHESTER
  catalog (F21, F22). `hwio remote join --target` accepts only `app` / `lte` (F23).
- **Command tree.** Learning is generic Zephyr shell (`help`, `<cmd> -h`, ADR-0178). It is tied to CHESTER only through
  the `info show` cache key (F24).
- **Secrets toward the relay.** Redaction is per profile and target (F25). Output redaction matches only `key: value`
  lines (F27). CHESTER has entries (F26); other profiles have none.

The transport side needs nothing new. J-Link RTT sharing runs on any `DeviceSession`, which is a `ConsoleSession`
(F28). Bluetooth NUS sharing works for any `ConsoleSession` too, but only CHESTER has a Bluetooth profile. The web
Terminal is a Bluetooth CHESTER host or a console-only joiner. As a joiner it works with any shared console unchanged.

## Verified facts

| # | Fact | Source |
|---|---|---|
| F1 | The relay forwards only `at`, `sync` and `write`, broadcast to every other socket in the namespace, and drops other events. The host cannot tell who sent a `write`. | plan 0044 §Context; `libs/remote/include/hwio/remote/tunnel.h:19-20` |
| F2 | Tunnel envelope: host `at {event:"hwio", data:{v:1,type,…}}`, joiner `write {hwio:{v:1,type,…}}`. | `tunnel.h:3-8` |
| F3 | `enum class OpKind {Flash, Erase, Recover, Reset, LteUpdate}`; `enum class OpTarget {App, Lte}`. | `tunnel.h:66-68` |
| F4 | `HostHello {app, version, device{name, serial, transport, firmware}, ops, opKinds, opQuestions}`; `JoinerHello {app, version}`; `OpRequest {id, kind, target, firmwareId?, questions}`. | `tunnel.h:82-100, 142-152` |
| F5 | Any `v` other than 1 is rejected ("unsupported version"). | `libs/remote/src/tunnel.cpp:105-107` |
| F6 | `errorCode()` rejects a code that is not a known `hwio::Error` id. It is used by `op-denied`, `op-result` and `end`. An older peer therefore rejects any error id added after its release. | `tunnel.cpp:70-80, 180-186, 206-224, 642-647` |
| F7 | The host hello decoder reads only known keys, so extra keys are ignored. Unknown `opKinds` entries are skipped. A hello without `opKinds` means the four kinds of ADR-0322. Text fields may be empty. | `tunnel.cpp:124-178, 46-60` |
| F8 | Host: a joiner tunnel object that fails decoding (unknown target, unknown kind, flash without `firmwareId`) only emits `warning`. **No `op-denied` is sent.** | `libs/remote/src/remote_host.cpp:455-460`; `tunnel.cpp:281-303` |
| F9 | A decoding failure on the joiner emits `warning`, which the GUI shows as the tab's connection warning. An unknown host message type is such a failure. | `libs/remote/src/remote_console_session.cpp:261-266`; `tunnel.cpp:666`; `app/gui/src/viewmodels/device_tab_view_model.cpp:616-617` |
| F10 | Joiner operation timeouts: 90 s until `op-accepted` / `op-denied`, then 300 s between progress messages. | `libs/remote/include/hwio/remote/remote_operation_client.h:35-36` |
| F11 | The host sends its hello on allow and again when the firmware identity changes. The hello name is the profile display name (J-Link) or the advertised name (BLE). Serial and firmware come from `adoptedIdentity()`. | `remote_host.cpp:187, 340, 359-384` |
| F12 | Customer-side question texts: "the application core" / "the LTE modem core"; erase: "The device stops working until new firmware is flashed"; recover: "erases its flash completely, including the protection settings". | `libs/remote/src/consent.cpp:38-42, 225-247` |
| F13 | Result and audit texts name "LTE MCU", "application MCU" and "The application on the customer's CHESTER will be erased". | `consent.cpp:112-214` |
| F14 | `ops = m_operations != nullptr`; `opKinds = m_operations->kinds()`. | `remote_host.cpp:375-381` |
| F15 | Operation mapping: flash `app` only (catalog); erase `Code` on `app`, else `All`; recover; reset `ResetMode::Go`; target validation is `findTarget(profile, "app"/"lte")`. | `libs/remote/src/host_operations.cpp:81-100, 198-225` |
| F16 | `kinds()` always contains flash / erase / recover / reset (+ `lteUpdate` when the profile's `lte` target has designated firmware). | `host_operations.cpp:60-79` |
| F17 | `RemoteConsoleSession::profile()` returns the `chester` profile ("The remote device is a CHESTER (the only profile …)"). | `remote_console_session.cpp:178-181` |
| F18 | GUI Share is available for any connected console tab that is not remote. No profile check is made. | `app/gui/src/viewmodels/remote_share_view_model.cpp:50-54` |
| F19 | GUI host: any tab with a J-Link `DeviceSession` gets `DeviceSessionOperations` built with that profile's catalog URL. | `remote_share_view_model.cpp:95-115` |
| F20 | The joiner Flash card has a target toggle with two buttons, "Application core" and "LTE modem core". It also has the catalog-ID field "Ask to flash" (always `app`), "Ask to update LTE firmware", erase, recover and reset. | `app/gui/qml/remote/RemoteFlashCard.qml:22, 59-151` |
| F21 | CLI host: J-Link via `appConsoleSessionOptions` (CHESTER `profileId = "chester"`), operations with `chesterCatalogOptions()`. | `app/cli/src/remote_commands.cpp:275-330, 538-548`; `app/cli/src/chester_commands.cpp:64, 99-105` |
| F22 | CLI host learns the tree with `ConsoleLearning(session, profile().id, consoleTargetId(), …)`. | `remote_commands.cpp:556-563` |
| F23 | `hwio remote join --target` is `CLI::IsMember({"app","lte"})`. The joiner maps it with `tunnel::targetFromId`. | `remote_commands.cpp:764-765`; the option is in the join command's setup at the end of the file (`grep -n 'IsMember({"app", "lte"})'`) |
| F24 | The `CommandTreeLearner` learns with `help` and `<path> -h` when the shell supports `-h`. Its cache key comes from `info show`. | `libs/device/include/hwio/device/command_tree_learner.h:3-16` |
| F25 | The remote host redacts commands by `sensitivePrefixesOf(profile, consoleTarget)` and output by `sensitiveOutputKeysOf`. Redaction covers what goes to the relay: the `at` write / read lines and the `sync` transcript. | `remote_host.cpp:805-823, 858-877` |
| F26 | CHESTER `app` sensitive commands: `lrw config appkey/nwkskey/appskey`, `lte config username/password`, `ble config passkey`. Sensitive output: `claim token`, `ble passkey`. | `libs/device/src/profiles/chester.cpp:150-157, 186-187` |
| F27 | `matchSensitiveOutput` needs a `key:` before a colon. A line without a colon is never redacted. | `libs/device/src/profile.cpp:130-160` |
| F28 | `class DeviceSession final : public ConsoleSession`. | `libs/device/include/hwio/device/device_session.h:173` |
| F29 | `TargetProfile` has `rttTerminal` ("" = no RTT console), `rttLogger`, `shellPrompt` and `designatedFirmware`. | `libs/device/include/hwio/device/profile.h:63-107` |
| F30 | `at` events (state / write / read / tunnel) reach every socket in the namespace, including participants not yet allowed. Only `sync` waits for the allow. | plan 0044 §Wire protocol; ADR-0335 |
| F31 | The branches `strnad/impl-0057-generic-nordic` and `strnad/impl-0039-stm32-ccb9fd` do not touch `libs/remote` or `remote_share_view_model.cpp`. | `git diff --stat origin/main <branch> -- libs/remote app/gui/src/viewmodels/remote_share_view_model.cpp` (empty) |
| F32 | On `nrf`, targets are `nrf52` and `nrf91`, plus unsupported families. The catalog URL is the CHESTER catalog. There is no `app` target. The RTT index fallback applies to `nrf` only. | `strnad/impl-0057-generic-nordic:libs/device/src/profiles/nrf.cpp:214-236`; 0057 ADR-0398 |
| F33 | STICKER debug firmware: Zephyr shell on RTT buffer 0, log on buffer 1, empty prompt, help on (`help`, `-h`), `SHELL_MINIMAL`. Release (`prj.conf`) has no shell, RTT or console, and has `CONFIG_PM=y`. | `~/src/sticker-firmware/app/debug.conf:12, 37-58, 74`; `app/prj.conf:31` |
| F34 | STICKER debug `config show` prints `config secret-key <hex>`, `claim-token`, `vendor-token`, `lrw-nwkkey`, `radio-appkey`, `lrw-nwkskey` and `lrw-appskey` **without a colon**. The same `config <key> <value>` commands set them. | `sticker-firmware/app/src/app_config.c:616-631, 745-747, 915-945, 1273-1300, 2276-2380` |
| F35 | STICKER debug `settings erase` erases the whole NVS, identity included. `settings vendor-reset <new-key>` re-provisions the key. `settings factory-reset` drops LoRaWAN. `nfc write / clear / regw` write the tag EEPROM / registers. | `sticker-firmware/app/src/app_settings.c:232-252`; `app/src/app_nfc.c:2178-2200` |
| F36 | STICKER debug auto-suspend: after `APP_DEBUG_AUTOSUSPEND_S` (default 7200 s) without RTT / shell activity, the device enters Shutdown. Wake needs NRST. | `sticker-firmware/app/Kconfig:84-90`; `app/debug.conf:76-79` |
| F37 | 0058: target `app` on `sticker`. `designatedFirmware` is STICKER 2.0.0 release (pinned URL + SHA-256), with debug and provisioning as Expert-mode alternatives (D7). `preserved` storage range. `EraseScope::Code` keeps identity. Erase-all / unlock carry `destroysIdentity`, are Expert-only and are refused over MCP. There is no "recover" for STICKER. 0058 says nothing about remote. | plan-0058 worktree `plan/features/0058-sticker-jlink-flashing.md:196-278, 324`; ADR-0407, 0408, 0409 |
| F38 | 0041 MCP rule: read-only STICKER tools; "Secret keys and vendor tokens are never accepted or returned over MCP; LoRaWAN key fields are redacted". | `strnad/impl-0041-sticker-nfc-281818:plan/decisions/ADR-0347*:9-17` |
| F39 | NFC: the tag lies on the reader for the whole session. The firmware ends its session after 3 s idle. Monitor cycles the field after 110 s. Counters are per session and move forward. The key CSV is plaintext on disk and is held in memory as a zeroised `SecretKey`. | `strnad/impl-0041-sticker-nfc-281818:plan/features/0041-sticker-nfc-backend-cli.md:81, 276-285, 311-313`; ADR-0344 |
| F40 | The local-device wizards refuse a remote tab (ADR-0393, branch `strnad/wizards-rc5`, pull request #19 open). | `strnad/wizards-rc5:plan/decisions/ADR-0393-local-device-wizards-refuse-remote-tabs.md` |
| F41 | No remote MCP tools (ADR-0332). Remote lines are visible to MCP with the remote origin. | `plan/decisions/ADR-0332-mcp-no-remote-session.md` |
| F42 | Open pull requests that matter here: #18 (0039/0040 STM32), #19 (wizards as views, ADR-0393), #2 (0044 draft; the code is on main). There is **no** pull request for 0041, 0057 or 0058. | `gh pr list --state open` (2026-11-24) |
| F43 | MCP in the GUI lists a tab only through `deviceId()`: the J-Link probe serial, or the Bluetooth id. A remote-support tab has no probe ("has no probe", ADR-0393 context), so a joiner's remote tab is **not exposed to AI tools**. The customer's shared local tab stays an ordinary MCP device. Remote lines carry the remote origin (ADR-0332). | `app/gui/src/mcp/gui_device_provider.cpp:166-177, 263-272` |
| F44 | The STICKER `config` setters also include `serial-number` and `nonce-counter` (both "Get/Set"). The firmware marks claim and vendor tokens "write-once at commissioning". | `sticker-firmware/app/src/app_config.c:2276-2294` |
| F45 | The STICKER debug configuration does not enable `CONFIG_SHELL_CMDS_SELECT` / `SHELL_WILDCARD` (not in `debug.conf`). `CONFIG_APP_CMD_DEBUG_SHELL=y` adds the debug subcommands of `ats`. | `sticker-firmware/app/debug.conf:114`; `app/src/app_ats.c:1068-1450, 1744` |
| F46 | Tags `v2.0.0-rc.2` … `v2.0.0-rc.4` exist, so the rc.4 baseline error set and decoder can be taken from the tag. | `git tag -l 'v2.0.0*'` |
| F47 | No GUI code on the 0057 branch selects the `nrf` profile yet. | `git grep '"nrf"' strnad/impl-0057-generic-nordic -- app/gui` (empty) |
| F48 | 0057: a nRF91 PIB write starts with a recover, because the PIB lies in UICR OTP and a recover erases the UICR. So a recover loses the PIB. | `strnad/impl-0057-generic-nordic:plan/decisions/ADR-0400-nrf91-pib-write.md` (Context) |

Facts F32, F33, F37, F38, F39 and parts of F34 were collected by research subagents from the cited files. F34, F35,
F36, F44 and F45 were re-checked directly in the sticker-firmware source. The others were checked only through the
cited lines in the subagent reports.

### Assumptions not verified

| # | Assumption | How to verify |
|---|---|---|
| U1 | The STICKER debug firmware's RTT buffers are named `Terminal` / `Logger` (0058 U8). If they are not, the 0057 index fallback (nrf only) does not apply to `sticker`. | 0058 Phase 0 P0.6 |
| U2 | A J-Link tab whose target has no RTT control block (STICKER release, bare chip without RTT) never reaches `ConnectionState::Connected`, so Share stays disabled. | read `DeviceSession` connect path / fake J-Link test |
| U3 | J-Link reset (NRST over JP1) wakes a STICKER debug unit after auto-suspend (0058 U5). | hardware |
| U4 | Shell input or RTT reads by the host count as "activity" for the 7200 s auto-suspend. | sticker-firmware source of `CONFIG_APP_DEBUG_AUTOSUSPEND_S` users |
| U5 | The host can tell debug from provisioning firmware, for example from the adopted identity or the image digest last flashed. | 0058 implementation |
| U6 | No other STICKER debug command prints secrets beyond those in F34 / F35. `ats`, `settings show`, `history` and `CONFIG_APP_CMD_DEBUG_SHELL` were not audited. | audit in step A3 |
| U7 | A bare Nordic chip with a non-Zephyr firmware does not answer `help` in a way the learner takes for a tree. That is 0057's "existing behaviour, unchanged". | 0057 Phase F |
| U8 | Monitor 2.0.0-rc.5 has the same tunnel decoder as main (ADR-0368 included). | `git show v2.0.0-rc.5:libs/remote/src/tunnel.cpp` once tagged |
| U9 | ADR-0322 / ADR-0323 / ADR-0324 texts beyond their Decision sections were not re-read in full. The decisions quoted here come from their Decision sections. | — |
| U10 | Why ADR-0410 asks for "disconnect the J-Link and reinsert the battery" after a flash. The assumption is that the STM32WL stays in debug / low-power-debug state while the probe is attached. It may matter only for release firmware, which sleeps. | 0058 Phase 0 |
| U11 | Zephyr's `SHELL_CMDS_SELECT` defaults to off under `SHELL_MINIMAL`, so `select config` cannot bypass a prefix refusal. Also, no shell built-in other than `help` re-dispatches a command line. | Zephyr Kconfig of the pinned SDK |
| U12 | `DeviceTabViewModel::probeSerial()` is empty for a remote tab, which F43 relies on. | read `device_tab_view_model.cpp` |

## Goals / Non-goals

**Goals**
- A customer shares the console of a **Nordic chip** tab and of a **STICKER (J-Link) tab with debug firmware** exactly
  as a CHESTER console: same code, same join approval, same banner, same audit and same limits.
- Support sees the right device and the right target names, and gets autocomplete for a Zephyr shell (STICKER debug).
- Support can ask for the operations that make sense per device (ADR-0417). For STICKER that includes the **pinned
  release or debug firmware** (ADR-0418). The customer confirms each one, with a text that says what is lost and what
  is kept.
- STICKER secrets never reach the relay in a shared console, and a remote participant cannot change or destroy the
  STICKER's identity (ADR-0416).
- Old peers (Monitor 2.0.0-rc.4 / rc.5) and the web Terminal keep working and degrade to "console only" with a clear
  message. They never hit a 90 s silent timeout (ADR-0414).
- The CLI matches the GUI: `hwio remote host` for the new devices, and `hwio remote join` with any target and prepared
  firmware.

**Non-goals**
- STICKER over NFC in a remote session (ADR-0420). It is evaluated below and recommended for a later plan.
- The STM32 chip (0039), the CHESTER `lrw` target (0040) and Bluetooth consoles of non-CHESTER devices (ADR-0421).
- Remote flash of a file or URL for a bare Nordic chip (recommended: none in this plan; ADR-0417 option list).
- Remote PIB / UICR writes, `modem-flash`, STICKER erase-all / unlock, and the provisioning firmware.
- Any relay or web Terminal change, end-to-end encryption, or MCP over the relay (ADR-0334, ADR-0329, ADR-0332).

## User experience

### Host (customer)

Unchanged flow (0044 §Host, plan 0052: the share and join screens are views; the join and operation **confirmations
stay dialogs**):

- **Nordic chip tab** (console connected): **Share with support…** works as today. The share view names the device
  ("Nordic chip nRF52840, serial …"). The operation sentence reads "ask to erase the firmware or reset the chip (each
  needs your confirmation)". With D-11 (a) it also says "recover". It does not mention flashing (D-12).
- **STICKER tab with debug firmware:** Share works. The share view adds one line (D-9): *"Monitor hides the STICKER's
  secret key, tokens and LoRaWAN keys from support, and refuses support's commands that change them or erase the
  STICKER's identity. Debug firmware has many test commands; share only with HARDWARIO Support."* It promises only
  what the lists of ADR-0416 cover (U6). The operation
  sentence: "ask to flash the STICKER release or debug firmware, erase the firmware (your STICKER keeps its identity),
  or reset it".
- **STICKER tab with release firmware** (no console): with D-17 = yes (phase B), the Flash page shows **Share with
  support…** too. The banner says *"Shared with support — code 482 913. This firmware has no console; support can
  ask to flash firmware or reset the STICKER."* With D-17 = no, the tab has no Share entry. Then support asks the
  customer to flash the debug firmware locally, which needs Expert mode (0058 D7).

Operation questions (Deny default, 60 s, as ADR-0324), by device:

| Device / request | Question text |
|---|---|
| Nordic, erase `nrf52` | "Support asks to erase the firmware of the nRF52840 chip. The UICR is kept. The chip stops working until new firmware is flashed." |
| Nordic, recover `nrf91` (only with D-11 (a)) | "Support asks to recover the nRF9160 chip. This erases its flash and UICR completely, including the protection settings and the PIB." |
| Nordic, reset | "Support asks to reset the nRF52840 chip. It restarts." |
| STICKER, flash release | "Support asks to flash STICKER firmware 2.0.0 (release). The STICKER keeps its identity, keys and settings, and restarts. Afterwards disconnect the J-Link and reinsert the battery." |
| STICKER, flash debug | "Support asks to flash STICKER firmware 2.0.0 (debug, with console). The STICKER keeps its identity, keys and settings. Debug firmware never sleeps: it drains the battery faster, and it switches itself off after 2 hours without console activity. Ask support to flash the release firmware back when you are done." |
| STICKER, erase | "Support asks to erase the STICKER's firmware. Its identity, keys and settings are kept. The STICKER stops working until firmware is flashed again." |
| STICKER, reset | "Support asks to reset the STICKER. It restarts." |

The model names come from the identified part (`nRF52840`, `nRF9160`). When the part is unknown, the family display
name is used.

A refused remote command (STICKER, D-10) appears as an audit line: *"— Remote: a command that changes the STICKER's
keys or identity was refused (config secret-key …) —"*. Support's console shows the line *"The customer's Monitor
refused this command: it changes the device's keys or identity."*

### Joiner (support)

- The tab title comes from the hello's display name: "Nordic chip nRF52840 1234… — remote" or
  "STICKER 2159018247 — remote".
- The console uses the host's profile console rules (D-6) and the host's command tree (D-8).
- The **Flash page › Remote operations** card is built from the host's `targets` (D-16, D-20). It shows:
  - one target button per target, named by the joiner's own translation of the profile target (D-5);
  - for each target, only the operations the host lists;
  - a **firmware chooser** with the host's prepared firmware ("STICKER 2.0.0 release" / "STICKER 2.0.0 debug") instead
    of the catalog-ID field when the target offers prepared firmware and no catalog;
  - the catalog-ID field and "Ask to update LTE firmware" only where the host lists them (CHESTER as today).
- An old host (no `targets`) shows the card exactly as in 0044.
- The connect, flash and troubleshooting wizards keep refusing a remote tab (ADR-0393, F40). This is consistent: the
  new devices' remote tabs are remote tabs like any other, and their local wizards (0057 / 0058 connection guides,
  the ADR-0410 wake retry) run only on the customer's side. Nothing changes in ADR-0393.
- MCP: a joiner's remote tab has no probe id and is not listed to AI tools (F43). This plan keeps it so (D-22).
  *Note 2026-11-24:* D-22 is amended by plan [0062](0062-mcp-remote-session.md) and ADR-0433: a joined remote tab is
  available to AI clients as `remote:<n>` when support turns on the setting "AI clients may use remote support
  sessions".
- A new host whose device an old joiner cannot handle sends `ops: false` (D-3). The old joiner's Flash card then
  shows its existing "only the console is available" state.

### Error messages (new; all via `hwio::Error`, translated; see ADR-0414 for what old peers receive)

| Code | What / why / what to do |
|---|---|
| `remote.target_unsupported` | The customer's device has no such target, or this operation is not available on it remotely. The device or its firmware does not support it in a support session. Choose another operation or ask the customer to do it locally. |
| `remote.command_refused` | The customer's Monitor refused this command. It would read or change the device's keys or identity, which is not possible in a support session. Ask the customer to run it locally if it is really needed. |
| `remote.no_console` | (customer side only: Share on a tab without a console with D-17 = no, or `hwio remote host` without `--no-console`) This firmware has no console to share. Flash firmware with a console (debug), or share without a console. |
| `remote.console_unavailable` | (support side: a command sent to an operations-only session) The customer's device has no console with this firmware. Ask for the debug firmware first. |

`remote.firmware_not_in_catalog` stays the answer to a catalog ID on a target without a catalog. Its old text still
fits.

## CLI

```
hwio remote host [--profile chester|nrf|sticker] [--target ID]      # NEW; default chester / its console target
                 [device selection as in `chester app console`, `nrf …` (0057), `sticker app console` (0058)]
                 [--relay URL] [--deny-operations] [--json]
hwio remote join <code|link> [--target ID]                          # any id; checked against the host's hello
                 --flash <catalog-id> | --flash-prepared <release|debug> | --erase | --recover | --reset | --lte-update
```

- `--profile nrf` takes the family from FICR as `hwio nrf` does (0057). `--target` names the family only to override
  it. `--profile sticker` uses target `app` and needs the debug firmware's console. With D-17 = yes,
  `--no-console` shares operations only.
- The join side checks `--target` and `--flash-prepared` against the host's hello and exits with the usage code before
  it sends anything ("The customer's device has targets: nrf52"). For an old host (no `targets`), `app` / `lte` are
  assumed as today.
- `--json` adds `profile` and `targets` to the hello object it already prints.
- Exit codes are unchanged (0006). A refused command in an interactive join prints the support-side line above.

Alternative (D-21): per-device subcommands (`hwio nrf remote-host`, `hwio sticker app remote-host`). Not recommended,
because one `remote` group is easier to document.

## Design

### Protocol: capabilities instead of a new version (ADR-0414)

`v` stays **1**, because every older peer rejects any other value (F5). All additions are **optional fields of
existing messages**, which older peers ignore (F7). A new host message type appears only behind a capability, since it
shows a warning in older joiners (F9).

**Host hello additions**

```json
{ "type": "hello", "app": "hwio-monitor", "version": "2.1.0",
  "device": {"name": "STICKER", "serial": "2159018247", "transport": "jlink", "firmware": "STICKER 2.0.0 debug"},
  "ops": false, "opKinds": [],
  "caps": ["targets", "prepared", "detail-codes", "refused-lines"],
  "profile": "sticker",
  "console": true,
  "targets": [ {"id": "app", "kinds": ["flash", "erase", "reset"], "catalog": false,
                "prepared": [ {"id": "release", "name": "STICKER", "version": "2.0.0"},
                              {"id": "debug",   "name": "STICKER", "version": "2.0.0"} ]} ] }
```

- `caps` is a list of strings, at most 16 entries of at most 32 bytes each, in `[a-z0-9-]`. Unknown entries are
  ignored.
- `profile` is the device profile id (`chester`, `nrf`, `sticker`), at most 32 bytes.
- `console` is false only for an operations-only session (ADR-0419).
- `targets` has at most 8 entries. Each entry has:
  - `id`: the profile target id, `[a-z0-9-]`, at most 32 bytes;
  - `kinds`: from the `OpKind` ids;
  - `catalog`: whether a catalog firmware ID is accepted;
  - `prepared`: at most 4 entries `{id, name, version}`.
- **Legacy fields** `ops` / `opKinds` carry only what a 2.0.0-rc.4 joiner can request correctly. On a CHESTER they are
  exactly as today. On any other profile they are `ops: false` (D-3), so an old joiner offers console only and never
  sends a request that would hit F8.

**Joiner hello addition.** It gets `caps`, the same format. An older host ignores it (it reads `app` / `version`
only). The host records the union of the caps seen this session, for the audit line only.

**op-request.** `target` becomes a **string id**. A joiner sends a target other than `app` / `lte`, or the new field
`prepared: "<id>"` (flash without `firmwareId`), **only when the host hello lists the target / the `prepared` cap**.
An older host therefore never receives something it cannot decode (F8). The host validates the request against its
own profile and the operations matrix (ADR-0417), whatever the joiner's hello claimed. In C++, `OpTarget` becomes a
validated `std::string` (bounded charset) with helpers for the two CHESTER ids. `OpRequest::firmware` becomes
`std::variant<std::monostate, CatalogId, PreparedId>`.

**Error codes toward possibly old peers.** Caps are **per session, not per peer**: the relay broadcasts, and the host
learns neither who sent a `write` nor that a participant left (F1). So every `op-denied`, `op-result` and `end` carries:

- `code`: a code from the **2.0.0-rc.4 baseline set** (a frozen list in `libs/remote`, checked by a unit test);
- optionally `detail: {"code": "<any current hwio::Error id>"}`.

A new joiner shows `detail.code` when it knows that id. An old joiner shows `code`. The mapping from a new id to a
baseline code is by category (step A1 table), for example `remote.target_unsupported` → `remote.operation_denied`,
and `target.*` / `firmware.*` / `sticker.*` ids added after rc.4 → the nearest rc.4 id of the same category. With
this, an old joiner never waits 300 s for a result it rejected (F6). An `end` with a new reason still ends its
session.

**Refused lines.** When the host refuses a remote command line (ADR-0416), it sends
`at {event:"hwio", data:{v:1, type:"line-refused", code:"remote.command_refused"}}` **only when the session's last
joiner hello listed `refused-lines`**. This is the one new message type. Otherwise it sends nothing new. It writes
the audit line, and the line also travels as an ordinary `at read` text that the web Terminal and old joiners show:
"(HARDWARIO Monitor: the command was refused …)", in English, as the other host-generated relay texts.

**Compatibility matrix**

| Host \ Joiner | web Terminal | Monitor rc.4 / rc.5 | Monitor with 0059 |
|---|---|---|---|
| web Terminal (BLE CHESTER) | as today | as today | as today (no hello: console only) |
| Monitor rc.4 / rc.5 (CHESTER only) | as today | as today | console + CHESTER ops. The joiner sends only `app` / `lte` and catalog IDs, because the host hello has no `targets` |
| Monitor 0059, CHESTER | as today | as today; new error ids travel as baseline `code` | full |
| Monitor 0059, Nordic / STICKER | console only | **console only** (`ops: false`). The Flash card says the customer's application supports only the console. The text names the web Terminal, which is acceptable and documented | full |

### Console sharing for every profile (ADR-0415)

- `RemoteShareViewModel::available()` and `hwio remote host` stay generic: any connected console tab. No Nordic or
  STICKER tab exists yet (F47). As soon as 0057's GUI phase D or 0058 adds one, it could be shared, with the faults
  F16–F19. **Step A0** therefore gates Share to the `chester` profile until phase A2 of this plan lands for that device (D-7). It is a
  one-line guard, given to 0057 phase D and 0058 if they ship first.
- Host hello: `profile`; the device `name` is the profile display name plus the part ("Nordic chip nRF52840"); the
  serial and firmware come from the adopted identity when there is one. Empty strings are valid (F7).
- Joiner: `RemoteConsoleSession::profile()` returns the profile named by the hello when the joiner knows it. Otherwise,
  and before the hello, it returns a new built-in **`generic` console profile**: no prompt assumption, no sensitive
  rules, Zephyr log-level parsing. CHESTER rules are no longer applied blindly (F17). The view's console rules
  (prompt, log channel classification, hexdump lines of 0046) follow.
- Command tree: the host feeds its learner's tree, as for CHESTER (F22, ADR-0364). STICKER debug has a Zephyr shell
  with help on and an empty prompt (F33), so the learner's `help` and `-h` work. The 300 ms silence rule of ADR-0175
  ends each query. A Nordic chip learns only if its firmware answers like a Zephyr shell (0057, U7). `learn` requests
  for an unknown path are dropped as today.
- The STICKER debug auto-suspend (F36) is not touched by the session. The idle limit of 30 min is shorter than the
  default 7200 s. If the console goes silent after a long session, support can ask for a reset, which wakes it if U3
  holds.

### STICKER secrets (ADR-0416)

- The `sticker` `app` target gets `sensitiveCommandPrefixes` for every secret `config` key in F34 (`config secret-key`,
  `config claim-token`, `config vendor-token`, `config lrw-nwkkey`, `config radio-appkey`, `config lrw-nwkskey`,
  `config lrw-appskey`) and for `settings vendor-reset`. `config serial-number` and `config nonce-counter` are not
  secret (the serial is public, the counter is sent in clear in every NFC frame), but their setters break NFC access.
  They go to the refused list below, not to redaction. Command history, the console file and the relay then redact
  their arguments (existing mechanism, F25).
- **New output rule kind** `sensitiveOutputPrefixes` (word prefix, no colon). `config secret-key <hex>` is printed
  without a colon (F34, F27). Lines starting with those words are sent as `config secret-key <redacted>`, to the relay
  and to the console file. This applies **whoever typed the command, the customer included**, because `at read` lines
  reach every participant in the namespace, also the ones not yet allowed (F30).
- **Refused from remote (D-10):** on profiles marked `refuseRemoteSensitive` (STICKER only), a remote line is not
  sent to the device when:
  - it sets a sensitive value (a sensitive prefix plus an argument);
  - it sets `config serial-number` or `config nonce-counter`;
  - or it is one of the identity-destroying commands `settings erase`, `settings vendor-reset`,
    `settings factory-reset`, `nfc write`, `nfc clear`, `nfc regw`.

  This list lives in the profile, as `remoteRefusedCommands`. Matching uses the same word-wise, whitespace-normalised
  rule as `matchSensitiveCommand`. Reading a key without an argument is allowed, and its output is redacted. CHESTER
  keeps today's behaviour (D-10).
- **Strength of the refusal.** It is a deny-list on the command words. It holds only while the shell has no way to
  run a command under another name: no `select` (F45, U11), no aliases, and no command that executes a stored line.
  The alternative is an **allow-list** of remote commands for STICKER (D-10 option d). It is safer, but every new
  firmware command then needs a profile change. U6: the lists are complete only for the audited commands. The plan's
  test list includes running `config show`, `settings …` and `ats …` on a debug unit and checking that no secret
  leaves. The share text therefore names what is hidden instead of promising "everything".

### Remote operations per device (ADR-0417, ADR-0418)

The host builds `targets` from a **remote operations matrix** in the profile. The matrix is new data on
`TargetProfile`: `remoteKinds`, `remoteEraseScope`, `remoteFlash` = none / catalog / prepared. It replaces the
hard-coded F15 / F16 logic.

| Profile / target | flash | erase | recover | reset | lteUpdate |
|---|---|---|---|---|---|
| `chester` / `app` | catalog | `Code` | yes | `Go` | — |
| `chester` / `lte` | — | `All` | yes | `Go` | yes (ADR-0363) |
| `chester` / `lrw` | — | — | — | — | — (ADR-0421) |
| `nrf` / `nrf52`, `nrf91` | **none** (D-12) | `Code` (keeps UICR, 0057 D15a) | **no** (D-11 (b); yes with (a)) | `Go` | — |
| `sticker` / `app` | **prepared: release, debug** (D-13) | `Code` (keeps `storage`, 0058 ADR-0407) | **never** | `Go` | — |

- Never remote, whatever a joiner sends:
  - erase-all or unlock of a target with `destroysIdentity`;
  - PIB / UICR writes;
  - `modem-flash`;
  - the provisioning flavour;
  - `--allow-uicr`.

  These match ADR-0409's MCP refusal.
- Prepared flash (ADR-0418, amends ADR-0323): the request names a `prepared` id from the host's `targets`. The host
  downloads that `DesignatedFirmware` from its pinned URL and checks its pinned SHA-256 through the existing firmware
  cache (0058 ADR-0408). No firmware, URL or digest crosses the relay. The host's UI mode does not gate a remote debug
  flash (D-14), because support is the expert and the customer confirms the text above.
- After-flash step, by flavour:
  - **release:** the STICKER sleeps and has no console, so the session cannot go on. The host shows the ADR-0410
    notice "disconnect the J-Link and reinsert the battery", and support's result line says "flashed; the customer
    must reinsert the battery". With D-17 the session stays an operations-only session until the customer ends it.
  - **debug:** the host resets with `ResetMode::Go` and waits for RTT, up to the tab's normal connect time. When the
    console appears, the hello is re-sent (`console: true`). Only when it does not appear does the host show the
    reinsert notice.

  U10 (why ADR-0410 asks for the reinsert) decides whether the debug case needs the notice at all. The texts in the
  table above say "afterwards disconnect … reinsert" for release only.
- Operations on a STICKER that sleeps (release firmware) follow ADR-0410: connect-under-reset, else the "reinsert the
  battery" guided retry. The retry is shown **on the host only**. Support sees the progress phase `waiting-for-wake`,
  translated on the joiner side.
- Cross-target rules (0040: flashing `lrw` from an `app` tab) do not apply: no remote `lrw`.

### Sharing a tab without a console (ADR-0419, D-17)

For a STICKER with release firmware (no RTT), support's first useful step is usually "flash the debug firmware".
Phase B allows Share on a J-Link tab that has an identified target but no RTT console. Only `sticker` is in scope.
Generic STM32 is excluded by ADR-0421, and a bare Nordic chip without RTT stays excluded.

`RemoteHost` takes a `ConsoleSession` today. An operations-only session uses the tab's `DeviceSession` with
`console: false`. The host sends no `at write` / `read` lines except audit and state lines. Remote command lines are
refused as `remote.console_unavailable`. When the debug firmware is flashed and RTT comes up, the session continues as a
console session. The host re-sends its hello with `console: true` (F11: the hello is re-sent on an identity change).
U2 decides how the connect path reports "connected, no console". Phase B starts with that check.

### STICKER over NFC remotely (ADR-0420) — evaluation

| Option | What support gets | Security | Effort |
|---|---|---|---|
| N0 Not supported (recommended now) | nothing; the customer reads values aloud or sends a diagnostic report | nothing new | none |
| N1 Customer sends a snapshot | the customer clicks **Send to support** on the STICKER NFC page. The host sends decoded `info` / `config` / `radio-state` with the ADR-0347 MCP redaction (LoRaWAN keys, claim / vendor tokens), read-only | the key never leaves the host. Every participant in the namespace, the not-yet-allowed ones included, sees the snapshot (F30), so redaction is mandatory. The relay sees plaintext (ADR-0329) | needs the 0042/0043 GUI (not built), a new message type (cap-gated) and a joiner view |
| N2 Support asks for reads | as N1, but support requests `info` / `config` / `history`. The customer's Monitor runs them while the tag lies on the reader, after the join allow. Reads are not confirmed one by one, as with console commands | as N1. Replay is not an issue: the joiner sends JSON intents, never CCM frames. The host builds each frame with its own per-session counter (F39) | N1 + request handling and the tag-presence UX (tag must stay on the reader; 3 s firmware idle; field cycling) |
| N3 Support asks for changes | `config set` etc., each confirmed with a diff | a joiner (or a stranger who guessed the code) can inject requests. The relay cannot authenticate the sender (F1), so the only protection is the customer's per-change confirmation. Key, claim, vendor-reset, factory / device-reset and raw `nfc console` stay never-remote. It goes further than ADR-0347, which keeps MCP read-only because "any change needs a human in the GUI or CLI": the customer is that human, but the request comes from outside | high: diff UX, consent texts, a restart / RebootWait over the relay |

Recommendation: **N0 in this plan**. Then **N2 as a later plan** once 0042/0043 ship, plus an Advanced-mode flag
for the NFC page's Share. **N3 not before** real support cases show it is needed, and only with its own ADR that
amends ADR-0347's spirit. The secret key never leaves the host in any option. The relay carries only decoded,
redacted values.

### Threading, limits

Unchanged from 0044 (one relay worker per session, BoundedQueues, ADR-0357). New bounds: hello `targets` ≤ 8,
`prepared` ≤ 4 per target, `caps` ≤ 16. A `line-refused` message is sent at most once per refused line, and the line
rate limit (20 lines/s) already bounds them. The `generic` console profile has no unbounded state.

## Decisions requiring user confirmation

| # | Question | Options | Recommendation | Decision (by user) | ADR |
|---|---|---|---|---|---|
| D-1 | Which new devices get remote support | (a) Nordic chip + STICKER J-Link; NFC evaluated only (b) + NFC read-only now (c) also STM32 chip / `lrw` | (a): the two consoles the request names; NFC GUI is not built yet | **as recommended** (2026-11-24) | ADR-0415, 0420, 0421 |
| D-2 | How to extend the protocol | (a) optional fields + `caps`, `v` stays 1 (b) `v: 2` (c) new relay events | (a): `v: 2` and new events break every old peer (F5, F1) | **as recommended** (2026-11-24) | ADR-0414 |
| D-3 | What old joiners see from a Nordic / STICKER host | (a) legacy `ops: false` → console only (b) `ops: true` with the old `app` / `lte` UI | (a): the old UI would offer wrong targets that the host must refuse | **as recommended** (2026-11-24) | ADR-0414 |
| D-4 | New error ids toward old peers | (a) baseline `code` + optional `detail.code` (b) send new ids (old joiners reject them, F6) (c) per-peer (impossible: broadcast) | (a) | **as recommended** (2026-11-24) | ADR-0414 |
| D-5 | Who words the target names on the joiner | (a) the joiner translates from profile + target id; falls back to the host's sanitised name (b) the host sends display names (host's language) | (a): texts in support's language, fallback for unknown profiles | **as recommended** (2026-11-24) | ADR-0415 |
| D-6 | Console rules on the joiner | (a) the hello's profile when known, else a neutral `generic` profile (b) keep CHESTER for all | (a) | **as recommended** (2026-11-24) | ADR-0415 |
| D-7 | Until phase A2 ships for a device, may its tab be shared | (a) Share only on `chester` tabs (step A0, also in 0057 phase D) (b) leave it (shares as "CHESTER" with dead operations) | (a) | **as recommended** (2026-11-24) | ADR-0415 |
| D-8 | Command tree for non-CHESTER shells | (a) same learner as the local tab (Zephyr `help` / `-h`) (b) none | (a): STICKER debug has help on (F33) | **as recommended** (2026-11-24) | ADR-0415 |
| D-9 | STICKER secrets in a shared console | (a) redact on output (new word-prefix rule) for everyone, plus a share-view line (b) refuse `config show` remotely (c) nothing | (a) | **as recommended** (2026-11-24) | ADR-0416 |
| D-10 | Remote commands that change keys or identity | (a) deny-list refused on STICKER only (b) the same on every profile, CHESTER included (behaviour change) (c) allowed (redacted) as on CHESTER (d) STICKER allow-list: only listed commands pass from remote | (a): the STICKER's keys are its NFC access; the deny-list holds while the shell has no `select` / alias (F45, U11). (d) if you prefer safety over maintenance. CHESTER can be decided separately | **as recommended** (2026-11-24) | ADR-0416 |
| D-11 | Nordic chip remote operations | (a) erase (code), recover, reset (b) erase (code) and reset, no recover (c) reset only (d) none | (b). Three points weigh against recover: on nRF91 it erases the UICR and so the PIB (F48), which nothing can restore remotely (PIB writes are never remote, as D-15's principle for STICKER identity); a chip that needs recover usually has no working RTT, so it could not be shared anyway (operations-only sharing is STICKER only); and CHESTER's remote recover exists today, so (a) is the consistent alternative if you want parity | **as recommended** (2026-11-24) | ADR-0417 |
| D-12 | Nordic chip remote flash | (a) none in this plan (b) a file the customer picks, SHA-256 shown on both sides (c) URL + SHA-256 from support (d) CHESTER catalog ID | (a). (b) would be a later plan; (c) lets a remote party choose what is downloaded; (d) puts CHESTER firmware on a bare chip | **as recommended** (2026-11-24) | ADR-0417 |
| D-13 | STICKER remote flash | (a) prepared release + debug (b) release only (c) none; provisioning never in all options | (a): the debug console is the reason for the request | **as recommended** (2026-11-24) | ADR-0418 |
| D-14 | Does the customer's UI mode gate a remote debug flash | (a) no: support asks, the customer confirms (b) only when the host is in Expert mode | (a) | **as recommended** (2026-11-24) | ADR-0418 |
| D-15 | STICKER erase / recover / unlock remotely | (a) erase keeps identity only; erase-all, unlock, recover never (b) also erase-all with typed confirmation | (a): matches ADR-0409's MCP refusal | **as recommended** (2026-11-24) | ADR-0417 |
| D-16 | Target naming in `op-request` | (a) the profile target id string, checked against the hello's `targets` (b) extend the enum per device | (a) | **as recommended** (2026-11-24) | ADR-0414 |
| D-17 | Share a STICKER tab without a console (release firmware) | (a) yes, phase B, `sticker` only (b) no: the customer flashes debug locally (Expert) | (a) | **as recommended** (2026-11-24) | ADR-0419 |
| D-18 | STICKER over NFC | N0 / N1 / N2 / N3 (see Design) | N0 now, N2 as a later plan, N3 only with its own ADR | **as recommended** (2026-11-24) | ADR-0420 |
| D-19 | STM32 chip, CHESTER `lrw`, Bluetooth non-CHESTER | (a) out of scope (b) `lrw` reset / prepared flash now (c) STM32 chip as an operations-only share | (a). The STM32 chip has no console and no prepared firmware (only user files, which D-12 keeps local). `lrw` operations pause the shared `app` console during a cross-target flash (0040), and 0039 / 0040 are not merged (pull request #18). Bluetooth consoles exist only for CHESTER. The matrix leaves room for `lrw` later | **as recommended** (2026-11-24) | ADR-0421 |
| D-20 | Remote tab pages | (a) Console + Flash (remote card built from `targets`); no STICKER / NFC pages (b) mirror the device's local pages | (a) | **as recommended** (2026-11-24) | — |
| D-21 | CLI shape | (a) `hwio remote host --profile`, `join --target ID --flash-prepared` (b) per-device `remote-host` subcommands | (a) | **as recommended** (2026-11-24) | — |
| D-22 | MCP | (a) unchanged: no remote tools; a joiner's remote tab stays invisible to AI tools (F43) (b) read-only status | (a). An AI client on support's machine cannot type into a customer's device today, and that should stay so | **as recommended** (2026-11-24); *note 2026-11-24: amended by ADR-0433 (plan 0062): a joined remote tab is available to AI clients behind a setting* | — |
| D-23 | Release order | (a) A1 (protocol base, on main now) → A2 per device as its console ships → B → (later plan) NFC (b) all at once | (a) | **as recommended** (2026-11-24) | — |

## Implementation steps

Phase A1 — device-independent protocol base. **Buildable on main today**: CHESTER only; the new fields are tested
with synthetic profiles in unit tests.

- [x] A0 Gate Share to the `chester` profile (GUI `available()`; CLI `remote host` stays CHESTER-only) until phase A2
      is in. One-line guard, handed to 0057 phase D and 0058 if they ship first.
- [x] A1 Tunnel: `caps`, `profile`, `console`, `targets`, `prepared`, string targets, `detail.code`, baseline error
      set (frozen list from tag `v2.0.0-rc.4` + test), `line-refused` (cap-gated). Decoder bounds. Unit tests,
      including **golden messages from `v2.0.0-rc.4`** both ways.
- [x] A2 (one point pending confirmation: the no-hello profile, see "Implementation notes") Joiner: `generic` console profile, profile from the hello, target names translated by profile, Flash card
      from `targets`. Old-host fallback is the 0044 card unchanged.
- [x] A3 Host: profile-driven hello (name + part), legacy `ops: false` off CHESTER, `detail.code` on every
      denial / result / end. The new output rule kind `sensitiveOutputPrefixes` and the `refuseRemoteSensitive` /
      `remoteRefusedCommands` mechanism, with the `remote.command_refused` path. Unit-tested with a test profile.
- [x] A4 Translations, docs and interop checklist rows for what A1–A3 change on CHESTER (no visible change expected).

Phase A2 — device consoles. Needs the device's profile and console in Monitor: 0041 + 0039 + 0058 (STICKER), 0057
phase B/D (Nordic).

- [ ] A5 STICKER profile data: sensitive prefixes, output prefixes, refused commands (ADR-0416). Audit of U6 on the
      firmware source. Console-file, history and relay tests with the fake J-Link STICKER debug scenario (empty-prompt
      Zephyr shell, `config show` output).
- [ ] A6 CLI `remote host --profile/--target` (nrf, sticker), `remote join --target ID`. E2E tests with the fake
      J-Link (nrf52 scenario with RTT index fallback; STICKER debug scenario). Remove the A0 guard per profile.
- [ ] A7 Translations (4 languages, 0 unfinished), docs `remote-support.md` ×5, interop checklist additions.

Phase B — operations. Needs 0058 built (prepared firmware, `preserved`, `destroysIdentity`) and 0057 erase / recover.

- [ ] B1 Remote operations matrix in `TargetProfile`. `DeviceSessionOperations` driven by it (replaces F15 / F16).
      Never-remote list. Unit tests per profile.
- [ ] B2 Prepared flash (`prepared` id → `DesignatedFirmware`, pinned SHA-256). STICKER after-flash notice and
      `waiting-for-wake` phase.
- [ ] B3 Customer texts per device and target (table above), support result texts. Model name from the identified
      part.
- [ ] B4 Operations-only sharing (D-17): U2 check first, then `console: false` sessions and the switch to console after
      flashing debug.
- [ ] B5 GUI joiner card: firmware chooser, per-target kinds. GUI host: Share on the STICKER Flash page (D-17). Qt
      Quick tests.
- [ ] B6 CLI `--flash-prepared`, `--no-console`. E2E tests. Translations, docs.

Phase C — hardware and interop (manual).

- [ ] C1 Interop checklist rows: new host ↔ rc.4/rc.5 joiner (console only, no warnings), rc.4/rc.5 host ↔ new joiner
      (CHESTER unchanged), web Terminal joiner on a STICKER debug host, real relay.
- [ ] C2 HIL: STICKER debug console shared, `config show` redacted on the joiner and in the relay's debug log,
      `config secret-key 00…`, `config serial-number …` and `settings erase` refused. Prepared debug → release flash
      remotely. Nordic nRF52840 / nRF9160 console and erase / reset (recover only with D-11 (a)).

## Release order and dependencies

| Part | Needs | Earliest |
|---|---|---|
| A0 guard | nothing | now (or inside 0057 phase D / 0058) |
| Phase A1 (protocol base, A1–A4) | main only | any release; best **before** the first release with a Nordic or STICKER console, so that old-joiner degradation is in place from the start |
| Phase A2, STICKER console | 0041 merged (`sticker` profile; no pull request yet), 0039 merged (pull request #18), 0058 built with D14 = yes; 0044 D1 done for the interop base | the first release that ships 0058 |
| Phase A2, Nordic console | 0057 phases B and D (no pull request yet) | the first release that ships 0057's GUI / CLI |
| Phase B (operations) | 0058 built (prepared firmware, identity-preserving erase, ADR-0410 reachability); 0057 erase; U2 for B4 | the release after 0058 |
| NFC (N2) | 0042 / 0043 GUI, a new plan | later |

**Smallest useful first step:** A0 plus Phase A1 (A1–A4), which can be built on main now and changes nothing visible
for CHESTER. Then A5–A7 for the **STICKER debug console** as soon as 0041 + 0039 + 0058 are in. It is the case the
request names, and the one where secrets make a plan necessary. The Nordic console follows with 0057 phase B/D at
almost no extra cost (A6). Neither device can be shared before its own console exists in Monitor.

**Open point (implementation):** the exact mapping table from each error id added after `v2.0.0-rc.4` to a baseline
code. The rule: same category, nearest meaning. Step A1 lists the table in this plan's implementation notes for the
user's review.

## Implementation notes (A0–A4, 2026-11-24)

**A1 baseline error mapping** (the open point above). The baseline set is the `errorTable` of tag `v2.0.0-rc.4`
(100 ids), frozen in `libs/remote/src/baseline_errors.cpp`; `Rc4Baseline.IsTheFrozenRc4SetAndCoversEveryErrorCode`
fails for an `ErrorCode` without a row. Error ids on main that rc.4 does not know, and what an rc.4 peer receives:

| New id | Category | Sent as `code` (rc.4) | Reasoning |
|---|---|---|---|
| `target.low_voltage` (plan 0051, after rc.4) | Target | `target.no_power` | Same category; its text ("the target has no power … check the power supply") names the same cause and remedy. **Judgement call:** rc.4 itself reported the nRF Util LOW_VOLTAGE failure as `firmware.modem_update_failed` (Firmware); that is what an rc.4 host would have sent for the same event. The category rule gives `target.no_power`. Main already sends this id in `op-result`, which rc.4 / rc.5 joiners reject today (a warning, then the 300 s inactivity timeout) |
| `remote.target_unsupported` (A1) | Usage | `remote.operation_denied` | The plan's own example. `remote.operation_unsupported_peer` (same category) was rejected: its text names the web Terminal |
| `remote.command_refused` (A3) | Usage | `remote.operation_denied` | Only travels in `line-refused`, which is sent only to joiners with the `refused-lines` cap; the row exists so that every id has a mapping |

`remote.no_console` and `remote.console_unavailable` are phase B (ADR-0419) and are not added yet; their rows come
with them (proposed: `remote.operation_unsupported_peer` for both, whose text "supports only the console" is the
nearest).

**Wire details decided in A1** (within ADR-0414):
- The `detail` of `op-result` sits inside `error` (`error: {code, detail: {code}}`), next to the code it refines;
  in `op-denied`, `end` and `line-refused` it is top level. `detail` is sent only when the real id differs from the
  baseline id, so a CHESTER host's messages with rc.4 ids are byte-identical to rc.4's. A decoder takes
  `detail.code` when it knows that id, else `code`; `code` must still be a known id.
- Unknown caps are kept (as data), unknown kinds in a target are skipped; malformed extension fields (wrong type, a
  name outside `[a-z0-9-]` / over 32 bytes, over a bound, duplicate target or prepared ids) reject the hello, as the
  0044 fields do.
- `OpTarget` is a bounded value type (32 bytes inline, constants `App` / `Lte`, `fromId()`); `OpRequest` keeps
  `firmwareId` and gains `prepared` (exactly one of them for flash) instead of the `std::variant` sketched in the
  design, so that the 0044 call sites and tests stay unchanged.
- A request for a target the host does not have is now denied with `remote.target_unsupported` (it did not decode
  before: no answer, F8). A request rc.4 could send keeps its old code (`remote.protocol_error` for flash to `lte`).

**A2:** `RemoteConsoleSession::profileFor(hello)`: the named profile when this build has it, else the `generic` console
profile (`device::genericConsoleProfile()`, not in `ProfileRegistry`). **Deviation, to confirm:** a hello without
`profile` (rc.4 / rc.5 host) and no hello at all (web Terminal host, and a Monitor host before its hello) keep the
`chester` rules, not `generic` as the Design text says for "before the hello". Reason: such hosts share CHESTERs only
(the compatibility matrix: "as today"), and `generic` would drop the CHESTER sensitive-command rules on the joiner. The
GUI tab takes the session's profile on every host hello (`configurationChanged`). The Flash card's target buttons
come from `RemoteJoinViewModel::targets` (an older host: Application core / LTE modem core as in 0044); per-target
kinds and the firmware chooser are B5. The joiner offers operations when the hello says `ops: true` or lists a
target with kinds (`tunnel::offersOperations`), so a host of another profile (`ops: false` for older joiners) gets
the card. The alternative for the no-hello case is `generic` (the Design text): the joiner then drops the CHESTER
sensitive-command and `info show` output rules for web Terminal hosts and before a Monitor host's hello; the switch is
one branch in `RemoteConsoleSession::profileFor()`.

**A3:** the host sends `profile` and `caps: [detail-codes, refused-lines]`. It does not send `targets` (and not the
`targets` / `prepared` caps) before the operations matrix of B1. The device `name` stays the profile display name: the
"+ part" naming needs the identified part of a Nordic / STICKER session and comes with A6. `ops: false` for every
profile other than `chester` (an operation request is then refused as on a console without operations). The refusal
rules are `TargetProfile::refuseRemoteSensitive`, `remoteRefusedSetters` (refused only with an argument:
`config serial-number`, `config nonce-counter`) and `remoteRefusedCommands`; `device::refusedFromRemote()`. The
redaction rules are read from the session's own profile (not by id from the registry). `sensitiveOutputPrefixes`
applies to the relay, the console file and the shell-query debug log; its STICKER data and the console-file test with
STICKER data are A5.

## Tests and verification

- Unit (libs/remote):
  - tunnel decode / encode of every new field with bounds;
  - golden rc.4 messages: new host → old decoder (via the frozen copy of the rc.4 decoder in the test, from the tag)
    and old host messages → new decoder;
  - the baseline error-code mapping covers every `ErrorCode` (test fails on a new id without a mapping);
  - `line-refused` only with the cap;
  - the joiner never sends a non-`app` / `lte` target or `prepared` to a hello without `targets`.
- Unit (libs/device): STICKER redaction rules (command, output without a colon) in the console file, history and
  relay; remote-refused commands; the operations matrix per profile (never-remote invariants: no `destroysIdentity`
  operation, no provisioning flavour, no PIB).
- `FakeRelay` integration per device:
  - nrf52 host ↔ joiner: console, tree, erase / recover / reset;
  - STICKER debug host ↔ joiner: console, redaction, refused command, prepared flash with the fake J-Link and a test
    designated image;
  - STICKER release, operations only (B4);
  - new host ↔ simulated rc.4 joiner: console only, no warning;
  - rc.4 host (recorded messages) ↔ new joiner.
- CLI e2e for `remote host --profile nrf|sticker` and `join --target / --flash-prepared`.
- Qt Quick: joiner card from `targets` (STICKER, Nordic, CHESTER, old host); host texts; Share on the STICKER Flash
  page (B5).
- ASan/UBSan, TSan for libs/remote; clang-tidy zero findings; qmllint; `check_translations`.
- Manual: Phase C (real relay, web Terminal, rc.5 package, STICKER and nRF hardware).

## Risks and open points

- **Secret redaction is name-based** (U6). A firmware update can add a command that prints a key. Mitigations: the
  STICKER profile list is reviewed with each prepared-firmware bump (the one-line profile change of ADR-0408 also
  touches the list), and the share text promises only the known keys.
- **Broadcast visibility** (F30): someone who guesses the code sees every redacted console line before approval. This
  is unchanged and accepted by ADR-0334. It matters more for STICKER, hence redaction for everyone.
- **Debug firmware left on a customer's STICKER** drains the battery. Mitigations: the confirmation text, and support's
  result line suggesting the release flash at the end. A host notice when the session ends with debug firmware on the
  device is an optional extra (ask at review).
- **Old joiner wording:** an rc.4 joiner explains `ops: false` with `remote.operation_unsupported_peer`, whose cause
  text names the web Terminal (`libs/core/src/error*.cpp`, the `RemoteOperationUnsupportedPeer` texts). It is
  acceptable and documented in the interop checklist.
- **0057 user-mode inconsistency** (Nordic entry in every mode vs Advanced) does not affect remote, which follows the
  tab that exists.
- 0044 itself is still In progress (D1). Phase C adds rows to the same checklist.
