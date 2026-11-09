#pragma once

// CHESTER LoRaWAN module commands (plan/features/0040-chester-lrw-module.md A4, ADR-0277,
// ADR-0283):
//   hwio chester lrw info
//   hwio chester lrw flash [FILE] [--address ADDR] [--no-cache] [--direct [--halt]]
//   hwio chester lrw erase [--yes] [--direct]
//   hwio chester lrw unlock [--yes] [--direct]
//   hwio chester lrw reset [--halt]
// The module answers only while the application MCU is held. flash / erase / unlock are guided by
// LrwUpdateProcedure in an interactive terminal (prompts to move the cable, like `chester lte
// flash --guided`); --direct runs the plain `hwio stm32` operation on the target `lrw` (the caller
// has held the application MCU and the cable is on the LoRaWAN connector) and is required without
// a terminal or with --json. `flash` without FILE flashes the prepared lora-modem firmware of the
// profile (downloaded and verified, ADR-0278). info / reset connect directly.

#include "cli_app.h"

#include <CLI/App.hpp>

namespace hwio::cli {

void registerLrwCommands(CLI::App& chester, CommandSlot& slot, const GlobalOptions& globals);

} // namespace hwio::cli
