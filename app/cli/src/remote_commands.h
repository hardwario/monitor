#pragma once

// `hwio remote host` / `hwio remote join` (plan/features/0044, ADR-0331). English only (ADR-0027).
//
//   hwio remote host [-n SN] [--jlink-speed S] [--latency MS] [--console-file P] [--auto-reconnect]
//                    | --ble (-a ID | --name N) [--console-file P] [--auto-reconnect]
//                    [--relay URL] [--deny-operations] [--json]
//     Shares the CHESTER console (J-Link RTT by default, Bluetooth with --ble): prints the session
//     code and link, streams the console (as `chester app console` / `ble console` without input)
//     and asks on stdin (stderr prompts) to allow every join and every operation request; any
//     other input line keeps sharing after the idle warning. Without a terminal on stdin, joins
//     are never allowed (the session ends at the first join, ADR-0335) and operations are denied.
//     --deny-operations denies operation requests without asking. Ctrl+C ends sharing (exit 130,
//     as the console). The session ends also by the limits (ADR-0328) and the errors of plan 0044.
//
//   hwio remote join CODE|LINK [--relay URL] [--json]
//                    [--command CMD [-t S]] | [--flash ID | --erase | --recover | --reset]
//                    [--target app|lte]
//     Joins a session: the interactive console (or plain stdin lines / --json NDJSON as `chester
//     app console`), one command (after the customer allowed this participant), or one operation
//     (a Monitor host only: flash by catalog firmware ID, erase, recover, reset). A link to a relay
//     other than the configured one (setting remote/relayUrl or --relay) needs confirmation on a
//     terminal; without one it is usage.invalid_argument (ADR-0330).
//
// Exit codes (plan/0006-cli.md): remote.join_not_allowed and remote.operation_denied are
// `cancelled` (130, "aborted"); relay errors `network` (8).

#include "cli_app.h"

#include <CLI/App.hpp>

#include <memory>

namespace hwio::cli {

struct ProbeOptions;

void registerRemoteCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

} // namespace hwio::cli
