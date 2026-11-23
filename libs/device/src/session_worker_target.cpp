#include "session_worker_target.h"

#include "flash_workflows.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/hw/jlink/programmer.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hwio::device::detail {

namespace {

void log(const WorkflowContext& context, const std::string& text) {
    if (context.log) {
        context.log(text);
    }
}

// The family check (review R7) before any access. A protected chip is an error here
// (target.protected), as for a memory read.
Status identified(WorkflowContext& context) {
    if (auto chip = identifyTarget(context, false); !chip) {
        return std::unexpected(std::move(chip).error());
    }
    return {};
}

TargetStatus haltedStatus(const WorkflowContext& context, bool halted) {
    return TargetStatus{
        .halted = halted, .coreId = std::nullopt, .deviceName = context.connectedDevice};
}

} // namespace

Result<TargetStatus> runHalt(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    log(context, "Halt");
    if (Status halted = context.programmer->halt(); !halted) {
        return std::unexpected(std::move(halted).error());
    }
    return haltedStatus(context, true);
}

Result<TargetStatus> runGo(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    log(context, "Go");
    if (Status running = context.programmer->go(); !running) {
        return std::unexpected(std::move(running).error());
    }
    return haltedStatus(context, false);
}

Result<TargetStatus> runTargetStatus(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    auto halted = context.programmer->isHalted();
    if (!halted) {
        return std::unexpected(std::move(halted).error());
    }
    // Review S6: without JLINKARM_GetId the status is still reported, without a core id.
    TargetStatus status = haltedStatus(context, *halted);
    if (auto coreId = context.programmer->coreId(); coreId) {
        status.coreId = *coreId;
    } else if (coreId.error().code == ErrorCode::DependencyJLinkFunctionMissing) {
        log(context, "Core id unknown (" + coreId.error().detail + " missing)");
    } else {
        return std::unexpected(std::move(coreId).error());
    }
    return status;
}

Status runWriteMemory(WorkflowContext& context, std::uint32_t address,
                      std::span<const std::byte> data, AccessWidth width) {
    // ADR-0160: checked again here (DeviceSession checks at request time), before any access.
    if (context.target == nullptr) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no target"));
    }
    if (Status allowed = checkMemoryWrite(*context.target, address, data.size()); !allowed) {
        return allowed;
    }
    if (Status ready = identified(context); !ready) {
        return ready;
    }
    log(context, std::format("Writing {} bytes at 0x{:08X} ({}-bit units)", data.size(), address,
                             jlink::unitBytes(width) * 8));
    return context.programmer->writeMemory(address, data, width);
}

Result<std::vector<RegisterValue>> runReadRegisters(WorkflowContext& context, bool haltIfRunning) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    // Plan 0049 (ADR-0372): halt only a running core and let only that one run again.
    bool weHalted = false;
    if (haltIfRunning) {
        auto halted = context.programmer->isHalted();
        if (!halted) {
            return std::unexpected(std::move(halted).error());
        }
        if (!*halted) {
            log(context, "Halting the CPU to read the registers");
            if (Status stopped = context.programmer->halt(); !stopped) {
                return std::unexpected(std::move(stopped).error());
            }
            weHalted = true;
        }
    }
    auto registers = context.programmer->readRegisters();
    if (weHalted) {
        // Always, also after a failed read; the first error wins.
        log(context, "Letting the CPU run again");
        if (Status running = context.programmer->go(); !running) {
            log(context, "Go failed: the CPU may remain halted");
            if (registers) {
                return std::unexpected(std::move(running).error());
            }
        }
    }
    return registers;
}

Result<std::vector<MemoryZone>> runMemoryZones(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    return context.programmer->memoryZones();
}

Result<jlink::ChipInfo> runWriteFlash(WorkflowContext& context,
                                      const firmware::FirmwareImage& image, bool allowUicr) {
    return runFlash(context, image, FlashOptions{.halt = false, .allowUicr = allowUicr});
}

} // namespace hwio::device::detail
