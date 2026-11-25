#pragma once

// The compiled-in device profiles (ADR-0032), defined one per file in src/profiles/ and listed in
// ProfileRegistry (profile.cpp). Constant-initialized: no static initialization order issues.

#include "hwio/device/profile.h"

namespace hwio::device::profiles {

extern const DeviceProfile chester;
// Plan 0059 (ADR-0415): not registered (genericConsoleProfile()).
extern const DeviceProfile generic;

} // namespace hwio::device::profiles
