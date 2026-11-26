#pragma once

// The error ids an older remote peer knows (plan 0059, ADR-0414). Monitor 2.0.0-rc.4 rejects an
// op-denied, op-result or end whose `code` is not one of its own error ids
// (libs/remote/src/tunnel.cpp at tag v2.0.0-rc.4), and the relay broadcasts, so the host cannot
// choose per peer. Every such message therefore carries a `code` from the frozen rc.4 set below
// and, when the real error id differs, `detail: {code: <current id>}` (TunnelCodec), which a newer
// joiner shows instead.
//
// The set is the errorTable of tag v2.0.0-rc.4 (libs/core/include/hwio/core/error.h), frozen; it
// never changes. A newer id maps to the rc.4 id of its category with the nearest meaning (the table
// in baseline_errors.cpp and plan 0059 § Implementation notes). A unit test fails for an ErrorCode
// without a mapping. Pure functions, any thread.

#include "hwio/core/error.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace hwio::remote {

// The 100 error ids of Monitor 2.0.0-rc.4.
inline constexpr std::size_t baselineErrorCount = 100;
[[nodiscard]] std::span<const std::string_view> baselineErrorIds() noexcept;
[[nodiscard]] bool isBaselineErrorId(std::string_view id) noexcept;
// The id an rc.4 peer knows for `code`: its own id when it is in the set, else the mapped one.
[[nodiscard]] std::string_view baselineErrorId(ErrorCode code) noexcept;
// True when `code` is in the rc.4 set or has an explicit mapping (the unit test checks every code).
[[nodiscard]] bool hasBaselineMapping(ErrorCode code) noexcept;

} // namespace hwio::remote
